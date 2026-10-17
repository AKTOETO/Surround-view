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
} // namespace sv::research
