#pragma once
#include "sv/protocol.hpp"
#include <boost/asio.hpp>
#include <chrono>
#include <deque>
#include <thread>

namespace sv::test
{
namespace asio = boost::asio;
using Tcp = asio::ip::tcp;

inline void accept(Tcp::acceptor &listener, Tcp::socket &socket)
{
    listener.non_blocking(true);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline)
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
    throw std::runtime_error("TCP mock accept deadline");
}

class Reader
{
    Tcp::socket &socket_;
    Decoder decoder_;
    std::deque<Message> messages_;

  public:

    explicit Reader(Tcp::socket &socket) : socket_(socket)
    {
        socket_.non_blocking(true);
    }

    Message receive()
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (messages_.empty())
        {
            if (std::chrono::steady_clock::now() >= deadline)
            {
                throw std::runtime_error("TCP mock receive deadline");
            }
            std::array<unsigned char, 8192> bytes{};
            boost::system::error_code error;
            const auto n = socket_.read_some(asio::buffer(bytes), error);
            if (error == asio::error::would_block || error == asio::error::try_again)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (error)
            {
                throw boost::system::system_error(error);
            }
            for (auto &message : decoder_.feed(bytes.data(), n))
            {
                messages_.push_back(std::move(message));
            }
        }
        auto result = std::move(messages_.front());
        messages_.pop_front();
        return result;
    }
};

inline void send(Tcp::socket &socket, const Message &message)
{
    socket.non_blocking(false);
    const auto bytes = encode(message);
    asio::write(socket, asio::buffer(bytes));
    socket.non_blocking(true);
}
} // namespace sv::test
