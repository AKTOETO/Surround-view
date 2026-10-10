#include "sv/client.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <thread>

namespace sv::client
{
namespace asio = boost::asio;
using Socket = asio::generic::stream_protocol::socket;

struct Client::Impl
{
    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> work{io.get_executor()};
    asio::steady_timer retry{io}, handshake{io}, commands_timer{io};
    asio::ip::tcp::resolver resolver{io};
    Options options;
    Handler handler;
    std::thread worker;
    std::atomic<bool> stopped{false};
    std::mutex command_submission;
    uint64_t next_id = 0;
    std::atomic<size_t> submitted{0};
    std::atomic<size_t> releases_submitted{0};

    struct Channel
    {
        Socket socket;
        asio::steady_timer partial;
        Decoder decoder;
        std::array<unsigned char, 8192> input{};
        std::deque<std::shared_ptr<std::vector<unsigned char>>> output;
        bool hello = false;

        explicit Channel(asio::io_context &io) : socket(io), partial(io)
        {
        }
    };

    std::shared_ptr<Channel> control, data;
    std::string session, token;
    std::map<uint64_t, std::chrono::steady_clock::time_point> pending;
    bool ready = false, failing = false;
    unsigned attempts = 0;

    Impl(Options o, Handler h) : options(std::move(o)), handler(std::move(h))
    {
        if (!handler || options.timeout_ms < 1 || options.timeout_ms > 60000 ||
            options.reconnect_ms < 1 || options.reconnect_ms > 60000 ||
            options.command_capacity < 1 || options.command_capacity > 4096 ||
            options.max_retries > 1000)
        {
            throw std::invalid_argument("invalid client options");
        }
        const auto &e = options.endpoint;
        if (e.transport == Endpoint::Transport::Unix &&
            (e.directory.empty() || e.directory.size() > 80))
        {
            throw std::invalid_argument("invalid Unix directory");
        }
        if (e.transport == Endpoint::Transport::Tcp &&
            (e.host.empty() || !e.control_port || !e.data_port || e.control_port == e.data_port))
        {
            throw std::invalid_argument("invalid TCP endpoint");
        }
        asio::post(io,
                   [this]
                   {
                       connect();
                       tick();
                   });
        worker = std::thread([this] { io.run(); });
    }

    ~Impl()
    {
        stop();
    }

    void emit(Event::Kind kind, std::string detail = {}, std::shared_ptr<const Message> m = {})
    {
        if (!stopped)
        {
            try
            {
                handler({kind, std::move(detail), std::move(m)});
            }
            catch (...)
            { /* Consumer exceptions must not terminate the network worker. */
            }
        }
    }

    bool current(const std::shared_ptr<Channel> &c) const
    {
        return c == control || c == data;
    }

    void close(const std::shared_ptr<Channel> &c)
    {
        if (!c)
        {
            return;
        }
        boost::system::error_code ec;
        c->partial.cancel();
        c->socket.close(ec);
    }

    void fail(const std::string &why)
    {
        if (failing || stopped)
        {
            return;
        }
        failing = true;
        ready = false;
        handshake.cancel();
        resolver.cancel();
        close(control);
        close(data);
        control.reset();
        data.reset();
        session.clear();
        token.clear();
        for (const auto &item : pending)
        {
            auto m = std::make_shared<Message>(Message{
                3, {{"command_id", std::to_string(item.first)}, {"reason", "session_lost"}}, {}});
            emit(Event::Kind::Error, "session_lost", m);
        }
        pending.clear();
        emit(Event::Kind::Error, why);
        emit(Event::Kind::State, "disconnected");
        if (attempts >= options.max_retries)
        {
            emit(Event::Kind::State, "retry_exhausted");
            return;
        }
        ++attempts;
        retry.expires_after(std::chrono::milliseconds(options.reconnect_ms));
        retry.async_wait(
            [this](auto ec)
            {
                if (!ec)
                {
                    connect();
                }
            });
    }

    void connect()
    {
        if (stopped)
        {
            return;
        }
        failing = false;
        emit(Event::Kind::State, "connecting");
        handshake.expires_after(std::chrono::milliseconds(options.timeout_ms));
        handshake.async_wait(
            [this](auto ec)
            {
                if (!ec)
                {
                    fail("handshake_timeout");
                }
            });
        control = std::make_shared<Channel>(io);
        open(control, true);
    }

