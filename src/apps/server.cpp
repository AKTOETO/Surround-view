#include "sv/protocol.hpp"
#include "sv/renderer.hpp"
#include "sv/source.hpp"
#include "sv/vision.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cmath>
#include <csignal>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <openssl/rand.h>
#include <thread>

namespace
{
namespace asio = boost::asio;
using Socket = asio::generic::stream_protocol::socket;
struct Connection;

struct Command
{
    sv::Message message;
    std::weak_ptr<Connection> origin;
};

struct ServerIO
{
    asio::io_context io;
    asio::local::stream_protocol::acceptor control, data;
    asio::ip::tcp::acceptor tcp_control, tcp_data;
    bool owns_control = false, owns_data = false;
    asio::signal_set signals;
    std::filesystem::path directory;
    std::thread worker;
    std::atomic<bool> stop{false}, ready{false}, busy{false};
    std::atomic<uint64_t> generation{0};
    std::shared_ptr<Connection> ctl, video;
    std::mutex mutex;
    std::deque<Command> commands;
    std::string session, token;
    boost::json::array capabilities;
    ServerIO(const sv::Connections &, const std::string &source_type);
    ~ServerIO();
    template <class Acceptor> void accept(Acceptor &, bool);
    void connected(Socket, bool);
    void new_session();
    void deliver(Command);
    std::vector<Command> take();
    void answer(const Command &, sv::Message);
    bool publish(sv::Message);
};

struct Connection : std::enable_shared_from_this<Connection>
{
    Socket socket;
    ServerIO &host;
    bool is_control, hello = false;
    std::atomic<bool> closed{false};
    asio::steady_timer deadline, release_timer;
    sv::Decoder decoder;
    std::array<unsigned char, 8192> input{};
    std::deque<std::shared_ptr<std::vector<unsigned char>>> output;
    uint64_t last_command = 0;
    std::string frame_id, buffer_token;

    Connection(Socket s, ServerIO &h, bool c)
        : socket(std::move(s)), host(h), is_control(c), deadline(h.io), release_timer(h.io)
    {
    }

    void close()
    {
        if (closed)
        {
            return;
        }
        closed = true;
        boost::system::error_code ec;
        deadline.cancel();
        release_timer.cancel();
        socket.close(ec);
        if (!is_control)
        {
            host.ready = false;
            host.busy = false;
        }
        else
        {
            host.ready = false;
            if (host.video)
            {
                host.video->close();
            }
        }
    }

    void arm()
    {
        deadline.expires_after(std::chrono::seconds(2));
        auto self = shared_from_this();
        deadline.async_wait(
            [self](auto ec)
            {
                if (!ec)
                {
                    self->close();
                }
            });
    }

    void start()
    {
        arm();
        read();
    }

    void send(sv::Message message)
    {
        if (closed)
        {
            return;
        }
        if (output.size() >= (is_control ? 64 : 2))
        {
            close();
            return;
        }
        output.push_back(std::make_shared<std::vector<unsigned char>>(sv::encode(message)));
        if (output.size() == 1)
        {
            write();
        }
    }

    void write()
    {
        auto self = shared_from_this();
        auto bytes = output.front();
        asio::async_write(socket, asio::buffer(*bytes),
                          [self, bytes](auto ec, size_t)
                          {
                              if (ec)
                              {
                                  self->close();
                                  return;
                              }
                              self->output.pop_front();
                              if (!self->output.empty())
                              {
                                  self->write();
                              }
                          });
    }

    void read()
    {
        auto self = shared_from_this();
        socket.async_read_some(asio::buffer(input),
                               [self](auto ec, size_t n)
                               {
                                   if (ec)
                                   {
                                       self->close();
                                       return;
                                   }
                                   try
                                   {
                                       bool empty = self->decoder.buffered() == 0;
                                       if (empty && self->hello)
                                       {
                                           self->arm();
                                       }
                                       for (auto &m : self->decoder.feed(self->input.data(), n))
                                       {
                                           self->handle(std::move(m));
                                       }
                                       if (self->hello && self->decoder.buffered() == 0)
                                       {
                                           self->deadline.cancel();
                                       }
                                   }
                                   catch (...)
                                   {
                                       self->close();
                                   }
                                   if (!self->closed)
                                   {
                                       self->read();
                                   }
                               });
    }

