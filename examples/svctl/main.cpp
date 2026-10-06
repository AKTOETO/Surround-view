#include "options.hpp"
#include <iostream>

int main(int argc, char **argv)
{
    sv::ctl::Options options;
    try
    {
        options = sv::ctl::parse(std::vector<std::string>(argv + 1, argv + argc));
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n' << sv::ctl::usage();
        return 2;
    }
    if (options.help)
    {
        std::cout << sv::ctl::usage();
        return 0;
    }
    try
    {
        return sv::ctl::execute(options);
    }
    catch (const std::exception &error)
    {
        std::cerr << boost::json::serialize(boost::json::object{{"error", error.what()}}) << '\n';
        return 3;
    }
}
