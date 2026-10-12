#include "sv/build_info.hpp"
#include "sv/fusion_runtime.hpp"
#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <charconv>
#include <fstream>
#include <iostream>

namespace
{
void bytes(const std::filesystem::path &path, const void *data, size_t size)
{
    std::ofstream output(path, std::ios::binary);
    output.write(static_cast<const char *>(data), size);
    if (!output)
    {
        throw std::runtime_error("cannot write probe output");
    }
}

void matrix(const std::filesystem::path &path, const cv::Mat &value)
{
    if (!value.isContinuous())
    {
        throw std::runtime_error("probe matrix is not contiguous");
    }
    bytes(path, value.data, value.total() * value.elemSize());
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc < 4 || argc > 6)
        {
            throw std::runtime_error(
                "usage: probe CONFIG MANIFEST OUTPUT [FRAME_INDEX] [--color-only]");
        }
        const auto config = sv::load_config(argv[1]);
        const auto rows = sv::load_manifest(argv[2], config);
        uint64_t frame_index = 0;
        bool color_only = false;
        if (argc >= 5)
        {
            const std::string argument = argv[4];
            if (argument == "--color-only")
            {
                color_only = true;
            }
            else
            {
                const auto parsed = std::from_chars(argument.data(),
                                                    argument.data() + argument.size(), frame_index);
                if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size())
                {
                    throw std::invalid_argument("invalid frame index");
                }
            }
        }
        if (argc == 6)
        {
            if (color_only || std::string(argv[5]) != "--color-only")
            {
                throw std::invalid_argument("expected --color-only after frame index");
            }
            color_only = true;
        }
        if (frame_index >= rows.size())
        {
            throw std::out_of_range("frame index outside manifest");
        }
        const auto &row = rows.at(frame_index);
        const std::filesystem::path output = argv[3];
        std::filesystem::create_directories(output);
        sv::FrameSet set;
        boost::json::array inputs;
        for (int c = 0; c < 4; ++c)
        {
            auto image = std::make_shared<sv::Image>(sv::read_image(row.paths[c]));
            set.frames[c] = sv::Frame{c, frame_index, 0, row.scenario_ns, image};
            inputs.emplace_back(boost::json::object{{"camera_id", c},
                                                    {"path", row.paths[c].string()},
                                                    {"sha256", sv::file_sha256(row.paths[c])}});
        }
        sv::Renderer renderer(config);
        boost::json::array cases;
        for (const auto *boundary : {"zero", "normalized"})
        {
            for (const auto *mode :
                 {"seam_distance_feather", "graph_cut_seam", "multi_band", "graph_cut_multi_band"})
            {
                if (std::string(boundary) == "normalized" && std::string(mode) != "multi_band" &&
                    std::string(mode) != "graph_cut_multi_band")
                {
                    continue;
                }
                for (const auto *diagnostic : {"color", "weights", "coverage"})
                {
                    if (color_only && std::string(diagnostic) != "color")
                    {
                        continue;
                    }
                    auto settings = config.fusion;
                    settings.mode = mode;
                    settings.pyramid_boundary = boundary;
                    settings.diagnostic = diagnostic;
                    renderer.set_fusion(settings);
                    sv::RenderInspection inspection;
                    const auto actual =
                        renderer.render(set, config.view, &inspection, {true, true, true});
                    if (inspection.carrier_positions.type() != CV_32FC3 ||
                        inspection.carrier_positions.size() !=
                            cv::Size(config.width, config.height))
                    {
                        throw std::runtime_error(
                            "missing carrier positions during hybrid inspection");
                    }
                    if (!inspection.samples)
                    {
                        throw std::runtime_error("missing projected samples");
                    }
                    if (!inspection.fusion)
                    {
                        throw std::runtime_error("missing fusion contribution inspection");
                    }
                    const auto directory =
                        output / (std::string(mode) + "-" + diagnostic +
                                  (std::string(boundary) == "zero" ? "" : "-normalized"));
                    std::filesystem::create_directory(directory);
                    boost::json::array fusion_levels;
                    matrix(directory / "blend-weights.f32", inspection.fusion->blend_weights);
                    for (size_t level = 0; level < inspection.fusion->level_weights.size(); ++level)
                    {
                        const auto &weights = inspection.fusion->level_weights[level];
                        boost::json::array bands;
                        for (int camera = 0; camera < 4; ++camera)
                        {
                            const auto name = "level" + std::to_string(level) + "-camera" +
                                              std::to_string(camera) + "-band.f32";
                            matrix(directory / name,
                                   inspection.fusion->level_contributions.at(level)[camera]);
                            bands.emplace_back(name);
                        }
                        fusion_levels.emplace_back(boost::json::object{
                            {"file", "level" + std::to_string(level) + "-weights.f32"},
                            {"width", weights.cols},
                            {"height", weights.rows},
                            {"channels", 4},
                            {"contributions", bands}});
                        matrix(directory / ("level" + std::to_string(level) + "-weights.f32"),
                               inspection.fusion->level_weights[level]);
                    }
                    for (int c = 0; c < 4; ++c)
                    {
                        const auto name = "camera" + std::to_string(c);
                        matrix(directory / (name + "-contribution.f32"),
                               inspection.fusion->camera_contributions[c]);
                        const auto &source = inspection.source_projections[c];
                        matrix(directory / (name + "-source-pixels.f32"), source.pixels);
                        matrix(directory / (name + "-source-status.u8"), source.status);
                        for (int y = 0; y < config.height; ++y)
                        {
                            for (int x = 0; x < config.width; ++x)
                            {
                                const auto region = inspection.carrier_regions.at<uchar>(y, x);
                                // Inspection always depth-masks ego, even in diagnostic views.
                                if (region == 0 || region == 5)
                                {
                                    continue;
                                }
                                const bool valid = source.status.at<uchar>(y, x) == 1;
                                if (valid != (inspection.samples->validity[c].at<uchar>(y, x) != 0))
                                {
                                    throw std::runtime_error(
                                        "source projection/sample validity mismatch");
                                }
                            }
                        }
                        matrix(directory / (name + "-linear.f32"), inspection.samples->colors[c]);
                        matrix(directory / (name + "-valid.u8"), inspection.samples->validity[c]);
                        matrix(directory / (name + "-edge.f32"),
                               inspection.samples->edge_weights[c]);
                    }
                    bytes(directory / "actual.rgba", actual.pixels.data(), actual.pixels.size());
                    bytes(directory / "fallback.rgba", inspection.fallback_rgba.pixels.data(),
                          inspection.fallback_rgba.pixels.size());
                    matrix(directory / "carrier-regions.u8", inspection.carrier_regions);
                    matrix(directory / "carrier-positions.f32", inspection.carrier_positions);
                    if (!inspection.ego_rgba.empty())
                    {
                        matrix(directory / "ego.rgba", inspection.ego_rgba);
                    }
                    // Inspection must not change the production output or resource reuse.
                    const auto plain = renderer.render(set, config.view);
                    if (plain.pixels != actual.pixels || renderer.uploads() != 4 ||
                        renderer.mesh_builds() != 1)
                    {
                        throw std::runtime_error("inspection changed output/resources");
                    }
                    std::vector<std::filesystem::path> paths;
                    for (const auto &entry : std::filesystem::directory_iterator(directory))
                    {
                        if (entry.is_regular_file())
                        {
                            paths.push_back(entry.path());
                        }
                    }
                    std::sort(paths.begin(), paths.end());
                    boost::json::object files;
                    for (const auto &path : paths)
                    {
                        files[path.filename().string()] =
                            boost::json::object{{"sha256", sv::file_sha256(path)},
                                                {"bytes", std::filesystem::file_size(path)}};
                    }
                    cases.emplace_back(boost::json::object{
                        {"mode", mode},
                        {"diagnostic", diagnostic},
                        {"boundary", boundary},
                        {"directory", directory.filename().string()},
                        {"fusion_levels", fusion_levels},
                        {"settings", sv::fusion_settings(settings)},
                        {"files", files},
                        {"contribution_scope", "signed_linear_rgb_pre_clamp_pre_composition"}});
                }
            }
        }
        const uint16_t endian = 1;
        sv::write_json(output / "index.json",
                       boost::json::object{
                           {"width", config.width},
                           {"height", config.height},
                           {"float_endian",
                            *reinterpret_cast<const unsigned char *>(&endian) ? "little" : "big"},
                           {"origin", "top_left"},
                           {"frame_index", frame_index},
                           {"scenario_timestamp_ns", std::to_string(row.scenario_ns)},
                           {"config_sha256", sv::file_sha256(argv[1])},
                           {"manifest_sha256", sv::file_sha256(argv[2])},
                           {"inputs", inputs},
                           {"normal_output_unchanged", true},
                           {"color_only", color_only},
                           {"cases", cases},
                           {"device", renderer.device()},
                           {"config", config.effective},
                           {"source_revision", sv::source_revision},
                           {"source_fingerprint", sv::source_fingerprint}});
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
