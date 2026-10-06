#include "sv/protocol.hpp"
#include "sv/source.hpp"
#include <atomic>
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>
#include <unistd.h>

namespace
{
void check(bool ok, const char *what)
{
    if (!ok)
    {
        throw std::runtime_error(what);
    }
}

std::vector<sv::SourceEvent> wait(sv::FrameSource &source, uint64_t id)
{
    std::vector<sv::SourceEvent> result;
    const auto deadline = sv::now_ns() + 2000000000;
    while (sv::now_ns() < deadline)
    {
        for (auto &e : source.poll())
        {
            const bool done = e.kind == sv::SourceEvent::Kind::Control && e.request_id == id;
            result.push_back(std::move(e));
            if (done)
            {
                return result;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    throw std::runtime_error("source completion timeout");
}
} // namespace

int main(int argc, char **argv)
{
    std::filesystem::path directory;
    try
    {
        check(argc == 2, "config required");
        auto config = sv::load_config(argv[1]);
        char temporary[] = "/tmp/sv-source-XXXXXX";
        auto path = mkdtemp(temporary);
        check(path != nullptr, "temporary directory");
        directory = path;
        boost::json::array ids, paths;
        for (auto &camera : config.cameras)
        {
            ids.emplace_back(camera.calibration_id);
            paths.emplace_back(std::to_string(camera.id));
        }
        boost::json::array rows;
        for (int i = 0; i < 8; ++i)
        {
            rows.push_back({{"scenario_timestamp_ns", std::to_string(i * 1000000)},
                            {"paths", paths},
                            {"offset_ns", {0, 0, 0, 0}}});
        }
        auto manifest = directory / "manifest.json";
        sv::write_json(manifest,
                       {{"schema_version", 1}, {"calibration_ids", ids}, {"frames", rows}});
        // Source selection is strict and may not silently enable an unintended transport.
        auto value = config.effective;
        value.as_object()["source"] = {
            {"type", "replay"}, {"manifest", "manifest.json"}, {"loop", false}};
        check(sv::parse_config(value).source.explicit_config, "explicit replay config");
        boost::json::array endpoints;
        for (int i = 0; i < 4; ++i)
        {
            endpoints.push_back({{"camera_id", i},
                                 {"transport", "unix"},
                                 {"path", (directory / std::to_string(i)).string()}});
        }
        value.as_object()["source"] = {{"type", "socket"}, {"cameras", endpoints}};
        check(sv::parse_config(value).source.type == "socket", "socket config");
        auto rejects = [&](boost::json::value invalid)
        {
            bool rejected = false;
            try
            {
                sv::parse_config(invalid);
            }
            catch (const std::exception &)
            {
                rejected = true;
            }
            check(rejected, "invalid source accepted");
        };
        auto invalid = value;
        invalid.as_object().at("source").as_object()["type"] = "v4l2";
        rejects(invalid);
        invalid = value;
        invalid.as_object().at("source").as_object().at("cameras").as_array()[1] = endpoints[0];
        rejects(invalid);
        invalid = value;
        invalid.as_object().at("source").as_object()["message_timeout_ms"] = 0;
        rejects(invalid);
        invalid = value;
        invalid.as_object()
            .at("source")
            .as_object()
            .at("cameras")
            .as_array()[0]
            .as_object()["path"] = "relative.sock";
        rejects(invalid);
        std::mutex mutex;
        std::condition_variable condition;
        bool entered = false, release = false;
        std::atomic<unsigned> calls{0};
        auto loader = [&](const std::filesystem::path &p)
        {
            ++calls;
            std::unique_lock<std::mutex> lock(mutex);
            entered = true;
            condition.notify_all();
            condition.wait(lock, [&] { return release; });
            auto &camera = config.cameras.at(std::stoi(p.filename().string()));
            return sv::Image{camera.width, camera.height, 3,
                             std::vector<unsigned char>(camera.width * camera.height * 3, 123)};
        };
        auto source = sv::make_replay_source(config, manifest, true, loader);
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!condition.wait_for(lock, std::chrono::seconds(2), [&] { return entered; }))
            {
                release = true;
                condition.notify_all();
                throw std::runtime_error("worker did not enter loader");
            }
        }
        // The worker is deliberately blocked in decode: polling/control submission must finish.
        auto begin = sv::now_ns();
        auto events = source->poll();
        const bool submitted = source->request(sv::SourceAction::Pause, 1);
        const auto elapsed = sv::now_ns() - begin;
        {
            std::lock_guard<std::mutex> lock(mutex);
            release = true;
        }
        condition.notify_all();
        check(submitted && elapsed < 100000000, "GL-facing API blocked on decoding");
        events = wait(*source, 1);
        check(events.back().paused, "pause barrier");
        const auto decoded = calls.load();
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        check(source->poll().empty() && calls == decoded, "decode continued after pause ACK");
        check(source->request(sv::SourceAction::Step, 2), "step submission");
        events = wait(*source, 2);
        unsigned frames = 0;
        for (const auto &event : events)
        {
            if (event.kind == sv::SourceEvent::Kind::Frames)
            {
                ++frames;
            }
        }
        check(frames == 1 && events.back().paused && calls == 4, "step/cache semantics");
        check(source->request(sv::SourceAction::Resume, 3), "resume submission");
        wait(*source, 3);
        std::this_thread::sleep_for(std::chrono::milliseconds(40));
        auto stats = source->stats();
        check(stats.queued_batches <= size_t(config.queue_size) && stats.dropped > 0,
              "bounded replay mailbox");
        source->stop();
        check(!source->request(sv::SourceAction::Step, 4), "stopped source accepted request");
        auto failed =
            sv::make_replay_source(config, manifest, false, [](const auto &) -> sv::Image
                                   { throw std::runtime_error("injected decode failure"); });
        bool failure = false;
        auto deadline = sv::now_ns() + 2000000000;
        while (!failure && sv::now_ns() < deadline)
        {
            for (const auto &event : failed->poll())
            {
                failure |= event.kind == sv::SourceEvent::Kind::Failure;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(failure && !failed->request(sv::SourceAction::Resume, 5),
              "decode failure propagation");
        failed->stop();
        // Saturate all camera mailboxes without polling: each retains its own newest Q frames.
        config.source.type = "socket";
        config.source.message_timeout_ms = 100;
        for (int i = 0; i < 4; ++i)
        {
            config.source.cameras[i].path = (directory / ("camera" + std::to_string(i))).string();
        }
        auto sockets = sv::make_socket_source(config);
        boost::asio::io_context io;
        const int sent_per_camera = config.queue_size + 7;
        const uint64_t total_sent = uint64_t(4 * sent_per_camera);
        for (int i = 0; i < 4; ++i)
        {
            boost::asio::local::stream_protocol::socket socket(io);
            socket.connect(
                boost::asio::local::stream_protocol::endpoint(config.source.cameras[i].path));
            const auto &camera = config.cameras[i];
            auto message = sv::encode({1,
                                       {{"role", "producer"},
                                        {"camera_id", i},
                                        {"calibration_id", camera.calibration_id},
                                        {"width", camera.width},
                                        {"height", camera.height},
                                        {"pixel_format", "RGB8"},
                                        {"row_origin", "top_left"},
                                        {"clock_domain", "test"}},
                                       {}});
            boost::asio::write(socket, boost::asio::buffer(message));
            sv::Decoder decoder;
            std::vector<sv::Message> reply;
            std::array<unsigned char, 4096> bytes{};
            while (reply.empty())
            {
                reply = decoder.feed(bytes.data(), socket.read_some(boost::asio::buffer(bytes)));
            }
            auto session = reply.front().header.at("session_id");
            for (int seq = 0; seq < sent_per_camera; ++seq)
            {
                message =
                    sv::encode({10,
                                {{"session_id", session},
                                 {"camera_id", i},
                                 {"calibration_id", camera.calibration_id},
                                 {"width", camera.width},
                                 {"height", camera.height},
                                 {"stride_bytes", camera.width * 3},
                                 {"pixel_format", "RGB8"},
                                 {"row_origin", "top_left"},
                                 {"clock_domain", "test"},
                                 {"sequence_id", std::to_string(seq)},
                                 {"source_timestamp_ns", std::to_string(seq)},
                                 {"scenario_timestamp_ns", "0"}},
                                std::vector<unsigned char>(camera.width * camera.height * 3, 123)});
                boost::asio::write(socket, boost::asio::buffer(message));
            }
        }
        deadline = sv::now_ns() + 2000000000;
        while (sv::now_ns() < deadline)
        {
            const auto current = sockets->stats();
            if (current.received == total_sent &&
                current.queued_batches == size_t(4 * config.queue_size))
            {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        stats = sockets->stats();
        check(stats.received == total_sent &&
                  stats.queued_batches == size_t(4 * config.queue_size) &&
                  stats.dropped == total_sent - uint64_t(4 * config.queue_size),
              "per-camera bounded mailbox");
        std::array<int, 4> counts{};
        for (const auto &event : sockets->poll())
        {
            if (event.kind == sv::SourceEvent::Kind::Frames)
            {
                ++counts[event.camera_id];
                check(event.frames[event.camera_id]->source_sequence >=
                          uint64_t(sent_per_camera - config.queue_size),
                      "old camera frame survived saturation");
            }
        }
        for (auto count : counts)
        {
            check(count == config.queue_size, "camera starvation");
        }
        begin = sv::now_ns();
        sockets->stop();
        check(sv::now_ns() - begin < 1000000000, "socket source shutdown deadline");
        for (auto &endpoint : config.source.cameras)
        {
            check(!std::filesystem::exists(endpoint.path), "socket cleanup");
        }
        std::filesystem::remove_all(directory);
        std::cout << "Replay worker, pause barrier, step/cache, bounded mailbox and failure checks "
                     "passed\n";
        return 0;
    }
    catch (const std::exception &e)
    {
        if (!directory.empty())
        {
            std::filesystem::remove_all(directory);
        }
        std::cerr << e.what() << '\n';
        return 1;
    }
}
