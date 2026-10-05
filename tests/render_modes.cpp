#include "sv/render_validation.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("config required");
        }
        auto config = sv::load_config(argv[1]);
        sv::qualify_fusion_modes(config);
        sv::qualify_enclosure_coverage(config);
        std::cout << "GPU fusion color/coverage oracles and 36 enclosure orbits passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
