#include "sv/client.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

namespace asio = boost::asio;
using Tcp = asio::ip::tcp;

void check(bool value, const char *why)
{
    if (!value)
    {
        throw std::runtime_error(why);
    }
}

sv::Message receive(Tcp::socket &socket)
{
    sv::Decoder decoder;
    std::array<unsigned char, 8192> bytes{};
    while (true)
    {
        auto n = socket.read_some(asio::buffer(bytes));
        auto messages = decoder.feed(bytes.data(), n);
        if (!messages.empty())
        {
            return std::move(messages.front());
        }
    }
}

void send(Tcp::socket &socket, const sv::Message &message)
{
    auto bytes = sv::encode(message);
    asio::write(socket, asio::buffer(bytes));
}

void mock(int mode)
{
    asio::io_context io;
    Tcp::acceptor ctl(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    Tcp::acceptor data(io, Tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    sv::client::Options options;
    options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    options.endpoint.control_port = ctl.local_endpoint().port();
    options.endpoint.data_port = data.local_endpoint().port();
    options.timeout_ms = 100;
    options.max_retries = 0;
    std::string server_error;
    std::thread server(
        [&]
        {
            try
            {
                Tcp::socket socket(io);
                ctl.accept(socket);
                if (mode == 0)
                {
                    asio::write(socket, asio::buffer("SV01", 4));
                    std::this_thread::sleep_for(std::chrono::milliseconds(180));
                    return;
                }
                receive(socket);
                send(socket, {2,
                              {{"session_id", "s"},
                               {"data_token", "t"},
                               {"capabilities", boost::json::array{}}},
                              {}});
                Tcp::socket video(io);
                data.accept(video);
                receive(video);
                send(video, {2, {{"session_id", "s"}}, {}});
                if (mode >= 3)
                {
                    const auto command = receive(socket);
                    boost::json::object ack{{"command_id", command.header.at("command_id")}};
                    if (mode == 3)
                    {
                        ack["accepted"] = "invalid_boolean";
                    }
                    else
                    {
                        ack["accepted"] = false;
                        ack["reason"] = mode == 4 ? boost::json::value(42)
                                                  : boost::json::value("invalid_parameters");
                    }
                    send(socket, {21, std::move(ack), {}});
                    std::this_thread::sleep_for(std::chrono::milliseconds(80));
                    return;
                }
                if (mode == 2)
                {
                    asio::write(video, asio::buffer("SV01", 4));
                    std::this_thread::sleep_for(std::chrono::milliseconds(180));
                    return;
                }
                send(video, {11,
                             {{"session_id", "s"},
                              {"pixel_format", "RGBA8"},
                              {"row_origin", "top_left"},
                              {"width", 4096},
                              {"height", 2160},
                              {"stride_bytes", 16384},
                              {"frame_id", "1"},
                              {"frame_set_id", "1"},
                              {"state_revision", "0"},
                              {"buffer_token", "b"}},
                             {0}});
                std::this_thread::sleep_for(std::chrono::milliseconds(80));
            }
            catch (const std::exception &e)
            {
                server_error = e.what();
            }
        });
    std::mutex mutex;
    std::condition_variable cv;
    bool failed = false, delivered = false, ready = false, ack_delivered = false;
    bool command_lost = false;
    std::string detail;
    {
        sv::client::Client client(
            options,
            [&](sv::client::Event event)
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (event.kind == sv::client::Event::Kind::Error)
                {
                    if (event.detail == "session_lost" && event.message)
                    {
                        command_lost = event.message->header.at("command_id") == "1";
                    }
                    failed = true;
                    detail = event.detail;
                }
                if (event.kind == sv::client::Event::Kind::State && event.detail == "ready")
                {
                    ready = true;
                }
                if (event.kind == sv::client::Event::Kind::Message && event.message->type == 21)
                {
                    ack_delivered = true;
                }
                if (event.message && event.message->type == 11)
                {
                    delivered = true;
                }
                cv.notify_one();
            });
        std::unique_lock<std::mutex> lock(mutex);
        if (mode >= 3)
        {
            const bool connected =
                cv.wait_for(lock, std::chrono::seconds(2), [&] { return ready || failed; });
            if (connected && ready)
            {
                lock.unlock();
                client.state();
                lock.lock();
            }
        }
        const bool signaled =
            cv.wait_for(lock, std::chrono::seconds(2),
                        [&]
                        {
                            return failed && ((mode != 3 && mode != 4) ||
                                              detail.find("protocol:") != std::string::npos);
                        });
        lock.unlock();
        client.stop();
        server.join();
        check(signaled, "client failed to enforce deadline/metadata");
    }
    check(server_error.empty(), server_error.c_str());
    check(!delivered, "invalid frame delivered");
    if (mode == 3 || mode == 4)
    {
        check(command_lost, "malformed ACK silently removed the pending command");
        check(!ack_delivered, "malformed ACK delivered to the consumer");
        check(detail.find("protocol:") != std::string::npos, "malformed ACK not a protocol error");
    }
    else if (mode == 5)
    {
        check(ack_delivered && !command_lost, "valid rejection was not completed normally");
    }
    else
    {
        check(mode == 1 ? detail.find("invalid RGBA size") != std::string::npos
                        : detail.find("timeout") != std::string::npos,
              "unexpected error reason");
    }
}

int main()
{
    try
    {
        bool rejected = false;
        try
        {
            sv::client::Options invalid;
            invalid.timeout_ms = 0;
            sv::client::Client client(invalid, [](auto) {});
        }
        catch (const std::invalid_argument &)
        {
            rejected = true;
        }
        check(rejected, "invalid options accepted");
        mock(0);
        mock(1);
        mock(2);
        mock(3);
        mock(4);
        mock(5);
        std::atomic<unsigned> callbacks{0};
        sv::client::Options unavailable;
        unavailable.endpoint.directory = "/tmp/sv-does-not-exist-lifecycle";
        sv::client::Client client(unavailable, [&](auto) { ++callbacks; });
        client.stop();
        const auto before = callbacks.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        check(callbacks == before, "callback after shutdown");
        rejected = false;
        try
        {
            client.command("state");
        }
        catch (const std::logic_error &)
        {
            rejected = true;
        }
        check(rejected, "command after shutdown accepted");
        std::cout << "client lifecycle passed\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