    void handle(sv::Message m)
    {
        if (!hello)
        {
            if (m.type != 1 || !m.payload.empty() ||
                m.header.at("role").as_string() != (is_control ? "control" : "data"))
            {
                throw std::runtime_error("hello required");
            }
            if (!is_control && (!host.ctl || !host.ctl->hello ||
                                m.header.at("session_id").as_string() != host.session ||
                                m.header.at("data_token").as_string() != host.token))
            {
                throw std::runtime_error("session handshake mismatch");
            }
            hello = true;
            if (!is_control)
            {
                ++host.generation;
                host.ready = true;
            }
            send({2,
                  {{"session_id", host.session},
                   {"data_token", host.token},
                   {"profile", "linux-prototype-v1"},
                   {"capabilities", host.capabilities}},
                  {}});
            return;
        }
        if (!is_control)
        {
            if (m.type != 22 || m.header.at("session_id").as_string() != host.session)
            {
                throw std::runtime_error("release required");
            }
            if (m.header.at("frame_id").as_string() == frame_id &&
                m.header.at("buffer_token").as_string() == buffer_token)
            {
                release_timer.cancel();
                frame_id.clear();
                buffer_token.clear();
                host.busy = false;
            }
            return;
        }
        if (m.type != 20 || !m.payload.empty())
        {
            throw std::runtime_error("command required");
        }
        uint64_t id = sv::parse_decimal_u64(std::string(m.header.at("command_id").as_string()));
        if (id <= last_command)
        {
            send({21,
                  {{"command_id", std::to_string(id)},
                   {"accepted", false},
                   {"reason", "duplicate_or_out_of_order"}},
                  {}});
            return;
        }
        last_command = id;
        host.deliver({std::move(m), shared_from_this()});
    }

