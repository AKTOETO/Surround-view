#include "sv/client.hpp"
#include <algorithm>
#include <boost/asio.hpp>
#include <condition_variable>
#include <deque>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace
{
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

// Preserve coalesced messages: a single TCP read can contain several commands.
class Reader
{
    sv::Decoder decoder_;
    std::deque<sv::Message> messages_;

  public:

    sv::Message receive(Tcp::socket &socket)
    {
        socket.non_blocking(true);
        std::array<unsigned char, 8192> bytes{};
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        while (messages_.empty())
        {
            if (Clock::now() >= deadline)
            {
                throw std::runtime_error("mock receive deadline");
            }
            boost::system::error_code error;
            const auto count = socket.read_some(asio::buffer(bytes), error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (error)
            {
                throw boost::system::system_error(error);
            }
            for (auto &message : decoder_.feed(bytes.data(), count))
            {
                messages_.push_back(std::move(message));
            }
        }
        auto message = std::move(messages_.front());
        messages_.pop_front();
        return message;
    }
};

void send(Tcp::socket &socket, const sv::Message &message)
{
    socket.non_blocking(false);
    const auto bytes = sv::encode(message);
    asio::write(socket, asio::buffer(bytes));
}

TEST(ClientCommandOrder, ConcurrentProducersReachWireInIncreasingIdOrder)
{
    constexpr unsigned producers = 8, per_producer = 32, total = producers * per_producer;
    asio::io_context io;
    Tcp::acceptor control(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    Tcp::acceptor data(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    sv::client::Options options;
    options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    options.endpoint.control_port = control.local_endpoint().port();
    options.endpoint.data_port = data.local_endpoint().port();
    options.command_capacity = 512;
    options.timeout_ms = 5000;
    options.max_retries = 0;
    std::mutex mutex;
    std::condition_variable changed;
    bool ready = false, start = false, finish = false;
    unsigned acknowledgements = 0;
    std::vector<std::string> errors;

    auto server = std::async(
        std::launch::async,
        [&]
        {
            Tcp::socket ctl(io), video(io);
            Reader commands, frames;
            control.accept(ctl);
            commands.receive(ctl);
            send(
                ctl,
                {2,
                 {{"session_id", "s"}, {"data_token", "t"}, {"capabilities", boost::json::array{}}},
                 {}});
            data.accept(video);
            frames.receive(video);
            send(video, {2, {{"session_id", "s"}}, {}});
            std::vector<uint64_t> ids;
            uint64_t last = 0;
            for (unsigned index = 0; index < total; ++index)
            {
                const auto message = commands.receive(ctl);
                const auto id =
                    sv::parse_decimal_u64(std::string(message.header.at("command_id").as_string()));
                ids.push_back(id);
                const bool accepted = id > last;
                last = std::max(last, id);
                send(ctl, {21,
                           {{"command_id", std::to_string(id)},
                            {"accepted", accepted},
                            {"reason", accepted ? "ok" : "duplicate_or_out_of_order"}},
                           {}});
            }
            // Do not race socket teardown against delivery of the final ACK callback.
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait_for(lock, std::chrono::seconds(6), [&] { return finish; });
            return ids;
        });
    sv::client::Client client(
        options,
        [&](sv::client::Event event)
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (event.kind == sv::client::Event::Kind::State && event.detail == "ready")
            {
                ready = true;
            }
            if (event.kind == sv::client::Event::Kind::Error)
            {
                errors.push_back(event.detail);
            }
            if (event.message && event.message->type == 21)
            {
                ++acknowledgements;
                if (!event.message->header.at("accepted").as_bool())
                {
                    errors.push_back(std::string(event.message->header.at("reason").as_string()));
                }
            }
            changed.notify_all();
        });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds(3), [&] { return ready; }));
    }
    std::vector<std::thread> threads;
    for (unsigned producer = 0; producer < producers; ++producer)
    {
        threads.emplace_back(
            [&, producer]
            {
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    changed.wait(lock, [&] { return start; });
                }
                try
                {
                    for (unsigned index = 0; index < per_producer; ++index)
                    {
                        // Unequal encoding work exposes allocation/post reordering.
                        client.command("state",
                                       {{"padding", std::string(producer % 2 ? 0 : 48000, 'x')}});
                    }
                }
                catch (const std::exception &error)
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    errors.push_back(error.what());
                }
            });
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        start = true;
    }
    changed.notify_all();
    for (auto &thread : threads)
    {
        thread.join();
    }
    {
        std::unique_lock<std::mutex> lock(mutex);
        EXPECT_TRUE(changed.wait_for(lock, std::chrono::seconds(6),
                                     [&] { return acknowledgements == total; }));
        EXPECT_TRUE(errors.empty()) << (errors.empty() ? "" : errors.front());
        finish = true;
    }
    changed.notify_all();
    ASSERT_EQ(server.wait_for(std::chrono::seconds(6)), std::future_status::ready);
    const auto ids = server.get();
    ASSERT_EQ(ids.size(), total);
    for (unsigned index = 0; index < total; ++index)
    {
        ASSERT_EQ(ids[index], index + 1) << "wire position " << index;
    }
    client.stop();
}
} // namespace
