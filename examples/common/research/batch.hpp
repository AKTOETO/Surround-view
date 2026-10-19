#pragma once
#include "scenario.hpp"
#include <filesystem>

namespace sv::research
{
// Same live server/recording, ordered scenarios; each member must explicitly reset replay.
// All inputs and output paths are prepared before any member connects. No server process launch.
boost::json::object run_batch(const std::filesystem::path &plan,
                              const std::filesystem::path &new_output_directory,
                              const client::Options &, const std::atomic_bool *cancel = nullptr,
                              std::function<void(unsigned, unsigned, unsigned)> progress = {});
// A compiled/UI plan: paths or schema2 inline scenarios; saves inputs/plan.json.
// Relative inline references resolve against the supplied plan base.
boost::json::object run_batch(const boost::json::object &plan,
                              const std::filesystem::path &scenario_base_directory,
                              const std::filesystem::path &new_output_directory,
                              const client::Options &, const std::atomic_bool *cancel = nullptr,
                              std::function<void(unsigned, unsigned, unsigned)> progress = {});
} // namespace sv::research
