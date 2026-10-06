#include "sv/protocol.hpp"
#include "sv/source.hpp"
#include <algorithm>
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <mutex>
#include <thread>

namespace sv
{
namespace
{
namespace asio = boost::asio;
using Socket = asio::generic::stream_protocol::socket;

class SocketSource final : public FrameSource
{
    asio::io_context io_;
    Config config_;
    struct Channel;
    std::array<std::unique_ptr<asio::local::stream_protocol::acceptor>, 4> unix_;
    std::array<std::unique_ptr<asio::ip::tcp::acceptor>, 4> tcp_;
    std::array<std::shared_ptr<Channel>, 4> channels_;
    std::vector<std::filesystem::path> owned_paths_;
    std::array<uint64_t, 4> sequences_{};
    mutable std::mutex mutex_;
    std::deque<SourceEvent> events_;
    size_t batches_ = 0, statuses_ = 0;
    std::array<size_t, 4> queued_{};
    std::atomic<bool> stopped_{false};
    std::atomic<unsigned> controls_{0};
    std::atomic<uint64_t> received_{0}, dropped_{0}, rejected_{0};
    std::thread worker_;
    bool paused_ = false;
    uint64_t batch_id_ = 0;

    void publish(SourceEvent event)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (event.kind == SourceEvent::Kind::Frames)
        {
            const int id = event.camera_id;
            if (queued_[id] == size_t(config_.queue_size))
            {
                auto old = std::find_if(
                    events_.begin(), events_.end(), [id](const auto &e)
                    { return e.kind == SourceEvent::Kind::Frames && e.camera_id == id; });
                events_.erase(old);
                --batches_;
                --queued_[id];
                ++dropped_;
            }
            ++batches_;
            ++queued_[id];
        }
        if (event.kind == SourceEvent::Kind::Status)
        {
            if (statuses_ == 16)
            {
                return;
            }
            ++statuses_;
        }
        events_.push_back(std::move(event));
    }

    struct Channel : std::enable_shared_from_this<Channel>
    {
        Socket socket;
        SocketSource &host;
        int id;
        asio::steady_timer deadline;
        Decoder decoder;
        std::array<unsigned char, 8192> bytes{};
        bool closed = false, hello = false;
        std::optional<uint64_t> last_sequence, last_timestamp;
        std::string session, clock;

        Channel(Socket s, SocketSource &h, int camera)
            : socket(std::move(s)), host(h), id(camera), deadline(h.io_)
        {
        }

        void close(const std::string &why)
        {
            if (closed)
            {
                return;
            }
            closed = true;
            boost::system::error_code ec;
            socket.close(ec);
            deadline.cancel();
            SourceEvent event;
            event.kind = SourceEvent::Kind::Status;
            event.camera_id = id;
            event.reason = why;
            host.publish(std::move(event));
        }

        void arm()
        {
            deadline.expires_after(
                std::chrono::milliseconds(host.config_.source.message_timeout_ms));
            auto self = shared_from_this();
            deadline.async_wait(
                [self](auto ec)
                {
                    if (!ec)
                    {
                        self->close("source_message_timeout");
                    }
                });
        }

        void start()
        {
            arm();
            read();
        }

        void read()
        {
            auto self = shared_from_this();
            socket.async_read_some(asio::buffer(bytes),
                                   [self](auto ec, size_t n)
                                   {
                                       if (self->closed)
                                       {
                                           return;
                                       }
                                       if (ec)
                                       {
                                           self->close("source_disconnected");
                                           return;
                                       }
                                       try
                                       {
                                           if (self->hello && self->decoder.buffered() == 0)
                                           {
                                               self->arm();
                                           }
                                           for (auto &m : self->decoder.feed(self->bytes.data(), n))
                                           {
                                               self->handle(std::move(m));
                                           }
                                           if (self->hello && self->decoder.buffered() == 0)
                                           {
                                               self->deadline.cancel();
                                           }
                                       }
                                       catch (const std::exception &e)
                                       {
                                           ++self->host.rejected_;
                                           self->close(std::string("source_rejected: ") + e.what());
                                       }
                                       if (!self->closed)
                                       {
                                           self->read();
                                       }
                                   });
        }

