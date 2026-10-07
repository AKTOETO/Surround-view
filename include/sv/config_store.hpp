#pragma once

#include "sv/config.hpp"
#include <atomic>
#include <memory>
#include <mutex>
#include <string>

namespace sv
{

class ConfigStore
{
public:
    explicit ConfigStore(const Config &initial_config)
        : active_(std::make_shared<Config>(initial_config)),
          persisted_(std::make_shared<Config>(initial_config)),
          revision_(0)
    {
    }

    std::shared_ptr<const Config> active() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    uint64_t revision() const
    {
        return revision_.load();
    }

    bool update(const Config &new_config, std::string &error_reason)
    {
        (void)error_reason;
        std::lock_guard<std::mutex> lock(mutex_);
        active_ = std::make_shared<Config>(new_config);
        revision_++;
        return true;
    }

    void persist()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        persisted_ = active_;
    }

    std::shared_ptr<const Config> persisted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return persisted_;
    }

private:
    mutable std::mutex mutex_;
    std::shared_ptr<const Config> active_;
    std::shared_ptr<const Config> persisted_;
    std::atomic<uint64_t> revision_;
};

} // namespace sv
