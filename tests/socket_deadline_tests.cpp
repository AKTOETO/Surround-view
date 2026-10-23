#include "socket_message_deadline.hpp"
#include <boost/asio/error.hpp>
#include <chrono>
#include <deque>
#include <functional>
#include <gtest/gtest.h>

namespace
{
// Emulates the important Asio race: cancel cannot change an already-ready success callback.
struct TimerQueue
{
    using Handler = std::function<void(boost::system::error_code)>;
    Handler pending;
    std::deque<std::pair<Handler, boost::system::error_code>> ready;
    bool fail_arm = false, fail_cancel = false;

    void expire()
    {
        if (pending)
        {
            ready.emplace_back(std::move(pending), boost::system::error_code{});
        }
        pending = {};
    }

    void dispatch()
    {
        while (!ready.empty())
        {
            auto event = std::move(ready.front());
            ready.pop_front();
            event.first(event.second);
        }
    }
};

class QueuedTimer
{
    TimerQueue &queue_;

  public:

    explicit QueuedTimer(TimerQueue &queue) : queue_(queue)
    {
    }

    void cancel()
    {
        if (queue_.fail_cancel)
        {
            throw boost::system::system_error(boost::asio::error::operation_aborted);
        }
        if (queue_.pending)
        {
            queue_.ready.emplace_back(std::move(queue_.pending),
                                      boost::asio::error::operation_aborted);
        }
        queue_.pending = {};
    }

    void expires_after(std::chrono::milliseconds)
    {
        if (queue_.fail_arm)
        {
            throw std::runtime_error("mock timer setup failed");
        }
        cancel();
    }

    template <typename Handler> void async_wait(Handler handler)
    {
        queue_.pending = std::move(handler);
    }
};
} // namespace

TEST(SocketDeadline, CompletedMessageSuppressesAlreadyReadySuccessfulTimeout)
{
    TimerQueue queue;
    sv::SocketMessageDeadline<QueuedTimer> deadline(queue);
    unsigned closes = 0;
    deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; });
    queue.expire();    // Kernel timeout is ready, handler has not run.
    deadline.cancel(); // Read completion handled the full message first.
    queue.dispatch();
    EXPECT_EQ(closes, 0U);
}

TEST(SocketDeadline, OldReadyAndAbortedCallbacksCannotCloseOrDisarmNewMessage)
{
    for (const bool expired : {false, true})
    {
        TimerQueue queue;
        sv::SocketMessageDeadline<QueuedTimer> deadline(queue);
        unsigned old_closes = 0, current_closes = 0;
        deadline.arm(std::chrono::milliseconds(10), [&] { ++old_closes; });
        if (expired)
        {
            queue.expire();
        }
        deadline.arm(std::chrono::milliseconds(20), [&] { ++current_closes; });
        queue.dispatch();
        EXPECT_EQ(old_closes, 0U);
        EXPECT_EQ(current_closes, 0U);
        queue.expire();
        queue.dispatch();
        EXPECT_EQ(current_closes, 1U);
    }
}

TEST(SocketDeadline, DestructionAndFailedRearmInvalidateQueuedCallbacks)
{
    TimerQueue queue;
    unsigned closes = 0;
    {
        sv::SocketMessageDeadline<QueuedTimer> deadline(queue);
        deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; });
        queue.expire();
    }
    queue.dispatch();
    EXPECT_EQ(closes, 0U);
    {
        sv::SocketMessageDeadline<QueuedTimer> deadline(queue);
        deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; });
        queue.expire();
        queue.fail_arm = true;
        EXPECT_ANY_THROW(deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; }));
        queue.dispatch();
        EXPECT_EQ(closes, 0U);
        queue.fail_arm = false;
        deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; });
        queue.expire();
        queue.dispatch();
        EXPECT_EQ(closes, 1U);
    }
}

TEST(SocketDeadline, NativeCancelFailureStillSuppressesThePendingCallback)
{
    TimerQueue queue;
    sv::SocketMessageDeadline<QueuedTimer> deadline(queue);
    unsigned closes = 0;
    deadline.arm(std::chrono::milliseconds(10), [&] { ++closes; });
    queue.fail_cancel = true;
    EXPECT_NO_THROW(deadline.cancel());
    queue.expire();
    queue.dispatch();
    EXPECT_EQ(closes, 0U);
    queue.fail_cancel = false;
}
