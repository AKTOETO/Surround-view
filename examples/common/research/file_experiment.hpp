#pragma once
#include "scenario.hpp"
#include <filesystem>
#include <memory>

namespace sv::research
{
// Freeze scenario/reference inputs before connecting. Reserve output separately, before mutation.
class FileExperiment
{
  public:

    explicit FileExperiment(const std::filesystem::path &scenario);
    ~FileExperiment();
    FileExperiment(const FileExperiment &) = delete;
    FileExperiment &operator=(const FileExperiment &) = delete;

    size_t reference_bytes() const;
    size_t trial_count() const;
    bool resets_replay() const;
    const std::string &input_sha256() const;
    void reserve(const std::filesystem::path &report);
    boost::json::object execute(const client::Options &, const std::atomic_bool *cancel = nullptr,
                                std::function<void(unsigned, unsigned)> progress = {});

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Workflow failure/incomplete -> 4, completed quality rejection -> 5, otherwise -> 0.
int research_exit_code(const boost::json::object &);
std::string quality_status(const boost::json::object &);
} // namespace sv::research
