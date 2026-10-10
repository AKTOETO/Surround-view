#include "connection.hpp"
#include <chrono>
#include <stdexcept>

namespace sv::research
{
namespace
{
client::Options without_reconnect(client::Options options)
{
    options.max_retries = 0;
    return options;
}
} // namespace

Connection::Connection(client::Options options)
    : timeout_ms_(options.timeout_ms),
      client_(without_reconnect(options),
              [this](client::Event event) { enqueue(std::move(event)); })
{
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
    while (true)
    {
        auto event = next(deadline);
        if (event.kind == client::Event::Kind::State && event.detail == "ready")
        {
            break;
        }
        if (event.message && event.message->type == 11)
        {
            retain_frame(event.message);
        }
    }
}

void Connection::enqueue(client::Event event)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (events_.size() == 64)
    {
        overflow_ = true;
    }
    else
    {
        events_.push_back(std::move(event));
    }
    changed_.notify_one();
}

client::Event Connection::next(Clock::time_point deadline)
{
    std::unique_lock<std::mutex> lock(mutex_);
    if (Clock::now() >= deadline ||
        !changed_.wait_until(lock, deadline, [&] { return overflow_ || !events_.empty(); }))
    {
        throw std::runtime_error("research connection timeout; mutation outcome may be unknown");
    }
    if (overflow_)
    {
        throw std::runtime_error("research event queue overflow");
    }
    auto event = std::move(events_.front());
    events_.pop_front();
    if (event.kind == client::Event::Kind::Error ||
        (event.kind == client::Event::Kind::State && event.detail == "retry_exhausted"))
    {
        throw std::runtime_error(event.detail);
    }
    return event;
}

void Connection::retain_frame(const std::shared_ptr<const Message> &message)
{
    client_.release(message->header);
    if (frames_.size() == 8)
    {
        frames_.pop_front();
    }
    frames_.push_back(message);
}

boost::json::object Connection::request(std::string operation, boost::json::object parameters)
{
    const auto id = std::to_string(client_.command(std::move(operation), std::move(parameters)));
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
    while (true)
    {
        auto event = next(deadline);
        if (!event.message)
        {
            continue;
        }
        if (event.message->type == 11)
        {
            retain_frame(event.message);
        }
        if (event.message->type == 21 && event.message->header.at("command_id").as_string() == id)
        {
            return event.message->header;
        }
    }
}

std::shared_ptr<const Message> Connection::frame(const std::string &state_revision)
{
    const auto revision = parse_decimal_u64(state_revision);
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms_);
    while (true)
    {
        while (!frames_.empty())
        {
            auto frame = std::move(frames_.front());
            frames_.pop_front();
            const auto actual =
                parse_decimal_u64(std::string(frame->header.at("state_revision").as_string()));
            if (actual == revision)
            {
                return frame;
            }
            if (actual > revision)
            {
                throw std::runtime_error("unexpected concurrent state change");
            }
        }
        auto event = next(deadline);
        if (event.message && event.message->type == 11)
        {
            retain_frame(event.message);
        }
    }
}
} // namespace sv::research
