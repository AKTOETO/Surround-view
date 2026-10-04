#pragma once

#include "sv/config.hpp"
#include <string_view>

namespace sv
{
std::string sha256(std::string_view bytes);
std::string file_sha256(const std::filesystem::path &path);
boost::json::object distribution(const std::vector<double> &values);
boost::json::object system_info();
boost::json::array qualify_cpu(const Config &config, const std::filesystem::path &work);
void write_platform_report(const std::filesystem::path &directory,
                           const boost::json::object &report);
} // namespace sv
