// Test fixture only: native mask oracle for live server wire parity.
#include "sv/build_info.hpp"
#include "sv/output_product.hpp"
#include "sv/renderer.hpp"
#include "sv/report.hpp"
#include "sv/vision.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc < 4 || argc > 6)
        {
            throw std::invalid_argument("expected CONFIG MANIFEST OUTPUT [PRODUCT [FRAME_INDEX]]");
        }
        const auto config = sv::load_config(argv[1]);
        const auto rows = sv::load_manifest(argv[2], config);
        const auto frame_index = argc == 6 ? sv::parse_decimal_u64(argv[5]) : 0;
        sv::FrameSet frames;
        boost::json::array inputs;
        for (int camera = 0; camera < 4; ++camera)
        {
            auto image =
                std::make_shared<sv::Image>(sv::read_image(rows.at(frame_index).paths[camera]));
            sv::Frame frame;
            frame.camera_id = camera;
            frame.image = std::move(image);
            frames.frames[camera] = std::move(frame);
            const auto &path = rows.at(frame_index).paths[camera];
            inputs.emplace_back(boost::json::object{
                {"camera_id", camera}, {"path", path.string()}, {"sha256", sv::file_sha256(path)}});
        }
        sv::Renderer renderer(config);
        sv::RenderInspection inspection;
        const auto product =
            argc >= 5 ? sv::parse_output_product(argv[4]) : sv::OutputProduct::RenderSupport;
        const bool visibility = product == sv::OutputProduct::SourceVisibility;
        const bool disagreement = product == sv::OutputProduct::SourceDisagreement;
        if (product == sv::OutputProduct::FinalFrame)
        {
            throw std::invalid_argument("unknown probe product");
        }
        const auto rendered = renderer.render(
            frames, config.view, &inspection,
            {false, false, false, !visibility && !disagreement, visibility, disagreement});
        const auto &mask = disagreement ? inspection.source_disagreement
                           : visibility ? inspection.source_visibility
                                        : inspection.support;
        std::ofstream file(argv[3], std::ios::binary);
        file.write(reinterpret_cast<const char *>(mask.data), mask.total() * mask.elemSize());
        file.close();
        if (!file)
        {
            throw std::runtime_error("cannot write native support oracle");
        }
        // Test/research provenance; no new product-side file-writing API.
        std::cout << boost::json::serialize(
                         boost::json::object{{"device", renderer.device()},
                                             {"output_product", sv::output_product_name(product)},
                                             {"frame_index", frame_index},
                                             {"inputs", inputs},
                                             {"width", config.width},
                                             {"height", config.height},
                                             {"config_sha256", sv::file_sha256(argv[1])},
                                             {"manifest_sha256", sv::file_sha256(argv[2])},
                                             {"payload_sha256", sv::file_sha256(argv[3])},
                                             {"source_fingerprint", sv::source_fingerprint}})
                  << '\n';
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
