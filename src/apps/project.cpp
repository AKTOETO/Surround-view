#include "sv/config.hpp"
#include "sv/vision.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    try
    {
        if (argc != 2)
        {
            throw std::runtime_error("usage: sv-project CONFIG < point-list.json");
        }
        auto c = sv::load_config(argv[1]);
        std::string input((std::istreambuf_iterator<char>(std::cin)), {});
        auto v = boost::json::parse(input);
        boost::json::array result;
        std::array<std::vector<sv::Vec3>, 4> points;
        std::vector<std::pair<int, size_t>> order;
        for (const auto &point : v.as_array())
        {
            const auto &a = point.as_array();
            if (a.size() != 4)
            {
                throw std::runtime_error("expected [camera_id,x,y,z]");
            }
            int id = static_cast<int>(a[0].as_int64());
            auto num = [](const boost::json::value &n)
            { return n.is_double() ? n.as_double() : static_cast<double>(n.as_int64()); };
            c.cameras.at(id);
            order.emplace_back(id, points[id].size());
            points[id].push_back({num(a[1]), num(a[2]), num(a[3])});
        }
        std::array<std::vector<sv::Pixel>, 4> pixels;
        for (int i = 0; i < 4; ++i)
        {
            pixels[i] = sv::project_opencv(c.cameras[i], points[i]);
        }
        for (const auto &[id, index] : order)
        {
            const auto p = pixels[id][index];
            result.push_back(boost::json::object{{"u", p.u}, {"v", p.v}, {"valid", p.valid}});
        }
        std::cout << boost::json::serialize(result) << '\n';
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
