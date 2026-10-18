#pragma once
#include "scenario.hpp"
#include <filesystem>
#include <memory>
#include <string_view>

namespace sv::research
{
// Freeze scenario/reference inputs before connecting. Reserve output separately, before mutation.
class FileExperiment
{
  public:

    explicit FileExperiment(const std::filesystem::path &scenario);
    // Exact UTF-8 JSON bytes; relative reference paths resolve against the explicit base.
    FileExperiment(std::string_view scenario_text, const std::filesystem::path &reference_base);
    ~FileExperiment();
    FileExperiment(const FileExperiment &) = delete;
    FileExperiment &operator=(const FileExperiment &) = delete;

    size_t reference_bytes() const;
    boost::json::value reference_description() const;
    size_t trial_count() const;
    bool resets_replay() const;
    const std::string &input_sha256() const;
    void reserve(const std::filesystem::path &report);
    boost::json::object execute(const client::Options &, const std::atomic_bool *cancel = nullptr,
                                std::function<void(unsigned, unsigned)> progress = {});

  private:

    friend class ScenarioBundle;
    void set_bundle_identity(const std::string &manifest_sha256, const std::string &source_sha256);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Workflow failure/incomplete -> 4, completed quality rejection -> 5, otherwise -> 0.
int research_exit_code(const boost::json::object &);
std::string quality_status(const boost::json::object &);
} // namespace sv::research
