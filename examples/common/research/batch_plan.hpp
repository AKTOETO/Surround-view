#pragma once
#include <boost/json.hpp>
#include <filesystem>
#include <string>
#include <string_view>

namespace sv::research
{
// Structure only; does not open scenario/reference paths or contact a server.
boost::json::object parse_batch_plan(std::string_view);
// Bounded regular-file input; exact bytes retained for the execution digest.
std::string read_batch_plan(const std::filesystem::path &);
} // namespace sv::research
