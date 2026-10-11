#include "sv/build_info.hpp"
#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include "sv/vision.hpp"
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
        if (argc != 4)
        {
            throw std::runtime_error("usage: probe CONFIG MANIFEST OUTPUT");
        }
        const auto config = sv::load_config(argv[1]);
        const auto rows = sv::load_manifest(argv[2], config);
        const std::filesystem::path output = argv[3];
        std::filesystem::create_directories(output);
        sv::FrameSet set;
        for (int c = 0; c < 4; ++c)
        {
            auto image = std::make_shared<sv::Image>(sv::read_image(rows.front().paths[c]));
            set.frames[c] = sv::Frame{c, 0, 0, 0, image};
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
                    auto settings = config.fusion;
                    settings.mode = mode;
                    settings.pyramid_boundary = boundary;
                    settings.diagnostic = diagnostic;
                    renderer.set_fusion(settings);
                    sv::RenderInspection inspection;
                    const auto actual = renderer.render(set, config.view, &inspection);
                    if (!inspection.samples)
                    {
                        throw std::runtime_error("missing projected samples");
                    }
                    const auto directory =
                        output / (std::string(mode) + "-" + diagnostic +
                                  (std::string(boundary) == "zero" ? "" : "-normalized"));
                    std::filesystem::create_directory(directory);
                    for (int c = 0; c < 4; ++c)
                    {
                        const auto name = "camera" + std::to_string(c);
                        matrix(directory / (name + "-linear.f32"), inspection.samples->colors[c]);
                        matrix(directory / (name + "-valid.u8"), inspection.samples->validity[c]);
                        matrix(directory / (name + "-edge.f32"),
                               inspection.samples->edge_weights[c]);
                    }
                    bytes(directory / "actual.rgba", actual.pixels.data(), actual.pixels.size());
                    bytes(directory / "fallback.rgba", inspection.fallback_rgba.pixels.data(),
                          inspection.fallback_rgba.pixels.size());
                    matrix(directory / "carrier-regions.u8", inspection.carrier_regions);
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
                    cases.emplace_back(
                        boost::json::object{{"mode", mode},
                                            {"diagnostic", diagnostic},
                                            {"boundary", boundary},
                                            {"directory", directory.filename().string()}});
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
