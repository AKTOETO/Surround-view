#include "command_parameters.hpp"
#include "experiment_recovery.hpp"
#include "output_selection.hpp"
#include "renderer_configuration.hpp"
#include "sv/build_info.hpp"
#include "sv/calibration_job.hpp"
#include "sv/config_operations.hpp"
#include "sv/config_service.hpp"
#include "sv/config_store.hpp"
#include "sv/experiment_lease.hpp"
#include "sv/fusion_runtime.hpp"
#include "sv/pipeline_spans.hpp"
#include "sv/protocol.hpp"
#include "sv/renderer.hpp"
#include "sv/server_session.hpp"
#include "sv/source.hpp"
#include "sv/telemetry.hpp"
#include "sv/vision.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/generic/stream_protocol.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cmath>
#include <csignal>
#include <deque>
#include <fstream>
#include <functional>
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

class ServerIO
{
  public:

    asio::io_context io;
    asio::local::stream_protocol::acceptor control, data;
    asio::ip::tcp::acceptor tcp_control, tcp_data;
    bool owns_control = false, owns_data = false;
    asio::signal_set signals;
    std::filesystem::path directory;
    std::thread worker;
    std::atomic<bool> stop{false}, ready{false};
    std::atomic<uint64_t> busy{0}; // Generation owning the pending/in-flight data slot.
    std::atomic<uint64_t> generation{0};

    sv::SessionRegistry sessions;
    std::shared_ptr<Connection> active_ctl;
    std::shared_ptr<Connection> active_video;
    std::mutex mutex;
    std::deque<Command> commands;
    std::string session_id, token;
    boost::json::array capabilities;
    std::function<void(const std::string &)> on_control_session_closed;

    ServerIO(const sv::Connections &connections_cfg, const std::string &source_type,
             const sv::View &default_view,
             std::function<void(const std::string &)> on_session_closed);
    ~ServerIO();

    template <class Acceptor> void accept(Acceptor &acceptor, bool is_control);
    void connected(Socket socket, bool is_control);
    void new_session(const sv::View &default_view);
    void deliver(Command cmd);
    std::vector<Command> take();
    void answer(const Command &cmd, sv::Message message);
    bool publish(sv::Message message, uint64_t expected_generation);
};

