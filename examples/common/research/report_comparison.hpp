#pragma once
#include <atomic>
#include <boost/json.hpp>
#include <filesystem>

namespace sv::research
{
// Reports are claims, not attestation. Never follows embedded paths or loads image payloads.
boost::json::object compare_reports(const boost::json::object &, const boost::json::object &,
                                    const std::atomic_bool *cancel = nullptr);
boost::json::object compare_report_files(const std::filesystem::path &,
                                         const std::filesystem::path &,
                                         const std::atomic_bool *cancel = nullptr);
} // namespace sv::research
