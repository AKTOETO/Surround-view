#include "sv/build_info.hpp"
#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>

namespace
{
boost::json::object write_matrix(const std::filesystem::path &directory, const std::string &name,
                                 const cv::Mat &matrix)
{
    if (matrix.empty() || (matrix.depth() != CV_32F && matrix.depth() != CV_8U))
    {
        throw std::runtime_error("unsupported diagnostic matrix");
    }
    const auto path = directory / name;
    std::ofstream stream(path, std::ios::binary);
    for (int y = 0; y < matrix.rows; ++y)
    {
        for (int element = 0; element < matrix.cols * matrix.channels(); ++element)
        {
            if (matrix.depth() == CV_8U)
            {
                stream.put(static_cast<char>(matrix.ptr<unsigned char>(y)[element]));
            }
            else
            {
                static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
                uint32_t bits;
                const float value = matrix.ptr<float>(y)[element];
                std::memcpy(&bits, &value, sizeof(bits));
                for (int byte = 0; byte < 4; ++byte)
                {
                    stream.put(static_cast<char>((bits >> (byte * 8)) & 255));
                }
            }
        }
    }
    stream.close();
    if (!stream)
    {
        throw std::runtime_error("cannot write diagnostic matrix");
    }
    return {{"file", name},
            {"sha256", sv::file_sha256(path)},
            {"width", matrix.cols},
            {"height", matrix.rows},
            {"channels", matrix.channels()},
            {"dtype", matrix.depth() == CV_32F ? "float32" : "uint8"},
            {"byte_order", "little_endian"},
            {"origin", "top_left"}};
}
} // namespace

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4 && argc != 5)
        {
            throw std::runtime_error(
                "usage: sv-correspondence-probe CONFIG MANIFEST OUTPUT_DIRECTORY [FRAME_INDEX]");
        }
        const auto config = sv::load_config(argv[1]);
        const auto rows = sv::load_manifest(argv[2], config);
        if (rows.empty())
        {
            throw std::runtime_error("empty replay manifest");
        }
        uint64_t frame_index = 0;
        if (argc == 5)
        {
            const std::string text = argv[4];
            const auto parsed =
                std::from_chars(text.data(), text.data() + text.size(), frame_index);
            if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
            {
                throw std::invalid_argument("invalid frame index");
            }
        }
        if (frame_index >= rows.size())
        {
            throw std::out_of_range("frame index outside manifest");
        }
        const auto &row = rows.at(frame_index);
        const std::filesystem::path directory = argv[3];
        std::filesystem::create_directories(directory);
        sv::FrameSet frames;
        boost::json::array inputs;
        for (int camera = 0; camera < 4; ++camera)
        {
            const auto &path = row.paths[camera];
            auto image = std::make_shared<sv::Image>(sv::read_image(path));
            frames.frames[camera] = sv::Frame{camera, frame_index, 0, row.scenario_ns, image};
            inputs.emplace_back(boost::json::object{
                {"camera_id", camera}, {"path", path.string()}, {"sha256", sv::file_sha256(path)}});
        }
        sv::Renderer renderer(config);
        sv::RenderInspection inspection;
        const auto plain = renderer.render(frames, config.view);
        const auto captured = renderer.render(frames, config.view, &inspection, {true, true});
        if (plain.pixels != captured.pixels ||
            plain.pixels != renderer.render(frames, config.view).pixels)
        {
            throw std::runtime_error("diagnostic capture changed normal RGBA");
        }
        boost::json::array products;
        products.push_back(
            write_matrix(directory, "carrier-positions.f32", inspection.carrier_positions));
        products.push_back(
            write_matrix(directory, "carrier-regions.u8", inspection.carrier_regions));
        cv::Mat rgba(captured.height, captured.width, CV_8UC4,
                     const_cast<unsigned char *>(captured.pixels.data()));
        products.push_back(write_matrix(directory, "output.rgba", rgba));
        for (int camera = 0; camera < 4; ++camera)
        {
            const auto prefix = "camera" + std::to_string(camera);
            products.push_back(write_matrix(directory, prefix + "-pixels.f32",
                                            inspection.source_projections[camera].pixels));
            products.push_back(write_matrix(directory, prefix + "-status.u8",
                                            inspection.source_projections[camera].status));
        }
        sv::write_json(
            directory / "report.json",
            boost::json::object{{"schema_version", 1},
                                {"scope", "selected_manifest_frame_native_correspondence"},
                                {"source_revision", sv::source_revision},
                                {"source_fingerprint", sv::source_fingerprint},
                                {"config_sha256", sv::file_sha256(argv[1])},
                                {"manifest_sha256", sv::file_sha256(argv[2])},
                                {"config", config.effective},
                                {"device", renderer.device()},
                                {"frame_index", frame_index},
                                {"scenario_timestamp_ns", std::to_string(row.scenario_ns)},
                                {"normal_output_unchanged", true},
                                {"inputs", inputs},
                                {"products", products}});
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
