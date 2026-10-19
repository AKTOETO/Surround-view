#pragma once
#include <atomic>
#include <boost/json.hpp>
#include <filesystem>
#include <vector>

namespace sv::research
{
// Baseline is element0. Descriptive ranges of report summaries, never pooled frame quantiles/CI.
// Full pair guards apply; an aggregate is absent unless every report qualifies for that metric.
boost::json::object compare_report_series(const std::vector<boost::json::object> &,
                                          const std::atomic_bool *cancel = nullptr);
// 2..8 regular JSON reports, <=16MiB each and <=64MiB total input bytes. No embedded paths read.
boost::json::object compare_report_series_files(const std::vector<std::filesystem::path> &,
                                                const std::atomic_bool *cancel = nullptr);
} // namespace sv::research
