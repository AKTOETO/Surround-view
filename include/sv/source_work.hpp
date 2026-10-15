#pragma once
#include "sv/source_work_timing.hpp"
#include <atomic>
#include <memory>

namespace sv
{
struct SourceTimingSession
{
    uint64_t epoch;
};

struct SourceWorkRecord
{
    std::shared_ptr<const SourceTimingSession> session;
    SourceWorkTiming timing;
};

// Pointer identity guards work begun before a hot config commit. Null means no timing.
class SourceTimingGate
{
    std::shared_ptr<const SourceTimingSession> session_;

  public:

    void set(std::shared_ptr<const SourceTimingSession> session) noexcept
    {
        std::atomic_store_explicit(&session_, std::move(session), std::memory_order_release);
    }

    std::shared_ptr<const SourceTimingSession> get() const noexcept
    {
        return std::atomic_load_explicit(&session_, std::memory_order_acquire);
    }
};
} // namespace sv
