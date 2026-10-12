// Test fixture only: native mask oracle for live server wire parity.
#include "sv/renderer.hpp"
#include "sv/vision.hpp"
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 4)
        {
            throw std::invalid_argument("expected CONFIG MANIFEST OUTPUT");
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
        renderer.render(frames, config.view, &inspection, {false, false, false, true});
        std::ofstream file(argv[3], std::ios::binary);
        file.write(reinterpret_cast<const char *>(inspection.support.data),
                   inspection.support.total() * inspection.support.elemSize());
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
