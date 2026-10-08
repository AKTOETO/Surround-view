#pragma once

#include "sv/interfaces.hpp"
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>

namespace sv
{

class ConfigStore : public IConfigStore
{
  public:

    explicit ConfigStore(const Config &initial_config, std::filesystem::path persistence_path = {})
        : active_(std::make_shared<Config>(initial_config)),
          persisted_(std::make_shared<Config>(initial_config)),
          persistence_path_(std::move(persistence_path)), revision_(0)
    {
    }

    std::shared_ptr<const Config> active() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return active_;
    }

    uint64_t revision() const override
    {
        return revision_.load();
    }

    std::pair<std::shared_ptr<const Config>, uint64_t> snapshot() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {active_, revision_.load()};
    }

    bool update(const Config &new_config, std::string &error_reason) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return update_locked(new_config, error_reason);
    }

    bool update_if_revision(const Config &new_config, uint64_t expected_revision,
                            std::string &error_reason)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (revision_.load() != expected_revision)
        {
            error_reason = "stale_config_revision";
            return false;
        }
        return update_locked(new_config, error_reason);
    }

    void persist() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!persistence_path_.empty())
        {
            write_atomic(active_->effective);
        }
        persisted_ = active_;
    }

    std::shared_ptr<const Config> persisted() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return persisted_;
    }

  private:

    bool update_locked(const Config &new_config, std::string &error_reason)
    {
        try
        {
            auto updated = std::make_shared<Config>(new_config);
            if (!persistence_path_.empty())
            {
                if (!updated->effective.is_object())
                {
                    throw std::invalid_argument(
                        "updated config has no serializable effective JSON");
                }
                (void)parse_config(updated->effective);
                write_atomic(updated->effective);
            }
            active_ = std::move(updated);
            if (!persistence_path_.empty())
            {
                persisted_ = active_;
            }
            revision_++;
            error_reason.clear();
            return true;
        }
        catch (const std::exception &error)
        {
            error_reason = error.what();
            return false;
        }
    }

    void write_atomic(const boost::json::value &value) const
    {
        if (!persistence_path_.parent_path().empty())
        {
            std::filesystem::create_directories(persistence_path_.parent_path());
        }
        auto temporary = persistence_path_;
        temporary += ".tmp";
        try
        {
            std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
            if (!stream)
            {
                throw std::runtime_error("cannot open temporary config file " + temporary.string());
            }
            stream << boost::json::serialize(value) << '\n';
            stream.flush();
            if (!stream)
            {
                throw std::runtime_error("cannot write temporary config file " +
                                         temporary.string());
            }
            stream.close();
            std::filesystem::rename(temporary, persistence_path_);
        }
        catch (...)
        {
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            throw;
        }
    }

    mutable std::mutex mutex_;
    std::shared_ptr<const Config> active_;
    std::shared_ptr<const Config> persisted_;
    std::filesystem::path persistence_path_;
    std::atomic<uint64_t> revision_;
};

} // namespace sv
