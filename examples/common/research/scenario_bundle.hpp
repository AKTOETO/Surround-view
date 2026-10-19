#pragma once
#include "file_experiment.hpp"

namespace sv::research
{
// A directory bundle contains only client scenario/reference inputs, not server datasets/config.
boost::json::object export_scenario_bundle(const std::filesystem::path &scenario,
                                           const std::filesystem::path &new_directory,
                                           const std::atomic_bool *cancel = nullptr);

// Exact input bytes; relative references resolve against the explicit base, no source-file shim.
boost::json::object export_scenario_bundle(std::string_view scenario,
                                           const std::filesystem::path &reference_base,
                                           const std::filesystem::path &new_directory,
                                           const std::atomic_bool *cancel = nullptr);

// Constructor verifies and freezes all inputs offline. execute never reopens the source bundle.
class ScenarioBundle
{
  public:

    explicit ScenarioBundle(const std::filesystem::path &,
                            const std::atomic_bool *cancel = nullptr);
    ~ScenarioBundle();
    ScenarioBundle(const ScenarioBundle &) = delete;
    ScenarioBundle &operator=(const ScenarioBundle &) = delete;
    boost::json::object summary() const;
    boost::json::object execute(const std::filesystem::path &new_report, const client::Options &,
                                const std::atomic_bool *cancel = nullptr,
                                std::function<void(unsigned, unsigned)> progress = {});

  private:

    friend class BatchBundle;
    FileExperiment &experiment();
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sv::research