    void publish(sv::Message message)
    {
        frame_id = std::string(message.header.at("frame_id").as_string());
        buffer_token = std::string(message.header.at("buffer_token").as_string());
        message.header["session_id"] = host.session;
        send(std::move(message));
        release_timer.expires_after(std::chrono::milliseconds(250));
        auto self = shared_from_this();
        release_timer.async_wait(
            [self](auto ec)
            {
                if (!ec)
                {
                    self->close();
                }
            });
    }
};

template <class Acceptor> void ServerIO::accept(Acceptor &listener, bool c)
{
    listener.async_accept(
        [this, &listener, c](auto ec, auto socket)
        {
            if (!ec)
            {
                connected(Socket(std::move(socket)), c);
            }
            if (!stop && listener.is_open())
            {
                accept(listener, c);
            }
        });
}

void ServerIO::new_session()
{
    std::array<unsigned char, 24> bytes{};
    if (RAND_bytes(bytes.data(), bytes.size()) != 1)
    {
        throw std::runtime_error("session entropy failure");
    }
    constexpr char hex[] = "0123456789abcdef";
    token.clear();
    for (auto b : bytes)
    {
        token += hex[b >> 4];
        token += hex[b & 15];
    }
    session = std::to_string(sv::now_ns()) + "-" + token.substr(0, 16);
}

ServerIO::ServerIO(const sv::Connections &n, const std::string &source_type)
    : control(io), data(io), tcp_control(io), tcp_data(io), signals(io, SIGINT, SIGTERM),
      directory(n.unix_directory)
{
    capabilities = {source_type, "orbit",  "zoom",  "preset",
                    "pause",     "resume", "state", "copied_rgba"};
    if (source_type == "replay")
    {
        capabilities.push_back("step");
    }
    try
    {
        if (n.unix_enabled)
        {
            std::filesystem::create_directories(directory);
            if (std::filesystem::exists(directory / "control.sock") ||
                std::filesystem::exists(directory / "data.sock"))
            {
                throw std::runtime_error("IPC paths already exist; select fresh paths");
            }
            control.open();
            control.bind(
                asio::local::stream_protocol::endpoint((directory / "control.sock").string()));
            owns_control = true;
            control.listen(2);
            data.open();
            data.bind(asio::local::stream_protocol::endpoint((directory / "data.sock").string()));
            owns_data = true;
            data.listen(2);
        }
        if (n.tcp_enabled)
        {
            auto address = asio::ip::make_address(n.address);
            auto bind = [&](auto &acceptor, uint16_t port)
            {
                asio::ip::tcp::endpoint endpoint(address, port);
                acceptor.open(endpoint.protocol());
                acceptor.set_option(asio::socket_base::reuse_address(true));
                if (address.is_v6())
                {
                    acceptor.set_option(asio::ip::v6_only(true));
                }
                acceptor.bind(endpoint);
                acceptor.listen(2);
            };
            bind(tcp_control, n.control_port);
            bind(tcp_data, n.data_port);
        }
        new_session();
        if (n.unix_enabled)
        {
            accept(control, true);
            accept(data, false);
        }
        if (n.tcp_enabled)
        {
            accept(tcp_control, true);
            accept(tcp_data, false);
        }
        signals.async_wait([this](auto, int) { stop = true; });
        worker = std::thread([this] { io.run(); });
    }
    catch (...)
    {
        if (owns_control)
        {
            std::filesystem::remove(directory / "control.sock");
        }
        if (owns_data)
        {
            std::filesystem::remove(directory / "data.sock");
        }
        throw;
    }
}

ServerIO::~ServerIO()
{
    stop = true;
    io.stop();
    if (worker.joinable())
    {
        worker.join();
    }
    if (ctl)
    {
        ctl->close();
    }
    if (video)
    {
        video->close();
    }
    if (owns_control)
    {
        std::filesystem::remove(directory / "control.sock");
    }
    if (owns_data)
    {
        std::filesystem::remove(directory / "data.sock");
    }
}

void ServerIO::connected(Socket socket, bool c)
{
    auto &current = c ? ctl : video;
    if (current && !current->closed)
    {
        boost::system::error_code ignored;
        socket.close(ignored);
        return;
    }
    if (c)
    {
        new_session();
    }
    current = std::make_shared<Connection>(std::move(socket), *this, c);
    current->start();
}

void ServerIO::deliver(Command command)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (commands.size() == 64)
    {
        if (auto origin = command.origin.lock())
        {
            origin->send({21,
                          {{"command_id", command.message.header.at("command_id")},
                           {"accepted", false},
                           {"reason", "control_queue_full"}},
                          {}});
        }
        return;
    }
    commands.push_back(std::move(command));
}

std::vector<Command> ServerIO::take()
{
    std::vector<Command> out;
    std::lock_guard<std::mutex> lock(mutex);
    for (size_t n = 0; n < 16 && !commands.empty(); n++)
    {
        out.push_back(std::move(commands.front()));
        commands.pop_front();
    }
    return out;
}

void ServerIO::answer(const Command &command, sv::Message message)
{
    asio::post(io,
               [origin = command.origin, message = std::move(message)]() mutable
               {
                   if (auto c = origin.lock())
                   {
                       c->send(std::move(message));
                   }
               });
}

