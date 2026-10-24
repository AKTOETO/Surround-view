#pragma once
#include "sv/client.hpp"
#include <filesystem>

namespace sv::research
{
// Sequential, one owner session; fits stay on the server and are never applied here.
boost::json::object run_calibration_cases(const std::filesystem::path &input,
                                          const std::filesystem::path &report,
                                          client::Options options);
} // namespace sv::research
