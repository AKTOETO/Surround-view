#pragma once
#include <boost/system/system_error.hpp>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace sv
{
// All operations run on the owning IO thread (or after it is joined).
// Timer follows the Asio expires_after/async_wait/cancel() interface.
template <typename Timer> class MessageDeadline
{
    struct State
    {
        uint64_t generation = 0;
        bool armed = false;
    };

    Timer timer_;
    std::shared_ptr<State> state_ = std::make_shared<State>();

  public:

    template <typename Context> explicit MessageDeadline(Context &context) : timer_(context)
    {
    }

    MessageDeadline(const MessageDeadline &) = delete;
    MessageDeadline &operator=(const MessageDeadline &) = delete;

    ~MessageDeadline()
    {
        cancel();
    }

    template <typename Duration, typename Callback> void arm(Duration duration, Callback callback)
    {
        if (state_->generation == std::numeric_limits<uint64_t>::max())
        {
            cancel();
            throw std::overflow_error("message deadline generation exhausted");
        }
        const auto generation = ++state_->generation;
        state_->armed = true;
        try
        {
            timer_.expires_after(duration);
            timer_.async_wait(
                [state = state_, generation, callback = std::move(callback)](auto error)
                {
                    // An expired handler can already be queued with success when cancel is called.
                    if (!state->armed || state->generation != generation)
                    {
                        return;
                    }
                    state->armed = false;
                    if (!error)
                    {
                        callback();
                    }
                });
        }
        catch (...)
        {
            cancel();
            throw;
        }
    }

    void cancel() noexcept
    {
        state_->armed = false;
        try
        {
            timer_.cancel();
        }
        catch (const boost::system::system_error &)
        {
            // Logical cancellation still makes already-ready callbacks inert.
        }
    }
};
} // namespace sv