bool ServerIO::publish(sv::Message message)
{
    if (!ready || busy.exchange(true))
    {
        return false;
    }
    asio::post(io,
               [this, message = std::move(message)]() mutable
               {
                   if (video && !video->closed && video->hello)
                   {
                       video->publish(std::move(message));
                   }
                   else
                   {
                       busy = false;
                   }
               });
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::string cfg, manifest, trace = "artifacts/server_trace.jsonl";
        std::filesystem::path ipc;
        bool looping = true, loop_explicit = false;
        for (int i = 1; i < argc; i++)
        {
            std::string key = argv[i];
            if (i + 1 >= argc)
            {
                throw std::runtime_error("argument value required");
            }
            std::string v = argv[++i];
            if (key == "--config")
            {
                cfg = v;
            }
            else if (key == "--manifest")
            {
                manifest = v;
            }
            else if (key == "--ipc-dir")
            {
                ipc = v;
            }
            else if (key == "--trace")
            {
                trace = v;
            }
            else if (key == "--loop")
            {
                if (v != "true" && v != "false")
                {
                    throw std::runtime_error("--loop requires true|false");
                }
                looping = v == "true";
                loop_explicit = true;
            }
            else
            {
                throw std::runtime_error("unknown argument " + key);
            }
        }
        if (cfg.empty())
        {
            throw std::runtime_error("usage: sv-server --config FILE --manifest FILE --ipc-dir DIR "
                                     "[--trace FILE --loop true|false]");
        }
        auto c = sv::load_config(cfg);
        if (c.connections.explicit_config && !ipc.empty())
        {
            throw std::runtime_error("--ipc-dir cannot override explicit connections config");
        }
        if (!c.connections.explicit_config && !ipc.empty())
        {
            c.connections.unix_directory = ipc.string();
        }
        if (c.connections.tcp_enabled)
        {
            asio::ip::make_address(c.connections.address);
        }
        if (c.source.explicit_config)
        {
            if (!manifest.empty() || loop_explicit)
            {
                throw std::runtime_error("CLI cannot override explicit source config");
            }
            if (c.source.type == "replay")
            {
                auto path = std::filesystem::path(c.source.manifest);
                if (path.is_relative())
                {
                    path = std::filesystem::path(cfg).parent_path() / path;
                }
                manifest = path.string();
                looping = c.source.loop;
            }
        }
        if (c.source.type == "replay" && manifest.empty())
        {
            throw std::runtime_error("replay manifest required");
        }
        auto source = c.source.type == "socket" ? sv::make_socket_source(c)
                                                : sv::make_replay_source(c, manifest, looping);
        sv::Renderer renderer(c);
        sv::Synchronizer sync(c);
        ServerIO network(c.connections, c.source.type);
        sv::View view = c.view;
        if (!std::filesystem::path(trace).parent_path().empty())
        {
            std::filesystem::create_directories(std::filesystem::path(trace).parent_path());
        }
        std::ofstream log(trace);
        if (!log)
        {
            throw std::runtime_error("cannot open trace");
        }
        auto record = [&](boost::json::object obj)
        {
            obj["timestamp_ns"] = std::to_string(sv::now_ns());
            log << boost::json::serialize(obj) << '\n';
        };
        record(
            {{"event", "startup"}, {"gl_renderer", renderer.device()}, {"profile", c.profile_id}});
        uint64_t revision = 0, frame_id = 0, sequence = 0, applied_command = 0;
        uint64_t last_generation = 0, last_render = 0, source_request = 0;
        bool paused = false, dirty = true;
        sv::FrameSet last_set;
        std::optional<Command> pending_source;
        std::deque<Command> pending_commands;
        auto answer = [&](const Command &cmd, bool accepted, const std::string &reason)
        {
            const auto id = cmd.message.header.at("command_id");
            network.answer(cmd, {21,
                                 {{"command_id", id},
                                  {"accepted", accepted},
                                  {"reason", reason},
                                  {"state_revision", std::to_string(revision)},
                                  {"paused", paused},
                                  {"azimuth_rad", view.azimuth},
                                  {"elevation_rad", view.elevation},
                                  {"distance_m", view.distance},
                                  {"fusion_mode", c.fusion.mode},
                                  {"diagnostic_view", c.fusion.diagnostic}},
                                 {}});
            record({{"event", "command"},
                    {"command_id", id},
                    {"accepted", accepted},
                    {"state_revision", std::to_string(revision)}});
        };
        while (!network.stop)
        {
            const auto generation = network.generation.load();
            if (generation != last_generation && network.ready)
            {
                dirty = true;
                last_generation = generation;
            }
            for (auto &event : source->poll())
            {
                if (event.kind == sv::SourceEvent::Kind::Failure)
                {
                    throw std::runtime_error("FrameSource: " + event.reason);
                }
                if (event.kind == sv::SourceEvent::Kind::Frames)
                {
                    for (auto &frame : event.frames)
                    {
                        if (frame)
                        {
                            sync.push(std::move(*frame));
                        }
                    }
                    sequence = event.batch_id;
                    last_set = sync.select(sv::now_ns());
                    paused = event.paused;
                    dirty = true;
                    record({{"event", "frame_set"},
                            {"health", last_set.health},
                            {"skew_ns", std::to_string(last_set.skew_ns)},
                            {"sequence", std::to_string(sequence)}});
                }
                else if (event.kind == sv::SourceEvent::Kind::Status)
                {
                    record({{"event", "source_status"},
                            {"camera_id", event.camera_id},
                            {"reason", event.reason}});
                }
                else if (pending_source && event.request_id == source_request)
                {
                    paused = event.paused;
                    if (!event.reason.empty())
                    {
                        answer(*pending_source, false, event.reason);
                        pending_source.reset();
                        continue;
                    }
                    revision++;
                    applied_command = sv::parse_decimal_u64(
                        std::string(pending_source->message.header.at("command_id").as_string()));
                    dirty = true;
                    answer(*pending_source, true, "ok");
                    pending_source.reset();
                }
            }
            if (!pending_source && pending_commands.empty())
            {
                for (auto &cmd : network.take())
                {
                    pending_commands.push_back(std::move(cmd));
                }
            }
            while (!pending_source && !pending_commands.empty())
            {
                auto cmd = std::move(pending_commands.front());
                pending_commands.pop_front();
                auto origin = cmd.origin.lock();
                if (!origin || origin->closed)
                {
                    continue;
                }
                auto &m = cmd.message;
                bool accepted = true;
                std::string reason = "ok";
                auto candidate = view;
                std::string id, type;
                std::optional<sv::SourceAction> source_action;
                try
                {
                    id = std::string(m.header.at("command_id").as_string());
                    type = std::string(m.header.at("type").as_string());
                    auto number = [&](const char *k)
                    {
                        auto &v = m.header.at(k);
                        double x =
                            v.is_double() ? v.as_double() : static_cast<double>(v.as_int64());
                        if (!std::isfinite(x))
                        {
                            throw std::runtime_error("nonfinite command");
                        }
                        return x;
                    };
                    if (type == "state")
                    {
                        // Query returns authoritative state without a mutation.
                    }
                    else if (type == "orbit")
                    {
                        candidate.azimuth = std::remainder(
                            candidate.azimuth + number("azimuth_delta_rad"), 2 * sv::pi);
                        candidate.elevation = std::clamp(
                            candidate.elevation + number("elevation_delta_rad"), .35, sv::pi / 2);
                    }
                    else if (type == "zoom")
                    {
                        candidate.distance =
                            std::clamp(candidate.distance + number("distance_delta_m"), 6.0, 18.0);
                    }
                    else if (type == "preset")
                    {
                        std::string name(m.header.at("name").as_string());
                        if (name == "top")
                        {
                            candidate.azimuth = 0;
                            candidate.elevation = sv::pi / 2;
                        }
                        else if (name == "front")
                        {
                            candidate.azimuth = 0;
                            candidate.elevation = .8;
                        }
                        else if (name == "rear")
                        {
                            candidate.azimuth = sv::pi;
                            candidate.elevation = .8;
                        }
                        else
                        {
                            accepted = false;
                            reason = "unknown_preset";
                        }
                    }
                    else if (type == "pause")
                    {
                        source_action = sv::SourceAction::Pause;
                    }
                    else if (type == "resume")
                    {
                        source_action = sv::SourceAction::Resume;
                    }
                    else if (type == "step")
                    {
                        source_action = sv::SourceAction::Step;
                    }
                    else
                    {
                        accepted = false;
                        reason = "unknown_command";
                    }
                    if (!sv::safe_view(c.surface, candidate))
                    {
                        accepted = false;
                        reason = "view_clearance";
                    }
                }
                catch (const std::exception &e)
                {
                    accepted = false;
                    reason = e.what();
                }
                if (accepted && source_action)
                {
                    if (source->request(*source_action, ++source_request))
                    {
                        pending_source = std::move(cmd);
                        continue;
                    }
                    accepted = false;
                    reason = "source_control_queue_full";
                }
                if (accepted && type != "state")
                {
                    view = candidate;
                    revision++;
                    applied_command = sv::parse_decimal_u64(id);
                    dirty = true;
                }
                answer(cmd, accepted, reason);
            }
            const auto now = sv::now_ns();
            if (!paused)
            {
                auto current = sync.select(now);
                if (current.health != last_set.health)
                {
                    last_set = current;
                    dirty = true;
                }
            }
            if (dirty && network.ready && !network.busy && now - last_render >= 16000000)
            {
                const auto source_stats = source->stats();
                auto start = sv::now_ns();
                auto image = renderer.render(last_set, view);
                auto done = sv::now_ns();
                frame_id++;
                boost::json::array inputs;
                uint64_t oldest = done;
                for (int k = 0; k < 4; k++)
                {
                    boost::json::object input{{"camera_id", k},
                                              {"used", bool(last_set.frames[k])},
                                              {"calibration_id", c.cameras[k].calibration_id}};
                    if (last_set.frames[k])
                    {
                        input["sequence_id"] = std::to_string(last_set.frames[k]->sequence);
                        input["release_timestamp_ns"] =
                            std::to_string(last_set.frames[k]->release_ns);
                        const auto &frame = *last_set.frames[k];
                        input["source_session_id"] = frame.source_session;
                        input["source_clock_domain"] = frame.source_clock_domain;
                        input["source_sequence_id"] = std::to_string(frame.source_sequence);
                        input["source_timestamp_ns"] = std::to_string(frame.source_timestamp_ns);
                        oldest = std::min(oldest, frame.release_ns);
                    }
                    inputs.push_back(input);
                }
                sv::Message output{
                    11,
                    {{"frame_id", std::to_string(frame_id)},
                     {"frame_set_id", std::to_string(sequence)},
                     {"state_revision", std::to_string(revision)},
                     {"applied_command_id", std::to_string(applied_command)},
                     {"buffer_token", std::to_string(done) + ":" + std::to_string(frame_id)},
                     {"width", c.width},
                     {"height", c.height},
                     {"pixel_format", "RGBA8"},
                     {"fusion_mode", c.fusion.mode},
                     {"diagnostic_view", c.fusion.diagnostic},
                     {"stride_bytes", c.width * 4},
                     {"row_origin", "top_left"},
                     {"health", last_set.health},
                     {"paused", paused},
                     {"inputs", inputs},
                     {"clock_domain", "local_monotonic"},
                     {"oldest_release_timestamp_ns", std::to_string(oldest)},
                     {"render_complete_timestamp_ns", std::to_string(done)},
                     {"render_readback_ms", double(done - start) / 1e6},
                     {"source_type", c.source.type},
                     {"timestamp_basis", "server_delivery"},
                     {"source_received", std::to_string(source_stats.received)},
                     {"source_rejected", std::to_string(source_stats.rejected)},
                     {"decode_count", std::to_string(source_stats.decoded)},
                     {"source_dropped_batches", std::to_string(source_stats.dropped)},
                     {"source_queue_depth", source_stats.queued_batches},
                     {"mesh_build_count", std::to_string(renderer.mesh_builds())},
                     {"upload_count", std::to_string(renderer.uploads())}},
                    std::move(image.pixels)};
                if (network.publish(std::move(output)))
                {
                    record({{"event", "rendered"},
                            {"frame_id", std::to_string(frame_id)},
                            {"state_revision", std::to_string(revision)},
                            {"health", last_set.health},
                            {"source_type", c.source.type},
                            {"timestamp_basis", "server_delivery"},
                            {"source_received", std::to_string(source_stats.received)},
                            {"source_rejected", std::to_string(source_stats.rejected)},
                            {"decode_count", std::to_string(source_stats.decoded)},
                            {"source_dropped_batches", std::to_string(source_stats.dropped)},
                            {"source_queue_depth", source_stats.queued_batches},
                            {"mesh_build_count", std::to_string(renderer.mesh_builds())},
                            {"upload_count", std::to_string(renderer.uploads())},
                            {"render_readback_ms", double(done - start) / 1e6}});
                    dirty = false;
                    last_render = done;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        source->stop();
        record({{"event", "shutdown"}});
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