    void open(const std::shared_ptr<Channel> &c, bool ctl)
    {
        auto done = [this, c, ctl](auto ec)
        {
            if (!current(c) || stopped)
            {
                return;
            }
            if (ec)
            {
                fail("connect: " + ec.message());
                return;
            }
            send(c, {1,
                     ctl ? boost::json::object{{"role", "control"}}
                         : boost::json::object{{"role", "data"},
                                               {"session_id", session},
                                               {"data_token", token}},
                     {}});
            read(c, ctl);
        };
        const auto &e = options.endpoint;
        if (e.transport == Endpoint::Transport::Unix)
        {
            asio::local::stream_protocol::endpoint ep(e.directory +
                                                      (ctl ? "/control.sock" : "/data.sock"));
            c->socket.async_connect(asio::generic::stream_protocol::endpoint(ep), done);
        }
        else
        {
            resolver.async_resolve(
                e.host, std::to_string(ctl ? e.control_port : e.data_port),
                [this, c, done](auto ec, auto results)
                {
                    if (!current(c) || stopped)
                    {
                        return;
                    }
                    if (ec)
                    {
                        done(ec);
                        return;
                    }
                    // All resolved addresses are tried without changing transport or port.
                    auto endpoints =
                        std::make_shared<std::vector<asio::generic::stream_protocol::endpoint>>();
                    for (const auto &r : results)
                    {
                        endpoints->emplace_back(r.endpoint());
                    }
                    asio::async_connect(c->socket, *endpoints,
                                        [endpoints, done](auto error, auto) { done(error); });
                });
        }
    }

    void send(const std::shared_ptr<Channel> &c, const Message &m)
    {
        if (!c || !current(c))
        {
            return;
        }
        if (c->output.size() >= options.command_capacity + 2)
        {
            fail("write_queue_full");
            return;
        }
        c->output.push_back(std::make_shared<std::vector<unsigned char>>(encode(m)));
        if (c->output.size() == 1)
        {
            write(c);
        }
    }

    void write(const std::shared_ptr<Channel> &c)
    {
        auto bytes = c->output.front();
        asio::async_write(c->socket, asio::buffer(*bytes),
                          [this, c, bytes](auto ec, size_t)
                          {
                              if (!current(c) || stopped)
                              {
                                  return;
                              }
                              if (ec)
                              {
                                  fail("write: " + ec.message());
                                  return;
                              }
                              c->output.pop_front();
                              if (!c->output.empty())
                              {
                                  write(c);
                              }
                          });
    }

    void read(const std::shared_ptr<Channel> &c, bool ctl)
    {
        c->socket.async_read_some(asio::buffer(c->input),
                                  [this, c, ctl](auto ec, size_t n)
                                  {
                                      if (!current(c) || stopped)
                                      {
                                          return;
                                      }
                                      if (ec)
                                      {
                                          fail("read: " + ec.message());
                                          return;
                                      }
                                      try
                                      {
                                          if (c->decoder.buffered() == 0)
                                          {
                                              c->partial.expires_after(
                                                  std::chrono::milliseconds(options.timeout_ms));
                                              c->partial.async_wait(
                                                  [this, c](auto error)
                                                  {
                                                      if (!error && current(c))
                                                      {
                                                          fail("partial_message_timeout");
                                                      }
                                                  });
                                          }
                                          for (auto &m : c->decoder.feed(c->input.data(), n))
                                          {
                                              handle(c, ctl, std::move(m));
                                          }
                                          if (c->decoder.buffered() == 0)
                                          {
                                              c->partial.cancel();
                                          }
                                      }
                                      catch (const std::exception &e)
                                      {
                                          fail(std::string("protocol: ") + e.what());
                                          return;
                                      }
                                      if (current(c))
                                      {
                                          read(c, ctl);
                                      }
                                  });
    }

