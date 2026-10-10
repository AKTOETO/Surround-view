#pragma once
#include "sv/config.hpp"
#include <memory>
#include <stdexcept>

namespace sv
{
// Render-thread state machine. No network callbacks mutate this object.
class ExperimentLease
{
  public:

    enum class State
    {
        Idle,
        Active,
        Restoring,
        Failed
    };

    State state() const
    {
        return state_;
    }

    const std::string &id() const
    {
        return id_;
    }

    const std::string &owner() const
    {
        return owner_;
    }

    const Config &baseline() const
    {
        return *baseline_;
    }

    bool baseline_paused() const
    {
        return paused_;
    }

    bool acquire(const std::string &owner, std::shared_ptr<const Config> baseline, bool paused,
                 uint64_t now, uint64_t ttl_ms, std::string &error)
    {
        if (state_ != State::Idle)
        {
            error = "experiment_lease_busy";
            return false;
        }
        if (owner.empty() || !baseline)
        {
            error = "experiment_invalid_owner_or_baseline";
            return false;
        }
        if (ttl_ms < 250 || ttl_ms > 30000)
        {
            error = "experiment_ttl_out_of_range";
            return false;
        }
        owner_ = owner;
        id_ = std::to_string(++serial_);
        baseline_ = std::move(baseline);
        paused_ = paused;
        ttl_ns_ = ttl_ms * 1000000;
        deadline_ns_ = now + ttl_ns_;
        error_.clear();
        state_ = State::Active;
        error.clear();
        return true;
    }

    bool matches(const std::string &owner, const std::string &id) const
    {
        return state_ == State::Active && owner == owner_ && id == id_;
    }

    bool expired(uint64_t now) const
    {
        return state_ == State::Active && now >= deadline_ns_;
    }

    bool renew(const std::string &owner, const std::string &id, uint64_t now, std::string &error)
    {
        if (!matches(owner, id) || expired(now))
        {
            error = "experiment_lease_invalid_or_expired";
            return false;
        }
        deadline_ns_ = now + ttl_ns_;
        error.clear();
        return true;
    }

    void start_restore()
    {
        if (state_ == State::Active)
        {
            state_ = State::Restoring;
        }
    }

    void complete()
    {
        state_ = State::Idle;
        baseline_.reset();
        owner_.clear();
        id_.clear();
        deadline_ns_ = ttl_ns_ = 0;
        error_.clear();
    }

    void fail(std::string error)
    {
        state_ = State::Failed;
        error_ = std::move(error);
    }

    bool allows(const std::string &operation, const std::string &owner, const std::string &id,
                uint64_t now) const
    {
        if (operation == "state" || operation == "fusion_catalog" ||
            operation == "surface_catalog" || operation == "calibration_status")
        {
            return true;
        }
        if (state_ == State::Idle)
        {
            return id.empty(); // Never replay an expired experiment command as a normal mutation.
        }
        if (!matches(owner, id) || expired(now))
        {
            return false;
        }
        return operation == "configure_fusion" || operation == "configure_surface" ||
               operation == "pause" || operation == "resume" || operation == "experiment_renew" ||
               operation == "experiment_release";
    }

    boost::json::object status() const
    {
        const char *state = state_ == State::Idle        ? "idle"
                            : state_ == State::Active    ? "active"
                            : state_ == State::Restoring ? "restoring"
                                                         : "failed";
        return {{"state", state},
                {"lease_id", id_},
                {"deadline_monotonic_ns", std::to_string(deadline_ns_)},
                {"error", error_}};
    }

  private:

    State state_ = State::Idle;
    std::string owner_, id_, error_;
    std::shared_ptr<const Config> baseline_;
    uint64_t serial_ = 0, ttl_ns_ = 0, deadline_ns_ = 0;
    bool paused_ = false;
};
} // namespace sv
