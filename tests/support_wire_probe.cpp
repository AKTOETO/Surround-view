// Test fixture only: native mask oracle for live server wire parity.
#include "sv/renderer.hpp"
#include "sv/vision.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4 && argc != 5)
        {
            throw std::invalid_argument("expected CONFIG MANIFEST OUTPUT [source_visibility_v1]");
        }
        const auto config = sv::load_config(argv[1]);
        const auto rows = sv::load_manifest(argv[2], config);
        sv::FrameSet frames;
        for (int camera = 0; camera < 4; ++camera)
        {
            auto image = std::make_shared<sv::Image>(sv::read_image(rows.at(0).paths[camera]));
            sv::Frame frame;
            frame.camera_id = camera;
            frame.image = std::move(image);
            frames.frames[camera] = std::move(frame);
        }
        sv::Renderer renderer(config);
        sv::RenderInspection inspection;
        const bool visibility = argc == 5;
        if (visibility && std::string(argv[4]) != "source_visibility_v1")
        {
            throw std::invalid_argument("unknown probe product");
        }
        renderer.render(frames, config.view, &inspection,
                        {false, false, false, !visibility, visibility});
        const auto &mask = visibility ? inspection.source_visibility : inspection.support;
        std::ofstream file(argv[3], std::ios::binary);
        file.write(reinterpret_cast<const char *>(mask.data), mask.total() * mask.elemSize());
        file.close();
        if (!file)
        {
            throw std::runtime_error("cannot write native support oracle");
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