struct Connection : std::enable_shared_from_this<Connection>
{
    Socket socket;
    ServerIO &host;
    bool is_control;
    bool hello = false;
    std::atomic<bool> closed{false};
    asio::steady_timer deadline;
    asio::steady_timer release_timer;
    sv::Decoder decoder;
    std::array<unsigned char, 8192> input{};
    std::deque<std::shared_ptr<std::vector<unsigned char>>> output;
    uint64_t last_command = 0;
    std::string frame_id;
    std::string buffer_token;
    std::string bound_session_id;

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
            if (!bound_session_id.empty())
            {
                if (host.on_control_session_closed)
                {
                    host.on_control_session_closed(bound_session_id);
                }
                host.sessions.remove_session(bound_session_id);
            }
            host.ready = false;
            if (host.active_video)
            {
                host.active_video->close();
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
            if (!is_control)
            {
                std::string req_session = std::string(m.header.at("session_id").as_string());
                std::string req_token = std::string(m.header.at("data_token").as_string());
                if (!host.sessions.attach_data_channel(req_session, req_token))
                {
                    throw std::runtime_error("session handshake mismatch");
                }
                bound_session_id = req_session;
                hello = true;
                ++host.generation;
                host.ready = true;
                send({2,
                      {{"session_id", req_session},
                       {"data_token", req_token},
                       {"profile", "linux-prototype-v1"},
                       {"capabilities", host.capabilities}},
                      {}});
                return;
            }
            bound_session_id = host.session_id;
            hello = true;
            send({2,
                  {{"session_id", host.session_id},
                   {"data_token", host.token},
                   {"profile", "linux-prototype-v1"},
                   {"capabilities", host.capabilities}},
                  {}});
            return;
        }
        if (!is_control)
        {
            if (m.type != 22 || !m.payload.empty() ||
                m.header.at("session_id").as_string() != bound_session_id)
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
        message.header["session_id"] = bound_session_id;
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

void ServerIO::new_session(const sv::View &default_view)
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
    session_id = std::to_string(sv::now_ns()) + "-" + token.substr(0, 16);
    sessions.create_session(session_id, token, default_view);
}

ServerIO::ServerIO(const sv::Connections &n, const std::string &source_type,
                   const sv::View &default_view,
                   std::function<void(const std::string &)> on_session_closed)
    : control(io), data(io), tcp_control(io), tcp_data(io), signals(io, SIGINT, SIGTERM),
      directory(n.unix_directory), on_control_session_closed(std::move(on_session_closed))
{
    capabilities = {source_type,
                    "orbit",
                    "zoom",
                    "preset",
                    "pause",
                    "resume",
                    "state",
                    "config_read_v1",
                    "config_catalog_v1",
                    "config_validate_v1",
                    "config_apply_v1",
                    "config_operations_v1",
                    "telemetry_v1",
                    "source_work_timing_v1",
                    "fusion_runtime_v1",
                    "research_fusion_v1",
                    "runtime_surface_v1",
                    "experiment_lease_v1",
                    "copied_rgba",
                    "frame_view_v1",
                    "output_selection_v1",
                    "render_support_v1",
                    "source_visibility_v1",
                    "source_disagreement_v1",
                    "calibrate",
                    "calibration_provenance_v1",
                    "calibration_status",
                    "apply_calibration",
                    "cancel_calibration"};
    if (source_type == "replay")
    {
        capabilities.push_back("step");
        capabilities.push_back("experiment_step_v1");
        capabilities.push_back("replay_reset_v1");
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
            auto bind_acceptor = [&](auto &acceptor, uint16_t port)
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
            bind_acceptor(tcp_control, n.control_port);
            bind_acceptor(tcp_data, n.data_port);
        }
        new_session(default_view);
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
    if (active_ctl)
    {
        active_ctl->close();
    }
    if (active_video)
    {
        active_video->close();
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
    auto &current = c ? active_ctl : active_video;
    if (current && !current->closed)
    {
        boost::system::error_code ignored;
        socket.close(ignored);
        return;
    }
    if (c)
    {
        new_session({});
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

bool ServerIO::publish(sv::Message message, uint64_t expected_generation)
{
    uint64_t free_slot = 0;
    if (!ready || generation != expected_generation ||
        !busy.compare_exchange_strong(free_slot, expected_generation))
    {
        return false;
    }
    asio::post(io,
               [this, expected_generation, message = std::move(message)]() mutable
               {
                   if (generation == expected_generation && active_video && !active_video->closed &&
                       active_video->hello)
                   {
                       active_video->publish(std::move(message));
                   }
                   else
                   {
                       auto owner = expected_generation;
                       busy.compare_exchange_strong(owner, 0);
                   }
               });
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    try
    {
        std::string cfg_path, manifest_path, trace_path = "artifacts/server_trace.jsonl";
        std::filesystem::path ipc_path;
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
                cfg_path = v;
            }
            else if (key == "--manifest")
            {
                manifest_path = v;
            }
            else if (key == "--ipc-dir")
            {
                ipc_path = v;
            }
            else if (key == "--trace")
            {
                trace_path = v;
            }
            else if (key == "--loop")
            {
                if (v != "true" && v != "false")
                {
                    throw std::runtime_error("--loop requires true|false");
                }
                looping = (v == "true");
                loop_explicit = true;
            }
            else
            {
                throw std::runtime_error("unknown argument " + key);
            }
        }
        if (cfg_path.empty())
        {
            throw std::runtime_error("usage: sv-server --config FILE --manifest FILE --ipc-dir DIR "
                                     "[--trace FILE --loop true|false]");
        }
        auto initial_cfg = sv::load_config(cfg_path);
        if (initial_cfg.connections.explicit_config && !ipc_path.empty())
        {
            throw std::runtime_error("--ipc-dir cannot override explicit connections config");
        }
        if (!initial_cfg.connections.explicit_config && !ipc_path.empty())
        {
            initial_cfg.connections.unix_directory = ipc_path.string();
        }
        if (initial_cfg.connections.tcp_enabled)
        {
            asio::ip::make_address(initial_cfg.connections.address);
        }
        if (initial_cfg.source.explicit_config)
        {
            if (!manifest_path.empty() || loop_explicit)
            {
                throw std::runtime_error("CLI cannot override explicit source config");
            }
            if (initial_cfg.source.type == "replay")
            {
                auto path = std::filesystem::path(initial_cfg.source.manifest);
                if (path.is_relative())
                {
                    path = std::filesystem::path(cfg_path).parent_path() / path;
                }
                manifest_path = path.string();
                looping = initial_cfg.source.loop;
            }
        }
        if (initial_cfg.source.type == "replay" && manifest_path.empty())
        {
            throw std::runtime_error("replay manifest required");
        }

        sv::ConfigStore config_store(initial_cfg, std::filesystem::absolute(cfg_path));
        sv::ConfigService config_service(config_store);
        unsigned char instance_bytes[16];
        if (RAND_bytes(instance_bytes, sizeof(instance_bytes)) != 1)
        {
            throw std::runtime_error("configuration_instance_random_failed");
        }
        std::string instance_id;
        constexpr char instance_hex[] = "0123456789abcdef";
        for (auto byte : instance_bytes)
        {
            instance_id += instance_hex[byte >> 4];
            instance_id += instance_hex[byte & 15];
        }
        sv::ConfigOperations config_operations(std::move(instance_id));
        sv::TelemetryCollector telemetry(config_operations.instance_id(), initial_cfg.telemetry);
        sv::CalibrationJobManager calib_jobs;

        std::unique_ptr<sv::FrameSource> source;
        if (initial_cfg.source.type == "socket")
        {
            source = sv::make_socket_source(*config_store.active());
        }
        else if (initial_cfg.source.type == "camera")
        {
            source = sv::make_camera_source(*config_store.active(), telemetry.source_session());
        }
        else
        {
            source = sv::make_replay_source(*config_store.active(), manifest_path, looping, {},
                                            telemetry.source_session());
        }

        auto source_timing_epoch = telemetry.epoch();
        auto renderer = std::make_unique<sv::Renderer>(*config_store.active());
        sv::Synchronizer sync(*config_store.active());
        ServerIO network(config_store.active()->connections, config_store.active()->source.type,
                         config_store.active()->view, [&calib_jobs](const std::string &session_id)
                         { calib_jobs.cancel_session(session_id); });

        sv::View view = config_store.active()->view;
        if (!std::filesystem::path(trace_path).parent_path().empty())
        {
            std::filesystem::create_directories(std::filesystem::path(trace_path).parent_path());
        }
        std::ofstream log(trace_path);
        if (!log)
        {
            throw std::runtime_error("cannot open trace");
        }
        auto record = [&](boost::json::object obj)
        {
            obj["timestamp_ns"] = std::to_string(sv::now_ns());
            log << boost::json::serialize(obj) << '\n';
        };
        record({{"event", "startup"},
                {"gl_renderer", renderer->device()},
                {"profile", config_store.active()->profile_id}});

        uint64_t frame_id = 0, sequence = 0, applied_command = 0, state_revision = 0;
        uint64_t last_generation = 0, last_render = 0, source_request = 0;
        bool paused = false, dirty = true;
        sv::OutputProduct output_product = sv::OutputProduct::FinalFrame;
        uint64_t output_generation = 0;
        sv::PipelineSpanTracker span_tracker;
        sv::FrameSet last_set;
        sv::ExperimentLease experiment;
        sv::ExperimentRecovery recovery;
        std::optional<Command> pending_source;
        std::deque<Command> pending_commands;

        auto answer = [&](const Command &cmd, bool accepted, const std::string &reason,
                          boost::json::object extra = {})
        {
            const auto id = cmd.message.header.at("command_id");
            boost::json::object hdr{
                {"command_id", id},
                {"accepted", accepted},
                {"reason", reason},
                {"state_revision", std::to_string(state_revision)},
                {"paused", paused},
                {"azimuth_rad", view.azimuth},
                {"elevation_rad", view.elevation},
                {"distance_m", view.distance},
                {"fusion_mode", config_store.active()->fusion.mode},
                {"diagnostic_view", config_store.active()->fusion.diagnostic},
                {"output_product", sv::output_product_name(output_product)},
                {"config_revision", std::to_string(config_store.revision())},
                {"source_type", config_store.active()->source.type},
                {"experiment_lease", experiment.status()},
                {"fusion", sv::fusion_settings(config_store.active()->fusion)},
                {"surface", config_store.active()->effective.as_object().at("surface")}};
            for (auto &kv : extra)
            {
                hdr[kv.key()] = kv.value();
            }
            network.answer(cmd, {21, std::move(hdr), {}});
            record({{"event", "command"},
                    {"command_id", id},
                    {"accepted", accepted},
                    {"state_revision", std::to_string(state_revision)}});
        };

        while (!network.stop)
        {
            const auto generation = network.generation.load();
            if (generation != last_generation && network.ready)
            {
                if (output_generation != generation)
                {
                    output_product = sv::OutputProduct::FinalFrame;
                    output_generation = generation;
                }
                dirty = true;
                last_generation = generation;
            }
            const auto poll_start = sv::now_ns();
            auto events = source->poll();
            const auto poll_end = sv::now_ns();
            for (auto &event : events)
            {
                if (event.kind == sv::SourceEvent::Kind::Failure)
                {
                    throw std::runtime_error("FrameSource: " + event.reason);
                }
                if (event.kind == sv::SourceEvent::Kind::Reset)
                {
                    sync.reset_history();
                    last_set = {};
                    paused = true;
                    dirty = true;
                    record({{"event", "replay_reset_boundary"},
                            {"request_id", std::to_string(event.request_id)}});
                }
                else if (event.kind == sv::SourceEvent::Kind::Frames)
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
                else if (recovery.handles(event))
                {
                    paused = event.paused;
                    recovery.accept(experiment, event);
                    ++state_revision;
                    dirty = true;
                    record({{"event", "experiment_restore"}, {"status", experiment.status()}});
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
                    applied_command = sv::parse_decimal_u64(
                        std::string(pending_source->message.header.at("command_id").as_string()));
                    ++state_revision;
                    dirty = true;
                    answer(*pending_source, true, "ok");
                    pending_source.reset();
                }
            }
            if (experiment.state() == sv::ExperimentLease::State::Active &&
                (experiment.expired(sv::now_ns()) ||
                 !network.sessions.find_session(experiment.owner())))
            {
                recovery.start(experiment, sv::now_ns());
                record({{"event", "experiment_restore_started"}, {"status", experiment.status()}});
            }
            const auto before_recovery = config_store.revision();
            const auto before_recovery_state = experiment.state();
            recovery.tick(experiment,
                          {config_store, *renderer, *source, paused, source_request,
                           pending_source.has_value()},
                          sv::now_ns());
            if (experiment.state() == sv::ExperimentLease::State::Failed && pending_source)
            {
                answer(*pending_source, false, "experiment_restore_failed");
                pending_source
                    .reset(); // Keep read-only status reachable after a hung source control.
            }
            if (before_recovery != config_store.revision() ||
                before_recovery_state != experiment.state())
            {
                ++state_revision;
                dirty = true;
                record({{"event", "experiment_restore"}, {"status", experiment.status()}});
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
                boost::json::object extra_res;
                auto candidate = view;
                std::string id, type;
                std::optional<sv::SourceAction> source_action;
                bool fatal_renderer_error = false;
                bool operation_replayed = false;
                try
                {
                    id = std::string(m.header.at("command_id").as_string());
                    type = std::string(m.header.at("type").as_string());
                    auto number = [&](const char *k)
                    { return sv::command_parameters::finite_number(m.header.at(k)); };
                    const auto *lease_value = m.header.if_contains("lease_id");
                    const std::string lease_id =
                        lease_value ? std::string(lease_value->as_string()) : "";
                    if (type != "config_apply_once" &&
                        !experiment.allows(type, origin->bound_session_id, lease_id, sv::now_ns()))
                    {
                        throw std::runtime_error("experiment_lease_conflict");
                    }
                    if (type == "experiment_acquire")
                    {
                        if (config_store.active()->source.type != "replay")
                        {
                            throw std::runtime_error("experiment_requires_replay");
                        }
                        const auto ttl = m.header.at("ttl_ms").to_number<uint64_t>();
                        if (!m.header.at("ttl_ms").is_int64() && !m.header.at("ttl_ms").is_uint64())
                        {
                            throw std::runtime_error("experiment_ttl_integer_required");
                        }
                        accepted =
                            experiment.acquire(origin->bound_session_id, config_store.active(),
                                               paused, sv::now_ns(), ttl, reason);
                        if (accepted)
                        {
                            reason = "ok";
                            recovery.reset();
                        }
                    }
                    else if (type == "experiment_renew")
                    {
                        accepted = experiment.renew(origin->bound_session_id, lease_id,
                                                    sv::now_ns(), reason);
                        if (accepted)
                        {
                            reason = "ok";
                        }
                    }
                    else if (type == "experiment_release")
                    {
                        recovery.start(experiment, sv::now_ns());
                    }
                    else if (type == "state")
                    {
                    }
                    else if (type == "config_read")
                    {
                        auto snapshot = config_service.read();
                        snapshot["server_instance_id"] = config_operations.instance_id();
                        extra_res["configuration"] = std::move(snapshot);
                    }
                    else if (type == "config_catalog")
                    {
                        extra_res["configuration_catalog"] = config_service.catalog();
                    }
                    else if (type == "config_validate")
                    {
                        const auto revision = sv::parse_decimal_u64(
                            std::string(m.header.at("base_config_revision").as_string()));
                        extra_res["configuration_validation"] = config_service.validate(
                            revision, m.header.at("changes").as_object(), view, output_product);
                    }
                    else if (type == "config_apply")
                    {
                        const auto revision = sv::parse_decimal_u64(
                            std::string(m.header.at("base_config_revision").as_string()));
                        const auto &changes = m.header.at("changes").as_object();
                        extra_res["configuration_application"] = config_service.apply(
                            revision, changes, m.header.at("persist").as_bool(), view,
                            output_product,
                            [&](const sv::Config &prepared)
                            {
                                return std::make_unique<sv::RendererConfigurationUpdate>(
                                    *renderer, prepared, changes.contains("surface"), &telemetry,
                                    changes.contains("telemetry"));
                            });
                    }
                    else if (type == "config_operation_status")
                    {
                        extra_res["configuration_operation"] = config_operations.status(
                            std::string(m.header.at("server_instance_id").as_string()),
                            std::string(m.header.at("operation_id").as_string()));
                    }
                    else if (type == "config_apply_once")
                    {
                        const std::string instance(m.header.at("server_instance_id").as_string());
                        const std::string operation(m.header.at("operation_id").as_string());
                        const auto applied_id = sv::parse_decimal_u64(id);
                        sv::ConfigOperations::Request request{
                            sv::parse_decimal_u64(
                                std::string(m.header.at("base_config_revision").as_string())),
                            m.header.at("changes").as_object(), m.header.at("persist").as_bool()};
                        const auto execution = config_operations.execute(
                            instance, operation, std::move(request),
                            [&](const sv::ConfigOperations::Request &requested)
                            {
                                if (!experiment.allows("config_apply", origin->bound_session_id, "",
                                                       sv::now_ns()))
                                {
                                    throw std::runtime_error("experiment_lease_conflict");
                                }
                                (void)config_service.apply(
                                    requested.base_revision, requested.changes, requested.persist,
                                    view, output_product,
                                    [&](const sv::Config &prepared)
                                    {
                                        return std::make_unique<sv::RendererConfigurationUpdate>(
                                            *renderer, prepared,
                                            requested.changes.contains("surface"), &telemetry,
                                            requested.changes.contains("telemetry"));
                                    });
                                applied_command = applied_id;
                                ++state_revision;
                                dirty = true;
                            });
                        operation_replayed = execution.replayed;
                        accepted =
                            execution.entry.state == sv::ConfigOperations::Entry::State::Committed;
                        reason = accepted ? "ok" : execution.entry.reason;
                        extra_res["configuration_operation"] =
                            config_operations.status(instance, operation, operation_replayed);
                    }
                    else if (type == "telemetry_catalog")
                    {
                        extra_res["telemetry_catalog"] = telemetry.catalog();
                    }
                    else if (type == "telemetry_read")
                    {
                        const auto &limit = m.header.at("limit");
                        if (!limit.is_int64() && !limit.is_uint64())
                        {
                            throw std::invalid_argument("telemetry_limit_integer_required");
                        }
                        const auto limit_value = limit.is_int64() && limit.as_int64() < 0
                                                     ? uint64_t(0)
                                                     : limit.to_number<uint64_t>();
                        if (limit_value < 1 || limit_value > 16)
                        {
                            throw std::invalid_argument("telemetry_read_limit_out_of_range");
                        }
                        extra_res["telemetry"] = telemetry.read(
                            std::string(m.header.at("server_instance_id").as_string()),
                            sv::parse_decimal_u64(std::string(m.header.at("epoch").as_string())),
                            sv::parse_decimal_u64(
                                std::string(m.header.at("after_cursor").as_string())),
                            unsigned(limit_value));
                    }
                    else if (type == "fusion_catalog")
                    {
                        auto catalog = sv::fusion_catalog();
                        catalog["backend"] = "opengl_es3";
                        catalog["gl_renderer"] = renderer->device();
                        catalog["gl_vendor"] = renderer->vendor();
                        catalog["source_revision"] = sv::source_revision;
                        catalog["source_fingerprint"] = sv::source_fingerprint;
                        extra_res["fusion_catalog"] = std::move(catalog);
                    }
                    else if (type == "output_catalog")
                    {
                        extra_res["output_catalog"] = sv::output_catalog();
                    }
                    else if (type == "configure_output")
                    {
                        const auto product = sv::parse_output_product(
                            std::string(m.header.at("product").as_string()));
                        if (!network.ready)
                        {
                            throw std::runtime_error("output_requires_data_channel");
                        }
                        if (product != sv::OutputProduct::FinalFrame &&
                            config_store.active()->fusion.diagnostic != "color")
                        {
                            throw std::runtime_error(product == sv::OutputProduct::SourceVisibility
                                                         ? "source_visibility_requires_color"
                                                     : product ==
                                                             sv::OutputProduct::SourceDisagreement
                                                         ? "source_disagreement_requires_color"
                                                         : "render_support_requires_color");
                        }
                        output_product = product;
                        output_generation = network.generation.load();
                    }
                    else if (type == "surface_catalog")
                    {
                        extra_res["surface_catalog"] = sv::surface_catalog();
                    }
                    else if (type == "configure_surface")
                    {
                        const auto revision = sv::parse_decimal_u64(
                            std::string(m.header.at("base_config_revision").as_string()));
                        auto effective = config_store.active()->effective;
                        effective.as_object()["surface"] = m.header.at("surface").as_object();
                        const auto parsed = sv::parse_config(effective);
                        if (!sv::safe_view(parsed.surface, view))
                        {
                            throw std::runtime_error("view_clearance");
                        }
                        accepted = config_store.update_runtime_if_revision(
                            effective, revision, reason, [&](const sv::Config &candidate_config)
                            { renderer->set_surface(candidate_config.surface); });
                        if (accepted)
                        {
                            reason = "ok";
                        }
                    }
                    else if (type == "configure_fusion")
                    {
                        const auto revision = sv::parse_decimal_u64(
                            std::string(m.header.at("base_config_revision").as_string()));
                        auto effective = config_store.active()->effective;
                        // Full replacement avoids retaining parameters from a previous trial.
                        effective.as_object()["fusion"] = m.header.at("fusion").as_object();
                        auto prepared = sv::parse_config(effective).fusion;
                        if (output_product != sv::OutputProduct::FinalFrame &&
                            prepared.diagnostic != "color")
                        {
                            throw std::runtime_error(
                                output_product == sv::OutputProduct::SourceVisibility
                                    ? "source_visibility_requires_color"
                                : output_product == sv::OutputProduct::SourceDisagreement
                                    ? "source_disagreement_requires_color"
                                    : "render_support_requires_color");
                        }
                        if (!config_store.update_runtime_if_revision(effective, revision, reason))
                        {
                            accepted = false;
                        }
                        else
                        {
                            renderer->set_fusion(std::move(prepared));
                            reason = "ok";
                        }
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
                    else if (type == "replay_reset")
                    {
                        source_action = sv::SourceAction::ReplayReset;
                    }
                    else if (type == "calibrate")
                    {
                        const int cam_id =
                            sv::command_parameters::camera_index(m.header.at("camera_id"));
                        const auto &pts_arr = m.header.at("points").as_array();
                        const auto &pix_arr = m.header.at("pixels").as_array();
                        if (!m.header.contains("validation_points") ||
                            !m.header.contains("validation_pixels"))
                        {
                            throw std::runtime_error(
                                "independent validation correspondences required");
                        }
                        const auto &validation_pts_arr =
                            m.header.at("validation_points").as_array();
                        const auto &validation_pix_arr =
                            m.header.at("validation_pixels").as_array();
                        if (pts_arr.size() != pix_arr.size() || pts_arr.size() < 6 ||
                            validation_pts_arr.size() != validation_pix_arr.size() ||
                            validation_pts_arr.size() < 6)
                        {
                            throw std::runtime_error(
                                "at least 6 training and validation pairs required");
                        }
                        std::vector<sv::Vec3> points;
                        std::vector<sv::Pixel> pixels;
                        std::vector<sv::Vec3> validation_points;
                        std::vector<sv::Pixel> validation_pixels;
                        for (size_t i = 0; i < pts_arr.size(); ++i)
                        {
                            const auto &pt = pts_arr[i].as_array();
                            const auto &px = pix_arr[i].as_array();
                            if (pt.size() != 3 || px.size() != 2)
                            {
                                throw std::runtime_error(
                                    "invalid training correspondence dimensions");
                            }
                            points.push_back({sv::command_parameters::finite_number(pt[0]),
                                              sv::command_parameters::finite_number(pt[1]),
                                              sv::command_parameters::finite_number(pt[2])});
                            pixels.push_back({sv::command_parameters::finite_number(px[0]),
                                              sv::command_parameters::finite_number(px[1]), true});
                        }
                        for (size_t i = 0; i < validation_pts_arr.size(); ++i)
                        {
                            const auto &pt = validation_pts_arr[i].as_array();
                            const auto &px = validation_pix_arr[i].as_array();
                            if (pt.size() != 3 || px.size() != 2)
                            {
                                throw std::runtime_error(
                                    "invalid validation correspondence dimensions");
                            }
                            validation_points.push_back(
                                {sv::command_parameters::finite_number(pt[0]),
                                 sv::command_parameters::finite_number(pt[1]),
                                 sv::command_parameters::finite_number(pt[2])});
                            validation_pixels.push_back(
                                {sv::command_parameters::finite_number(px[0]),
                                 sv::command_parameters::finite_number(px[1]), true});
                        }
                        if (!m.header.contains("provenance"))
                        {
                            throw std::runtime_error("calibration_provenance_required");
                        }
                        const auto provenance =
                            sv::parse_calibration_provenance(m.header.at("provenance"));
                        sv::ExtrinsicOptions opts;
                        if (m.header.contains("method"))
                        {
                            opts.method = std::string(m.header.at("method").as_string());
                        }
                        const auto [active_config, base_config_revision] = config_store.snapshot();
                        std::string calib_job_id = calib_jobs.submit_job(
                            origin->bound_session_id, base_config_revision, cam_id,
                            active_config->cameras[cam_id], points, pixels, validation_points,
                            validation_pixels, opts, provenance);
                        extra_res["job_id"] = calib_job_id;
                        extra_res["validation_policy"] = sv::calibration_validation_policy;
                    }
                    else if (type == "calibration_status")
                    {
                        std::string calib_job_id = std::string(m.header.at("job_id").as_string());
                        auto job_opt = calib_jobs.get_job(calib_job_id, origin->bound_session_id);
                        if (!job_opt)
                        {
                            accepted = false;
                            reason = "job_not_found";
                        }
                        else
                        {
                            extra_res["job_id"] = calib_job_id;
                            extra_res["base_config_revision"] =
                                std::to_string(job_opt->base_config_revision);
                            extra_res["job_state"] = sv::to_string(job_opt->state);
                            const auto &audit = job_opt->split_audit;
                            extra_res["validation_policy"] = sv::calibration_validation_policy;
                            extra_res["dataset_id"] = audit.dataset_id;
                            extra_res["training_observations"] = audit.training_observations;
                            extra_res["validation_observations"] = audit.validation_observations;
                            extra_res["training_frames"] = audit.training_frames;
                            extra_res["validation_frames"] = audit.validation_frames;
                            if (job_opt->state == sv::JobState::Completed)
                            {
                                extra_res["training_rmse_px"] =
                                    job_opt->calibration.training_rmse_px;
                                extra_res["validation_rmse_px"] = job_opt->validation_rmse_px;
                                extra_res["validation_max_error_px"] =
                                    job_opt->validation_max_error_px;
                                extra_res["quality_accepted"] = job_opt->quality_accepted;
                                extra_res["inliers"] =
                                    static_cast<int64_t>(job_opt->calibration.inliers);
                            }
                            else if (job_opt->state == sv::JobState::Failed)
                            {
                                extra_res["error"] = job_opt->error_message;
                            }
                        }
                    }
                    else if (type == "cancel_calibration")
                    {
                        std::string calib_job_id = std::string(m.header.at("job_id").as_string());
                        if (!calib_jobs.cancel_job(calib_job_id, origin->bound_session_id))
                        {
                            accepted = false;
                            reason = "job_not_found_or_not_cancellable";
                        }
                    }
                    else if (type == "apply_calibration")
                    {
                        std::string calib_job_id = std::string(m.header.at("job_id").as_string());
                        std::string apply_err;
                        const auto [active_config, active_config_revision] =
                            config_store.snapshot();
                        auto config_candidate = calib_jobs.config_for_job(
                            calib_job_id, origin->bound_session_id, active_config_revision,
                            *active_config, apply_err);
                        if (!config_candidate)
                        {
                            accepted = false;
                            reason = apply_err;
                        }
                        else
                        {
                            // Prepare all fallible runtime state before persisting the new config.
                            // If renderer construction or persistence fails, the active pair
                            // remains unchanged.
                            auto candidate_renderer =
                                std::make_unique<sv::Renderer>(*config_candidate);
                            if (!config_store.update_if_revision(*config_candidate,
                                                                 active_config_revision, apply_err))
                            {
                                accepted = false;
                                reason = apply_err == "stale_config_revision"
                                             ? apply_err
                                             : "config_persist_failed:" + apply_err;
                            }
                            else
                            {
                                renderer.swap(candidate_renderer);
                                ++state_revision;
                                dirty = true;
                            }
                        }
                    }
                    else
                    {
                        accepted = false;
                        reason = "unknown_command";
                    }
                    if (!sv::safe_view(config_store.active()->surface, candidate))
                    {
                        accepted = false;
                        reason = "view_clearance";
                    }
                }
                catch (const std::exception &e)
                {
                    if (fatal_renderer_error)
                    {
                        throw;
                    }
                    accepted = false;
                    reason = e.what();
                }
                if (accepted && source_action)
                {
                    if ((*source_action == sv::SourceAction::Step ||
                         *source_action == sv::SourceAction::ReplayReset) &&
                        config_store.active()->source.type != "replay")
                    {
                        accepted = false;
                        reason = type == "step" ? "step_unsupported_for_source"
                                                : "replay_reset_unsupported_for_source";
                    }
                    else if (source->request(*source_action, ++source_request))
                    {
                        pending_source = std::move(cmd);
                        continue;
                    }
                    else
                    {
                        accepted = false;
                        reason = "source_control_queue_full";
                    }
                }
                if (accepted && type != "state" && type != "config_read" &&
                    type != "config_catalog" && type != "config_validate" &&
                    type != "config_operation_status" && type != "config_apply_once" &&
                    type != "telemetry_catalog" && type != "telemetry_read" &&
                    type != "fusion_catalog" && type != "output_catalog" &&
                    type != "surface_catalog" && type != "experiment_acquire" &&
                    type != "experiment_renew" && type != "experiment_release" &&
                    type != "calibrate" && type != "calibration_status" &&
                    type != "apply_calibration")
                {
                    view = candidate;
                    applied_command = sv::parse_decimal_u64(id);
                    ++state_revision;
                    dirty = true;
                }
                answer(cmd, accepted, reason, extra_res);
            }
            if (source_timing_epoch != telemetry.epoch())
            {
                source->set_timing_session(telemetry.source_session());
                source_timing_epoch = telemetry.epoch();
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
                const auto render_generation = network.generation.load();
                if (render_generation != last_generation)
                {
                    continue; // Apply/reset the product before rendering for a new attachment.
                }
                const auto source_stats = source->stats();
                auto start = sv::now_ns();
                sv::RenderInspection inspection;
                sv::RenderInspectionRequest inspection_request;
                inspection_request.support = output_product == sv::OutputProduct::RenderSupport;
                inspection_request.source_disagreement =
                    output_product == sv::OutputProduct::SourceDisagreement;
                inspection_request.source_visibility =
                    output_product == sv::OutputProduct::SourceVisibility;
                auto image = renderer->render(
                    last_set, view,
                    output_product != sv::OutputProduct::FinalFrame ? &inspection : nullptr,
                    inspection_request);
                auto done = sv::now_ns();
                frame_id++;
                boost::json::array inputs;
                uint64_t oldest = done;
                for (int k = 0; k < 4; k++)
                {
                    boost::json::object input{
                        {"camera_id", k},
                        {"used", bool(last_set.frames[k])},
                        {"calibration_id", config_store.active()->cameras[k].calibration_id}};
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
                        if (frame.camera_capture)
                        {
                            input["camera_capture"] =
                                sv::camera_frame_provenance_json(*frame.camera_capture);
                        }
                        oldest = std::min(oldest, frame.release_ns);
                    }
                    inputs.push_back(input);
                }
                const auto timing = renderer->last_timing();
                const double render_wall_ms = static_cast<double>(done - start) / 1e6;
                sv::FrameTelemetry ft;
                ft.frame_id = frame_id;
                ft.sequence_id = sequence;
                ft.config_revision = config_store.revision();
                ft.timestamp_ns = done;
                ft.source_poll_ms = static_cast<double>(poll_end - poll_start) / 1e6;
                ft.pre_render_prepare_ms = static_cast<double>(start - poll_end) / 1e6;
                ft.render_wall_ms = render_wall_ms;
                ft.gpu_draw_ms = timing.gpu_draw_ms;
                ft.fusion_cpu_ms = timing.fusion_cpu_ms;
                ft.layer_readback_cpu_ms = timing.layer_readback_cpu_ms;
                ft.upload_cpu_ms = timing.upload_cpu_ms;
                ft.readback_copy_cpu_ms = timing.readback_copy_cpu_ms;
                ft.total_pipeline_ms = static_cast<double>(done - oldest) / 1e6;
                sv::Message output{
                    11,
                    {{"frame_id", std::to_string(frame_id)},
                     {"frame_set_id", std::to_string(sequence)},
                     {"config_revision", std::to_string(config_store.revision())},
                     {"fusion", sv::fusion_settings(config_store.active()->fusion)},
                     {"surface", config_store.active()->effective.as_object().at("surface")},
                     {"view",
                      boost::json::object{
                          {"azimuth_rad", view.azimuth},
                          {"elevation_rad", view.elevation},
                          {"distance_m", view.distance},
                          {"fov_y_rad", view.fov},
                          {"clip_m", boost::json::array{view.near_z, view.far_z}},
                          {"target_m",
                           boost::json::array{view.target.x, view.target.y, view.target.z}}}},
                     {"fusion_backend", sv::research_fusion(config_store.active()->fusion.mode)
                                            ? "gles_projection_cpu_fusion_v1"
                                            : "gles_local_fusion_v1"},
                     {"gpu_timer_status", timing.gpu_timer_status},
                     {"state_revision", std::to_string(state_revision)},
                     {"applied_command_id", std::to_string(applied_command)},
                     {"buffer_token", std::to_string(done) + ":" + std::to_string(frame_id)},
                     {"width", config_store.active()->width},
                     {"height", config_store.active()->height},
                     {"pixel_format", "RGBA8"},
                     {"fusion_mode", config_store.active()->fusion.mode},
                     {"diagnostic_view", config_store.active()->fusion.diagnostic},
                     {"stride_bytes", config_store.active()->width * 4},
                     {"row_origin", "top_left"},
                     {"health", last_set.health},
                     {"paused", paused},
                     {"inputs", inputs},
                     {"clock_domain", "local_monotonic"},
                     {"oldest_release_timestamp_ns", std::to_string(oldest)},
                     {"render_complete_timestamp_ns", std::to_string(done)},
                     {"render_readback_ms", render_wall_ms},
                     {"server_receive_to_render_ms", ft.total_pipeline_ms},
                     {"previous_frames_median_server_receive_to_render_ms",
                      span_tracker.median_total_latency_ms()},
                     {"pipeline_spans_ms", ft.to_json().at("spans_ms")},
                     {"source_type", config_store.active()->source.type},
                     {"timestamp_basis", "server_delivery"},
                     {"source_received", std::to_string(source_stats.received)},
                     {"source_rejected", std::to_string(source_stats.rejected)},
                     {"decode_count", std::to_string(source_stats.decoded)},
                     {"source_dropped_batches", std::to_string(source_stats.dropped)},
                     {"source_queue_depth", source_stats.queued_batches},
                     {"mesh_build_count", std::to_string(renderer->mesh_builds())},
                     {"mesh_triangles", renderer->triangles()},
                     {"mesh_resources", renderer->mesh_resources()},
                     {"upload_count", std::to_string(renderer->uploads())}},
                    std::move(image.pixels)};

                output.header["output_product"] = sv::output_product_name(output_product);
                if (output_product != sv::OutputProduct::FinalFrame)
                {
                    const auto &mask = inspection_request.support ? inspection.support
                                       : inspection_request.source_visibility
                                           ? inspection.source_visibility
                                           : inspection.source_disagreement;
                    if (mask.type() != CV_8UC4 || mask.rows != image.height ||
                        mask.cols != image.width || !mask.isContinuous())
                    {
                        throw std::runtime_error("invalid native render support mask");
                    }
                    output.payload.assign(mask.data, mask.data + mask.total() * mask.elemSize());
                    if (inspection_request.support)
                    {
                        output.header["support_schema"] = sv::render_support_schema();
                    }
                    else if (inspection_request.source_visibility)
                    {
                        output.header["source_visibility_schema"] = sv::source_visibility_schema();
                    }
                    else
                    {
                        output.header["source_disagreement_schema"] =
                            sv::source_disagreement_schema();
                    }
                    output.header["inspection_cpu_ms"] = timing.inspection_cpu_ms;
                }

                if (timing.seam_optimization)
                {
                    const auto &seam = *timing.seam_optimization;
                    output.header["seam_optimization"] =
                        boost::json::object{{"implementation", "integer_potts_v1"},
                                            {"initial_energy", seam.initial_energy},
                                            {"final_energy", seam.final_energy},
                                            {"nodes", seam.nodes},
                                            {"edges", seam.edges},
                                            {"sweeps", seam.sweeps},
                                            {"accepted_moves", seam.accepted_moves},
                                            {"converged", seam.converged},
                                            {"max_sweeps", 8}};
                }
                const auto publish_start = sv::now_ns();
                const bool published = network.publish(std::move(output), render_generation);
                ft.publish_enqueue_ms = static_cast<double>(sv::now_ns() - publish_start) / 1e6;
                span_tracker.record_frame(ft);
                telemetry.frame(ft, &last_set);
                record({{"event", "pipeline_span"},
                        {"frame_id", std::to_string(frame_id)},
                        {"telemetry", ft.to_json()}});
                if (published)
                {
                    record({{"event", "rendered"},
                            {"frame_id", std::to_string(frame_id)},
                            {"state_revision", std::to_string(state_revision)},
                            {"health", last_set.health},
                            {"source_type", config_store.active()->source.type},
                            {"timestamp_basis", "server_delivery"},
                            {"source_received", std::to_string(source_stats.received)},
                            {"source_rejected", std::to_string(source_stats.rejected)},
                            {"decode_count", std::to_string(source_stats.decoded)},
                            {"source_dropped_batches", std::to_string(source_stats.dropped)},
                            {"source_queue_depth", source_stats.queued_batches},
                            {"mesh_build_count", std::to_string(renderer->mesh_builds())},
                            {"upload_count", std::to_string(renderer->uploads())},
                            {"render_readback_ms", render_wall_ms},
                            {"server_receive_to_render_ms", ft.total_pipeline_ms},
                            {"server_receive_to_render_median_ms",
                             span_tracker.median_total_latency_ms()}});
                    dirty = false;
                    last_render = done;
                }
            }
            if (telemetry.enabled())
            {
                const auto sample_now = sv::now_ns();
                if (telemetry.due(sample_now))
                {
                    telemetry.sample(sample_now, config_store.revision(), source->stats(), paused,
                                     network.ready.load(), network.busy.load() != 0);
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
