#include "options.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>

namespace sv::ctl
{
int execute(const Options &options)
{
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<client::Event> events;
    bool overflow = false;
    client::Options connection;
    connection.endpoint = options.endpoint;
    connection.timeout_ms = options.timeout_ms;
    // A one-shot mutation must never be replayed after losing its session.
    connection.max_retries = 0;
    client::Client client(connection,
                          [&](client::Event event)
                          {
                              std::lock_guard<std::mutex> lock(mutex);
                              if (events.size() == 64)
                              {
                                  overflow = true;
                              }
                              else
                              {
                                  events.push_back(std::move(event));
                              }
                              changed.notify_one();
                          });
    uint64_t command_id = 0;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeout_ms);
    while (true)
    {
        client::Event event;
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!changed.wait_until(lock, deadline, [&] { return overflow || !events.empty(); }))
            {
                throw std::runtime_error("command timeout; outcome may be unknown if already sent");
            }
            if (overflow)
            {
                throw std::runtime_error("consumer event queue overflow");
            }
            event = std::move(events.front());
            events.pop_front();
        }
        if (event.kind == client::Event::Kind::Error)
        {
            throw std::runtime_error(event.detail);
        }
        if (event.kind == client::Event::Kind::State)
        {
            if (event.detail == "ready" && !command_id)
            {
                command_id = client.command(options.request.operation, options.request.parameters);
            }
            else if (event.detail == "retry_exhausted")
            {
                throw std::runtime_error(event.detail);
            }
            continue;
        }
        if (!event.message)
        {
            continue;
        }
        const auto &message = *event.message;
        if (message.type == 11)
        {
            // Legacy protocol always opens data. Drain/release until control-only is implemented.
            client.release(message.header);
        }
        else if (message.type == 21 && command_id &&
                 message.header.at("command_id").as_string() == std::to_string(command_id))
        {
            const bool accepted = message.header.at("accepted").as_bool();
            std::cout << boost::json::serialize(message.header) << '\n';
            client.stop();
            return accepted ? 0 : 4;
        }
    }
}
} // namespace sv::ctl
