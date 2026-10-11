#include "sv/build_info.hpp"
#include "sv/render_inspection.hpp"
#include "sv/renderer.hpp"
#include "sv/report.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 3)
        {
            throw std::runtime_error("usage: sv-carrier-probe CONFIG OUTPUT_DIRECTORY");
        }
        const auto config = sv::load_config(argv[1]);
        const std::filesystem::path directory = argv[2];
        std::filesystem::create_directories(directory);
        sv::Renderer renderer(config);
        sv::RenderInspection inspection;
        const auto inspected = renderer.render({}, config.view, &inspection);
        if (renderer.render({}, config.view).pixels != inspected.pixels)
        {
            throw std::runtime_error("geometry inspection changed normal output");
        }
        const auto &regions = inspection.carrier_regions;
        const std::array<const char *, 6> names{
            "background", "floor", "shell", "vehicle_footprint", "raised_bowl", "vehicle_model"};
        std::array<size_t, 6> counts{};
        for (int y = 0; y < regions.rows; ++y)
        {
            for (int x = 0; x < regions.cols; ++x)
            {
                const auto id = regions.at<uchar>(y, x);
                if (id >= counts.size())
                {
                    throw std::runtime_error("unknown carrier region");
                }
                ++counts[id];
            }
        }
        const auto mask = directory / "carrier-regions.u8";
        {
            std::ofstream stream(mask, std::ios::binary);
            stream.write(reinterpret_cast<const char *>(regions.data), regions.total());
            if (!stream)
            {
                throw std::runtime_error("cannot write carrier mask");
            }
        }
        boost::json::object pixels;
        for (size_t id = 0; id < names.size(); ++id)
        {
            pixels[names[id]] = counts[id];
        }
        sv::write_json(directory / "report.json",
                       boost::json::object{{"schema_version", 1},
                                           {"scope", "depth_tested_carrier_geometry"},
                                           {"source_revision", sv::source_revision},
                                           {"source_fingerprint", sv::source_fingerprint},
                                           {"config_sha256", sv::file_sha256(argv[1])},
                                           {"config", config.effective},
                                           {"device", renderer.device()},
                                           {"mesh_resources", renderer.mesh_resources()},
                                           {"width", config.width},
                                           {"height", config.height},
                                           {"origin", "top_left"},
                                           {"mask_sha256", sv::file_sha256(mask)},
                                           {"pixels", pixels},
                                           {"normal_output_unchanged", true},
                                           {"source_frames", 0}});
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
