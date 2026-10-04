#include "sv/protocol.hpp"
#include "sv/renderer.hpp"
#include "sv/vision.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cmath>
#include <csignal>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <thread>

namespace
{
namespace asio = boost::asio;
using Socket = asio::local::stream_protocol::socket;
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
    asio::signal_set signals;
    std::filesystem::path directory;
    std::thread worker;
    std::atomic<bool> stop{false}, ready{false}, busy{false};
    std::shared_ptr<Connection> ctl, video;
    std::mutex mutex;
    std::deque<Command> commands;
    std::string session, token;
    ServerIO(const std::filesystem::path &);
    ~ServerIO();
    void accept(bool);
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
        if (output.size() >= (is_control ? 64 : 1))
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
                host.ready = true;
            }
            send({2,
                  {{"session_id", host.session},
                   {"data_token", host.token},
                   {"profile", "linux-prototype-v1"},
                   {"capabilities", boost::json::array{"replay", "orbit", "zoom", "preset", "pause",
                                                       "step", "copied_rgba"}}},
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

ServerIO::ServerIO(const std::filesystem::path &dir)
    : control(io), data(io), signals(io, SIGINT, SIGTERM), directory(dir)
{
    std::filesystem::create_directories(dir);
    if (std::filesystem::exists(dir / "control.sock") || std::filesystem::exists(dir / "data.sock"))
    {
        throw std::runtime_error("IPC paths already exist; select a fresh --ipc-dir");
    }
    bool ctl_owned = false, data_owned = false;
    try
    {
        control.open();
        control.bind(asio::local::stream_protocol::endpoint((dir / "control.sock").string()));
        ctl_owned = true;
        control.listen(2);
        data.open();
        data.bind(asio::local::stream_protocol::endpoint((dir / "data.sock").string()));
        data_owned = true;
        data.listen(2);
    }
    catch (...)
    {
        if (ctl_owned)
        {
            std::filesystem::remove(dir / "control.sock");
        }
        if (data_owned)
        {
            std::filesystem::remove(dir / "data.sock");
        }
        throw;
    }
    session = std::to_string(sv::now_ns());
    token = session + "-data";
    accept(true);
    accept(false);
    signals.async_wait([this](auto, int) { stop = true; });
    worker = std::thread([this] { io.run(); });
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
    std::filesystem::remove(directory / "control.sock");
    std::filesystem::remove(directory / "data.sock");
}

void ServerIO::accept(bool c)
{
    auto &listener = c ? control : data;
    listener.async_accept(
        [this, c](auto ec, Socket socket)
        {
            if (!ec)
            {
                auto &current = c ? ctl : video;
                if (current && !current->closed)
                {
                    boost::system::error_code ignored;
                    socket.close(ignored);
                }
                else
                {
                    if (c)
                    {
                        session = std::to_string(sv::now_ns());
                        token = session + "-data";
                    }
                    current = std::make_shared<Connection>(std::move(socket), *this, c);
                    current->start();
                }
            }
            if (!stop)
            {
                accept(c);
            }
        });
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
        bool looping = true;
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
                looping = v == "true";
            }
            else
            {
                throw std::runtime_error("unknown argument " + key);
            }
        }
        if (cfg.empty() || manifest.empty() || ipc.empty())
        {
            throw std::runtime_error("usage: sv-server --config FILE --manifest FILE --ipc-dir DIR "
                                     "[--trace FILE --loop true|false]");
        }
        auto c = sv::load_config(cfg);
        auto rows = sv::load_manifest(manifest, c);
        sv::Renderer renderer(c);
        sv::Synchronizer sync(c);
        ServerIO network(ipc);
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
        size_t row = 0;
        uint64_t next = sv::now_ns(), last_render = 0;
        bool paused = false, step = false, dirty = true;
        sv::FrameSet last_set;
        std::array<std::filesystem::path, 4> cached_paths;
        std::array<std::shared_ptr<const sv::Image>, 4> cached_images;
        while (!network.stop)
        {
            for (auto &cmd : network.take())
            {
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
                    if (type == "orbit")
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
                        paused = true;
                    }
                    else if (type == "resume")
                    {
                        paused = false;
                        next = sv::now_ns();
                    }
                    else if (type == "step")
                    {
                        paused = true;
                        step = true;
                    }
                    else
                    {
                        accepted = false;
                        reason = "unknown_command";
                    }
                    if (candidate.eye().z <= c.surface.H + .1)
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
                if (accepted)
                {
                    view = candidate;
                    revision++;
                    applied_command = sv::parse_decimal_u64(id);
                    dirty = true;
                }
                network.answer(cmd, {21,
                                     {{"command_id", id},
                                      {"accepted", accepted},
                                      {"reason", reason},
                                      {"state_revision", std::to_string(revision)}},
                                     {}});
                record({{"event", "command"},
                        {"command_id", id},
                        {"accepted", accepted},
                        {"state_revision", std::to_string(revision)}});
            }
            uint64_t now = sv::now_ns();
            if ((!paused && now >= next) || step)
            {
                const auto &r = rows[row];
                for (int k = 0; k < 4; k++)
                {
                    if (!r.paths[k].empty())
                    {
                        if (cached_paths[k] != r.paths[k])
                        {
                            cached_images[k] =
                                std::make_shared<sv::Image>(sv::read_image(r.paths[k]));
                            cached_paths[k] = r.paths[k];
                        }
                        int64_t t = static_cast<int64_t>(now) + r.offset_ns[k];
                        if (t < 0)
                        {
                            throw std::runtime_error("negative runtime time");
                        }
                        sync.push({k, sequence, static_cast<uint64_t>(t), r.scenario_ns,
                                   cached_images[k]});
                    }
                }
                sequence++;
                last_set = sync.select(now);
                record({{"event", "frame_set"},
                        {"health", last_set.health},
                        {"skew_ns", std::to_string(last_set.skew_ns)},
                        {"sequence", std::to_string(sequence)}});
                row++;
                if (row == rows.size())
                {
                    row = 0;
                    if (!looping)
                    {
                        paused = true;
                    }
                }
                next = now + 33333333;
                step = false;
                dirty = true;
            }
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
                        oldest = std::min(oldest, last_set.frames[k]->release_ns);
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
                     {"stride_bytes", c.width * 4},
                     {"row_origin", "top_left"},
                     {"health", last_set.health},
                     {"paused", paused},
                     {"inputs", inputs},
                     {"clock_domain", "local_monotonic"},
                     {"oldest_release_timestamp_ns", std::to_string(oldest)},
                     {"render_complete_timestamp_ns", std::to_string(done)},
                     {"render_readback_ms", double(done - start) / 1e6},
                     {"upload_count", std::to_string(renderer.uploads())}},
                    std::move(image.pixels)};
                if (network.publish(std::move(output)))
                {
                    record({{"event", "rendered"},
                            {"frame_id", std::to_string(frame_id)},
                            {"state_revision", std::to_string(revision)},
                            {"health", last_set.health},
                            {"upload_count", std::to_string(renderer.uploads())},
                            {"render_readback_ms", double(done - start) / 1e6}});
                    dirty = false;
                    last_render = done;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        record({{"event", "shutdown"}});
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
