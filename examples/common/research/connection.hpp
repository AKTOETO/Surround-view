#pragma once
#include "sv/client.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>

namespace sv::research
{
// One session, no automatic reconnect/replay of mutations.
class Connection
{
  public:

    explicit Connection(client::Options);
    boost::json::object request(std::string, boost::json::object = {});
    std::shared_ptr<const Message> frame(const std::string &state_revision);
    bool supports(std::string_view capability) const;

  private:

    using Clock = std::chrono::steady_clock;
    client::Event next(Clock::time_point deadline);
    void enqueue(client::Event);
    void retain_frame(const std::shared_ptr<const Message> &);
    std::mutex mutex_;
    std::condition_variable changed_;
    std::deque<client::Event> events_;
    std::deque<std::shared_ptr<const Message>> frames_;
    unsigned timeout_ms_;
    bool overflow_ = false;
    boost::json::array capabilities_;
    // Must be destroyed before callback state above.
    client::Client client_;
};
} // namespace sv::research
