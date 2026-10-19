#pragma once
#include "scenario_bundle.hpp"

namespace sv::research
{
// Same server/recording. The bundle contains ordered client scenarios and their references.
boost::json::object export_batch_bundle(const std::filesystem::path &plan,
                                        const std::filesystem::path &new_directory,
                                        const std::atomic_bool *cancel = nullptr);

class BatchBundle
{
  public:

    explicit BatchBundle(const std::filesystem::path &, const std::atomic_bool *cancel = nullptr);
    ~BatchBundle();
    BatchBundle(const BatchBundle &) = delete;
    BatchBundle &operator=(const BatchBundle &) = delete;
    boost::json::object summary() const;
    boost::json::object execute(const std::filesystem::path &new_output, const client::Options &,
                                const std::atomic_bool *cancel = nullptr,
                                std::function<void(unsigned, unsigned, unsigned)> progress = {});

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace sv::research