        void handle(Message m)
        {
            const auto &camera = host.config_.cameras[id];
            auto &h = m.header;
            if (!hello)
            {
                if (m.type != 1 || !m.payload.empty() || h.at("role").as_string() != "producer" ||
                    h.at("camera_id").as_int64() != id ||
                    h.at("calibration_id").as_string() != camera.calibration_id ||
                    h.at("width").as_int64() != camera.width ||
                    h.at("height").as_int64() != camera.height ||
                    h.at("pixel_format").as_string() != "RGB8" ||
                    h.at("row_origin").as_string() != "top_left")
                {
                    throw std::runtime_error("producer handshake mismatch");
                }
                clock = std::string(h.at("clock_domain").as_string());
                if (clock.empty() || clock.size() > 128)
                {
                    throw std::runtime_error("invalid clock domain");
                }
                session = "camera-" + std::to_string(id) + "-" + std::to_string(now_ns());
                hello = true;
                auto reply = std::make_shared<std::vector<unsigned char>>(
                    encode({2,
                            {{"session_id", session},
                             {"camera_id", id},
                             {"timestamp_basis", "server_delivery"}},
                            {}}));
                auto self = shared_from_this();
                asio::async_write(socket, asio::buffer(*reply),
                                  [self, reply](auto ec, size_t)
                                  {
                                      if (ec)
                                      {
                                          self->close("source_handshake_write_failed");
                                      }
                                  });
                return;
            }
            if (m.type != 10 || h.at("session_id").as_string() != session ||
                h.at("camera_id").as_int64() != id ||
                h.at("calibration_id").as_string() != camera.calibration_id ||
                h.at("width").as_int64() != camera.width ||
                h.at("height").as_int64() != camera.height ||
                h.at("stride_bytes").as_int64() != camera.width * 3 ||
                h.at("pixel_format").as_string() != "RGB8" ||
                h.at("row_origin").as_string() != "top_left" ||
                h.at("clock_domain").as_string() != clock ||
                m.payload.size() != size_t(camera.width) * camera.height * 3)
            {
                throw std::runtime_error("camera frame metadata/payload mismatch");
            }
            const auto sequence = parse_decimal_u64(std::string(h.at("sequence_id").as_string()));
            const auto timestamp =
                parse_decimal_u64(std::string(h.at("source_timestamp_ns").as_string()));
            const auto scenario =
                parse_decimal_u64(std::string(h.at("scenario_timestamp_ns").as_string()));
            if ((last_sequence && sequence <= *last_sequence) ||
                (last_timestamp && timestamp < *last_timestamp))
            {
                throw std::runtime_error("duplicate/out_of_order source frame");
            }
            last_sequence = sequence;
            last_timestamp = timestamp;
            ++host.received_;
            if (host.paused_)
            {
                ++host.dropped_;
                return;
            }
            auto image = std::make_shared<Image>(
                Image{camera.width, camera.height, 3, std::move(m.payload)});
            Frame frame{id, host.sequences_[id]++, now_ns(), scenario, std::move(image)};
            frame.source_session = session;
            frame.source_clock_domain = clock;
            frame.source_sequence = sequence;
            frame.source_timestamp_ns = timestamp;
            SourceEvent event;
            event.camera_id = id;
            event.frames[id] = std::move(frame);
            event.batch_id = ++host.batch_id_;
            host.publish(std::move(event));
        }
    };

    template <class Acceptor> void accept(Acceptor &listener, int id)
    {
        listener.async_accept(
            [this, &listener, id](auto ec, auto socket)
            {
                if (!ec)
                {
                    auto &current = channels_[id];
                    if (current && !current->closed)
                    {
                        boost::system::error_code ignored;
                        socket.close(ignored);
                    }
                    else
                    {
                        current = std::make_shared<Channel>(Socket(std::move(socket)), *this, id);
                        current->start();
                    }
                }
                if (!stopped_ && listener.is_open())
                {
                    accept(listener, id);
                }
            });
    }

