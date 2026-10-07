#include "sv/config.hpp"
#include <boost/json.hpp>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        std::cerr << "usage: sv-camera-source-tests CONFIG.json\n";
        return 2;
    }

    try
    {
        std::ifstream input(argv[1]);
        if (!input)
        {
            std::cerr << "cannot read camera config: " << argv[1] << '\n';
            return 1;
        }
        const auto config = sv::parse_config(boost::json::parse(input));
        if (config.source.type != "camera")
        {
            std::cerr << "expected source.type=camera\n";
            return 1;
        }
        for (int camera_id = 0; camera_id < 4; ++camera_id)
        {
            const auto &path = config.source.camera_devices[camera_id];
            if (path.empty() || path.front() != '/')
            {
                std::cerr << "camera device path must be absolute, id=" << camera_id << '\n';
                return 1;
            }
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << "camera config rejected: " << error.what() << '\n';
        return 1;
    }

    std::cout << "V4L2 camera configuration parsed (no physical capture asserted).\n";
    return 0;
}
