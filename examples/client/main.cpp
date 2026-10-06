#include "sv/client.hpp"
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <mutex>
#include <stdexcept>

int main(int argc, char **argv)
{
    try
    {
        sv::client::Options options;
        unsigned frames = 3;
        bool exercise = false, allow_reconnect = false;
        for (int i = 1; i < argc; ++i)
        {
            const std::string arg = argv[i];
            auto next = [&]()
            {
                if (++i >= argc)
                {
                    throw std::runtime_error("missing option value");
                }
                return argv[i];
            };
            if (arg == "--unix")
            {
                options.endpoint.directory = next();
            }
            else if (arg == "--tcp")
            {
                options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
                options.endpoint.host = next();
                auto port = [&]()
                {
                    auto n = sv::parse_decimal_u64(next());
                    if (!n || n > 65535)
                    {
                        throw std::runtime_error("invalid port");
                    }
                    return uint16_t(n);
                };
                options.endpoint.control_port = port();
                options.endpoint.data_port = port();
            }
            else if (arg == "--frames")
            {
                auto n = sv::parse_decimal_u64(next());
                if (!n || n > 10000)
                {
                    throw std::runtime_error("invalid frame count");
                }
                frames = n;
            }
            else if (arg == "--allow-reconnect")
            {
                allow_reconnect = true;
            }
            else if (arg == "--exercise")
            {
                exercise = true;
            }
            else
            {
                throw std::runtime_error("usage: sv-client-probe --unix DIR|--tcp HOST "
                                         "CONTROL_PORT DATA_PORT [--frames N --exercise]");
            }
        }
        std::mutex mutex;
        std::condition_variable cv;
        std::deque<sv::client::Event> queue;
        bool overflow = false;
        sv::client::Client client(options,
                                  [&](sv::client::Event event)
                                  {
                                      std::lock_guard<std::mutex> lock(mutex);
                                      if (queue.size() >= 64)
                                      {
                                          overflow = true;
                                      }
                                      else
                                      {
                                          queue.push_back(std::move(event));
                                      }
                                      cv.notify_one();
                                  });
        unsigned received = 0, acknowledgements = 0;
        const unsigned expected_acks = exercise ? 5 : 1;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (received < frames || acknowledgements < expected_acks)
        {
            sv::client::Event event;
            {
                std::unique_lock<std::mutex> lock(mutex);
                if (!cv.wait_until(lock, deadline, [&] { return !queue.empty() || overflow; }))
                {
                    throw std::runtime_error("probe timeout");
                }
                if (overflow)
                {
                    throw std::runtime_error("consumer queue full");
                }
                event = std::move(queue.front());
                queue.pop_front();
            }
            if (event.kind == sv::client::Event::Kind::Error)
            {
                if (!allow_reconnect)
                {
                    throw std::runtime_error(event.detail);
                }
                std::cout << boost::json::serialize(
                                 boost::json::object{{"client_error", event.detail}})
                          << std::endl;
                continue;
            }
            if (event.kind == sv::client::Event::Kind::State)
            {
                if (allow_reconnect)
                {
                    std::cout << boost::json::serialize(
                                     boost::json::object{{"client_state", event.detail}})
                              << std::endl;
                }
                if (event.detail == "retry_exhausted")
                {
                    throw std::runtime_error(event.detail);
                }
                if (event.detail == "ready")
                {
                    client.state();
                    if (exercise)
                    {
                        client.pause();
                        client.preset("top");
                        client.step();
                        client.command("unknown_test_command");
                    }
                }
                continue;
            }
            if (!event.message)
            {
                continue;
            }
            auto header = event.message->header;
            header["message_type"] = event.message->type;
            if (event.message->type == 11)
            {
                // Consumer retains an owning immutable Message; release does not invalidate it.
                uint64_t hash = 14695981039346656037ULL;
                for (auto byte : event.message->payload)
                {
                    hash ^= byte;
                    hash *= 1099511628211ULL;
                }
                header["rgba_fnv1a64"] = std::to_string(hash);
                client.release(event.message->header);
                ++received;
                if (exercise && received < frames)
                {
                    client.step();
                }
            }
            else if (event.message->type == 21)
            {
                ++acknowledgements;
            }
            std::cout << boost::json::serialize(header) << std::endl;
        }
        client.stop();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
