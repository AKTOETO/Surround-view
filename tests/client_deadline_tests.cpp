#include "sv/client.hpp"
#include <boost/asio.hpp>
#include <chrono>
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

void accept(Tcp::acceptor &listener, Tcp::socket &socket)
{
    listener.non_blocking(true);
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline)
    {
        boost::system::error_code error;
        listener.accept(socket, error);
        if (!error)
        {
            return;
        }
        if (error != asio::error::would_block && error != asio::error::try_again)
        {
            throw boost::system::system_error(error);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("mock accept deadline");
}

sv::Message receive(Tcp::socket &socket)
{
    socket.non_blocking(true);
    sv::Decoder decoder;
    std::array<unsigned char, 8192> bytes{};
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (Clock::now() < deadline)
    {
        boost::system::error_code error;
        const auto n = socket.read_some(asio::buffer(bytes), error);
        if (error == asio::error::would_block || error == asio::error::try_again)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        if (error)
        {
            throw boost::system::system_error(error);
        }
        auto messages = decoder.feed(bytes.data(), n);
        if (!messages.empty())
        {
            return std::move(messages.front());
        }
    }
    throw std::runtime_error("mock receive deadline");
}

void fragmented(Tcp::socket &socket, const sv::Message &message, size_t prefix)
{
    const auto bytes = sv::encode(message);
    socket.non_blocking(false);
    asio::write(socket, asio::buffer(bytes.data(), prefix));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    asio::write(socket, asio::buffer(bytes.data() + prefix, bytes.size() - prefix));
}

class ClientDeadline : public testing::TestWithParam<bool>
{
};

TEST_P(ClientDeadline, CompleteFragmentsAllowIdleAndPartialMessageStillReconnects)
{
    const bool partial_control = GetParam();
    asio::io_context io;
    Tcp::acceptor control(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    Tcp::acceptor data(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    sv::client::Options options;
    options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    options.endpoint.control_port = control.local_endpoint().port();
    options.endpoint.data_port = data.local_endpoint().port();
    options.timeout_ms = 300;
    options.reconnect_ms = 20;
    options.max_retries = 1;
    std::promise<void> start_partial, finish;
    const auto partial_signal = start_partial.get_future();
    const auto finish_signal = finish.get_future();
    auto server = std::async(
        std::launch::async,
        [&]
        {
            auto handshake = [&](Tcp::socket &ctl, Tcp::socket &video, const std::string &session)
            {
                accept(control, ctl);
                receive(ctl);
                fragmented(ctl,
                           {2,
                            {{"session_id", session},
                             {"data_token", "token"},
                             {"capabilities", boost::json::array{}}},
                            {}},
                           1);
                accept(data, video);
                receive(video);
                fragmented(video, {2, {{"session_id", session}}, {}}, 24);
            };
            Tcp::socket ctl(io), video(io);
            handshake(ctl, video, "first");
            const auto command = receive(ctl);
            fragmented(
                ctl,
                {21, {{"command_id", command.header.at("command_id")}, {"accepted", true}}, {}},
                24);
            for (unsigned id = 1; id <= 3; ++id)
            {
                fragmented(video,
                           {11,
                            {{"session_id", "first"},
                             {"frame_id", std::to_string(id)},
                             {"frame_set_id", std::to_string(id)},
                             {"state_revision", "0"},
                             {"buffer_token", std::to_string(id)},
                             {"pixel_format", "RGBA8"},
                             {"row_origin", "top_left"},
                             {"width", 2},
                             {"height", 2},
                             {"stride_bytes", 8}},
                            std::vector<unsigned char>(16, 127)},
                           id);
            }
            if (partial_signal.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
            {
                throw std::runtime_error("partial signal deadline");
            }
            auto &broken = partial_control ? ctl : video;
            broken.non_blocking(false);
            asio::write(broken, asio::buffer("SV", 2));
            Tcp::socket new_ctl(io), new_video(io);
            handshake(new_ctl, new_video, "second");
            if (finish_signal.wait_for(std::chrono::seconds(3)) != std::future_status::ready)
            {
                throw std::runtime_error("finish signal deadline");
            }
        });
    std::mutex mutex;
    std::condition_variable changed;
    unsigned ready = 0, frames = 0, acks = 0;
    std::vector<std::string> errors;
    sv::client::Client client(options,
                              [&](sv::client::Event event)
                              {
                                  std::lock_guard<std::mutex> lock(mutex);
                                  if (event.kind == sv::client::Event::Kind::State &&
                                      event.detail == "ready")
                                  {
                                      ++ready;
                                  }
                                  if (event.kind == sv::client::Event::Kind::Error)
                                  {
                                      errors.push_back(event.detail);
                                  }
                                  if (event.message)
                                  {
                                      frames += event.message->type == 11;
                                      acks += event.message->type == 21;
                                  }
                                  changed.notify_all();
                              });
    auto wait = [&](auto condition)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, std::chrono::seconds(2), condition);
    };
    EXPECT_TRUE(wait([&] { return ready == 1; }));
    client.state();
    EXPECT_TRUE(wait([&] { return frames == 3 && acks == 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_EQ(ready, 1U);
        EXPECT_TRUE(errors.empty());
    }
    start_partial.set_value();
    EXPECT_TRUE(wait([&] { return ready == 2; }));
    client.stop();
    finish.set_value();
    EXPECT_NO_THROW(server.get());
    EXPECT_EQ(errors, std::vector<std::string>{"partial_message_timeout"});
}

INSTANTIATE_TEST_SUITE_P(ControlAndData, ClientDeadline, testing::Values(false, true));
} // namespace