    void handle(const std::shared_ptr<Channel> &c, bool ctl, Message m)
    {
        if (!c->hello)
        {
            if (m.type != 2 || !m.payload.empty())
            {
                throw std::runtime_error("hello_ack required");
            }
            auto sid = std::string(m.header.at("session_id").as_string());
            if (ctl)
            {
                session = sid;
                token = std::string(m.header.at("data_token").as_string());
                if (session.empty() || token.empty() || !m.header.at("capabilities").is_array())
                {
                    throw std::runtime_error("invalid handshake");
                }
                c->hello = true;
                emit(Event::Kind::Message, {}, std::make_shared<Message>(m));
                data = std::make_shared<Channel>(io);
                open(data, false);
            }
            else
            {
                if (sid != session)
                {
                    throw std::runtime_error("data session mismatch");
                }
                c->hello = true;
                ready = true;
                attempts = 0;
                handshake.cancel();
                emit(Event::Kind::State, "ready");
            }
            return;
        }
        if (ctl)
        {
            if (m.type != 21 || !m.payload.empty())
            {
                throw std::runtime_error("unexpected control message");
            }
            auto id = parse_decimal_u64(std::string(m.header.at("command_id").as_string()));
            // Validate the ACK before completing it. Otherwise fail() cannot
            // report the mutation's unknown outcome for a malformed response.
            const bool accepted = m.header.at("accepted").as_bool();
            if (!accepted)
            {
                m.header.at("reason").as_string();
            }
            if (!pending.erase(id))
            {
                throw std::runtime_error("unexpected command ACK");
            }
        }
        else
        {
            if (m.type != 11 || m.header.at("session_id").as_string() != session ||
                m.header.at("pixel_format").as_string() != "RGBA8" ||
                m.header.at("row_origin").as_string() != "top_left")
            {
                throw std::runtime_error("invalid frame metadata");
            }
            const auto width = m.header.at("width").as_int64(),
                       height = m.header.at("height").as_int64();
            if (width < 2 || width > 4096 || height < 2 || height > 2160 ||
                m.header.at("stride_bytes").as_int64() != width * 4 ||
                m.payload.size() != size_t(width * height * 4))
            {
                throw std::runtime_error("invalid RGBA size");
            }
            for (auto key : {"frame_id", "frame_set_id", "state_revision"})
            {
                parse_decimal_u64(std::string(m.header.at(key).as_string()));
            }
            m.header.at("buffer_token").as_string();
        }
        emit(Event::Kind::Message, {}, std::make_shared<Message>(std::move(m)));
    }

    void tick()
    {
        commands_timer.expires_after(std::chrono::milliseconds(20));
        commands_timer.async_wait(
            [this](auto ec)
            {
                if (ec || stopped)
                {
                    return;
                }
                auto now = std::chrono::steady_clock::now();
                for (const auto &item : pending)
                {
                    if (now >= item.second)
                    {
                        fail("command_timeout");
                        break;
                    }
                }
                tick();
            });
    }

    void stop()
    {
        if (worker.joinable() && worker.get_id() == std::this_thread::get_id())
        {
            throw std::logic_error("Client::stop cannot run in a callback");
        }
        if (!stopped.exchange(true))
        {
            io.stop();
        }
        if (worker.joinable())
        {
            worker.join();
        }
        close(control);
        close(data);
        resolver.cancel();
    }
};

Client::Client(Options o, Handler h) : impl_(std::make_unique<Impl>(std::move(o), std::move(h)))
{
}

Client::~Client() = default;

void Client::stop()
{
    impl_->stop();
}

uint64_t Client::command(std::string type, boost::json::object p)
{
    auto &i = *impl_;
    // Serialize ID allocation through post(): concurrent callers must reach the
    // control stream in increasing ID order, as required by the server.
    std::lock_guard<std::mutex> lock(i.command_submission);
    if (i.stopped)
    {
        throw std::logic_error("client stopped");
    }
    if (i.next_id == std::numeric_limits<uint64_t>::max())
    {
        throw std::overflow_error("command ID exhausted; create a new Client");
    }
    const auto id = ++i.next_id;
    p["type"] = std::move(type);
    p["command_id"] = std::to_string(id);
    auto bytes = encode({20, p, {}}); // Validate size before queueing.
    (void)bytes;
    if (i.submitted.fetch_add(1) >= i.options.command_capacity)
    {
        --i.submitted;
        throw std::runtime_error("command submission queue full");
    }
    asio::post(
        i.io,
        [&i, id, p = std::move(p)]
        {
            --i.submitted;
            if (!i.ready || i.pending.size() >= i.options.command_capacity)
            {
                i.emit(Event::Kind::Error, "not_ready_or_queue_full",
                       std::make_shared<Message>(Message{3,
                                                         {{"command_id", std::to_string(id)},
                                                          {"reason", "not_ready_or_queue_full"}},
                                                         {}}));
                return;
            }
            i.pending[id] =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(i.options.timeout_ms);
            i.send(i.control, {20, p, {}});
        });
    return id;
}

void Client::release(const boost::json::object &frame)
{
    auto &i = *impl_;
    if (i.stopped)
    {
        return;
    }
    boost::json::object h;
    for (auto key : {"session_id", "frame_id", "buffer_token"})
    {
        h[key] = frame.at(key);
    }
    for (const auto &entry : h)
    {
        entry.value().as_string();
    }
    if (i.releases_submitted.fetch_add(1) >= i.options.command_capacity)
    {
        --i.releases_submitted;
        throw std::runtime_error("release submission queue full");
    }
    asio::post(i.io,
               [&i, h = std::move(h)]
               {
                   --i.releases_submitted;
                   if (i.ready && h.at("session_id").as_string() == i.session)
                   {
                       i.send(i.data, {22, h, {}});
                   }
               });
}
} // namespace sv::client
