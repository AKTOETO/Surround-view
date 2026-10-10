#include "sv/client.hpp"
#include <boost/asio.hpp>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <mutex>
#include <thread>

namespace
{
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;
using Clock = std::chrono::steady_clock;

sv::Message receive(Tcp::socket &socket)
{
    socket.non_blocking(true);
    sv::Decoder decoder;
    std::array<unsigned char, 8192> bytes{};
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline)
    {
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
        auto messages = decoder.feed(bytes.data(), count);
        if (!messages.empty())
        {
            return std::move(messages.front());
        }
    }
    throw std::runtime_error("mock receive deadline");
}

void send(Tcp::socket &socket, const sv::Message &message)
{
    socket.non_blocking(false);
    const auto bytes = sv::encode(message);
    asio::write(socket, asio::buffer(bytes));
}

TEST(ClientRelease, CommandSaturationDoesNotConsumeReleaseBudget)
{
    asio::io_context io;
    Tcp::acceptor control(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    Tcp::acceptor data(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    sv::client::Options options;
    options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    options.endpoint.control_port = control.local_endpoint().port();
    options.endpoint.data_port = data.local_endpoint().port();
    options.command_capacity = 1;
    options.timeout_ms = 2000;
    options.max_retries = 0;
    const boost::json::object frame{{"session_id", "session"},
                                    {"frame_id", "1"},
                                    {"frame_set_id", "1"},
                                    {"state_revision", "0"},
                                    {"buffer_token", "buffer"},
                                    {"pixel_format", "RGBA8"},
                                    {"row_origin", "top_left"},
                                    {"width", 2},
                                    {"height", 2},
                                    {"stride_bytes", 8}};

    auto server = std::async(
        std::launch::async,
        [&]
        {
            Tcp::socket ctl(io), video(io);
            control.accept(ctl);
            receive(ctl);
            send(ctl, {2,
                       {{"session_id", "session"},
                        {"data_token", "token"},
                        {"capabilities", boost::json::array{}}},
                       {}});
            data.accept(video);
            receive(video);
            send(video, {2, {{"session_id", "session"}}, {}});
            send(video, {11, frame, std::vector<unsigned char>(16, 127)});
            const auto command = receive(ctl);
            const auto released = receive(video);
            send(ctl,
                 {21, {{"command_id", command.header.at("command_id")}, {"accepted", true}}, {}});
            return std::make_pair(command, released);
        });

    std::mutex mutex;
    std::condition_variable changed;
    bool delivered = false, resume = false;
    sv::client::Client client(
        options,
        [&](sv::client::Event event)
        {
            if (event.kind == sv::client::Event::Kind::Message && event.message->type == 11)
            {
                std::unique_lock<std::mutex> lock(mutex);
                delivered = true;
                changed.notify_all();
                // Hold the worker so queue saturation is deterministic. The deadline
                // keeps cleanup bounded even if an assertion fails on the owner thread.
                changed.wait_for(lock, std::chrono::seconds(3), [&] { return resume; });
            }
        });
    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(changed.wait_for(lock, std::chrono::seconds(2), [&] { return delivered; }));
    }
    const auto id = client.state();
    EXPECT_THROW(client.state(), std::runtime_error);
    EXPECT_NO_THROW(client.release(frame));
    // The independent release queue is bounded too.
    EXPECT_THROW(client.release(frame), std::runtime_error);
    {
        std::lock_guard<std::mutex> lock(mutex);
        resume = true;
    }
    changed.notify_all();
    ASSERT_EQ(server.wait_for(std::chrono::seconds(4)), std::future_status::ready);
    const auto messages = server.get();
    EXPECT_EQ(messages.first.type, 20);
    EXPECT_EQ(std::string(messages.first.header.at("command_id").as_string()), std::to_string(id));
    EXPECT_EQ(messages.second.type, 22);
    EXPECT_EQ(messages.second.header.at("session_id"), "session");
    EXPECT_EQ(messages.second.header.at("frame_id"), "1");
    EXPECT_EQ(messages.second.header.at("buffer_token"), "buffer");
    client.stop();
}
} // namespace