    void cleanup_paths()
    {
        for (const auto &path : owned_paths_)
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
        owned_paths_.clear();
    }

  public:

    explicit SocketSource(const Config &config) : config_(config)
    {
        try
        {
            for (int id = 0; id < 4; ++id)
            {
                const auto &ep = config.source.cameras[id];
                if (ep.transport == "unix")
                {
                    if (std::filesystem::exists(ep.path))
                    {
                        throw std::runtime_error("camera socket path exists");
                    }
                    std::filesystem::create_directories(
                        std::filesystem::path(ep.path).parent_path());
                    auto &a = unix_[id];
                    a = std::make_unique<asio::local::stream_protocol::acceptor>(io_);
                    a->open();
                    a->bind(asio::local::stream_protocol::endpoint(ep.path));
                    owned_paths_.push_back(ep.path);
                    a->listen(2);
                }
                else
                {
                    auto &a = tcp_[id];
                    a = std::make_unique<asio::ip::tcp::acceptor>(io_);
                    auto address = asio::ip::make_address(ep.address);
                    asio::ip::tcp::endpoint endpoint(address, ep.port);
                    a->open(endpoint.protocol());
                    a->set_option(asio::socket_base::reuse_address(true));
                    if (address.is_v6())
                    {
                        a->set_option(asio::ip::v6_only(true));
                    }
                    a->bind(endpoint);
                    a->listen(2);
                }
            }
            for (int id = 0; id < 4; ++id)
            {
                if (unix_[id])
                {
                    accept(*unix_[id], id);
                }
                if (tcp_[id])
                {
                    accept(*tcp_[id], id);
                }
            }
            worker_ = std::thread([this] { io_.run(); });
        }
        catch (...)
        {
            cleanup_paths();
            throw;
        }
    }

    ~SocketSource() override
    {
        stop();
    }

    bool request(SourceAction action, uint64_t id) override
    {
        if (stopped_)
        {
            return false;
        }
        if (controls_.fetch_add(1) >= 64)
        {
            --controls_;
            return false;
        }
        asio::post(io_,
                   [this, action, id]
                   {
                       SourceEvent completed;
                       completed.kind = SourceEvent::Kind::Control;
                       completed.request_id = id;
                       if (action == SourceAction::Step)
                       {
                           completed.reason = "step_requires_replay";
                       }
                       else
                       {
                           paused_ = action == SourceAction::Pause;
                       }
                       completed.paused = paused_;
                       publish(std::move(completed));
                   });
        return true;
    }

    std::vector<SourceEvent> poll() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SourceEvent> result;
        while (!events_.empty())
        {
            if (events_.front().kind == SourceEvent::Kind::Control)
            {
                --controls_;
            }
            result.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        batches_ = statuses_ = 0;
        queued_.fill(0);
        return result;
    }

    SourceStats stats() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {0, dropped_.load(), received_.load(), rejected_.load(), batches_};
    }

    void stop() override
    {
        if (!stopped_.exchange(true))
        {
            io_.stop();
        }
        if (worker_.joinable())
        {
            worker_.join();
        }
        for (auto &channel : channels_)
        {
            if (channel)
            {
                channel->close("source_stopped");
            }
        }
        for (auto &listener : unix_)
        {
            if (listener)
            {
                boost::system::error_code ec;
                listener->close(ec);
            }
        }
        for (auto &listener : tcp_)
        {
            if (listener)
            {
                boost::system::error_code ec;
                listener->close(ec);
            }
        }
        cleanup_paths();
    }
};
} // namespace

std::unique_ptr<FrameSource> make_socket_source(const Config &config)
{
    return std::make_unique<SocketSource>(config);
}
} // namespace sv
