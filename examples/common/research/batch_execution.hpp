#pragma once
#include "file_experiment.hpp"
#include <vector>

namespace sv::research
{
// Internal executor shared by ordinary batch and verified batch bundles.
// Callers own frozen members for the entire execution. All reports reserved before connection.
boost::json::object execute_prepared_batch(
    std::string_view plan, const std::vector<FileExperiment *> &experiments,
    const std::filesystem::path &new_output, const client::Options &,
    const std::atomic_bool *cancel, std::function<void(unsigned, unsigned, unsigned)> progress,
    bool snapshot, const boost::json::value &bundle_identity = nullptr);
} // namespace sv::research
