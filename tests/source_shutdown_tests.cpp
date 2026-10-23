#include "sv/source.hpp"
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace
{
class SourceShutdown : public testing::Test
{
  protected:

    std::filesystem::path root;
    sv::Config config;

    void SetUp() override
    {
        char name[] = "/tmp/sv-source-shutdown-XXXXXX";
        const auto path = ::mkdtemp(name);
        ASSERT_NE(path, nullptr);
        root = path;
        config = sv::load_config(SV_TEST_CONFIG_PATH);
        config.queue_size = 1;
        boost::json::array ids, paths;
        for (auto &camera : config.cameras)
        {
            camera.width = camera.height = 2;
            ids.emplace_back(camera.calibration_id);
            paths.emplace_back(std::to_string(camera.id));
            auto &endpoint = config.source.cameras[camera.id];
            endpoint.transport = "unix";
            endpoint.path = (root / ("camera-" + std::to_string(camera.id))).string();
        }
        sv::write_json(root / "manifest.json",
                       {{"schema_version", 1},
                        {"calibration_ids", ids},
                        {"frames", boost::json::array{boost::json::object{
                                       {"scenario_timestamp_ns", "0"},
                                       {"paths", paths},
                                       {"offset_ns", boost::json::array{0, 0, 0, 0}}}}}});
    }

    void TearDown() override
    {
        if (!root.empty())
        {
            std::filesystem::remove_all(root);
        }
    }

    static sv::Image image()
    {
        return {2, 2, 3, std::vector<unsigned char>(12, 10)};
    }
};

struct Gate
{
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, release = false;
    std::atomic<unsigned> calls{0};

    void wait()
    {
        ++calls;
        std::unique_lock<std::mutex> lock(mutex);
        entered = true;
        changed.notify_all();
        changed.wait_for(lock, std::chrono::seconds(3), [&] { return release; });
    }
};
} // namespace

TEST_F(SourceShutdown, ConcurrentReplayStopsWaitForLoaderAndSkipFurtherCameraLoads)
{
    auto gate = std::make_shared<Gate>();
    auto source = sv::make_replay_source(config, root / "manifest.json", true,
                                         [gate](const auto &)
                                         {
                                             gate->wait();
                                             return image();
                                         });
    {
        std::unique_lock<std::mutex> lock(gate->mutex);
        ASSERT_TRUE(
            gate->changed.wait_for(lock, std::chrono::seconds(1), [&] { return gate->entered; }));
    }
    std::promise<void> started;
    auto began = started.get_future();
    auto first = std::async(std::launch::async,
                            [&]
                            {
                                started.set_value();
                                source->stop();
                            });
    began.wait();
    bool stopped = false;
    // Stay below the request queue limit: rejection must not be caused by queue saturation.
    for (unsigned index = 0; index < 50; ++index)
    {
        if (!source->request(sv::SourceAction::Pause, index))
        {
            stopped = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto second = std::async(std::launch::async, [&] { source->stop(); });
    const auto first_status = first.wait_for(std::chrono::milliseconds(20));
    const auto second_status = second.wait_for(std::chrono::milliseconds(20));
    {
        std::lock_guard<std::mutex> lock(gate->mutex);
        gate->release = true;
    }
    gate->changed.notify_all(); // Release before assertions/async destructors on failure.
    first.get();
    second.get();
    EXPECT_TRUE(stopped);
    EXPECT_EQ(first_status, std::future_status::timeout);
    EXPECT_EQ(second_status, std::future_status::timeout);
    EXPECT_EQ(gate->calls, 1U);
    EXPECT_EQ(source->stats().decoded, 0U);
    EXPECT_EQ(source->stats().received, 0U);
    EXPECT_TRUE(source->poll().empty());
    EXPECT_FALSE(source->request(sv::SourceAction::Resume, 100));
    source->stop();
}

TEST_F(SourceShutdown, ReplayQueuedControlsAndConcurrentStopsWakeDuringShutdown)
{
    for (unsigned run = 0; run < 12; ++run)
    {
        auto source = sv::make_replay_source(config, root / "manifest.json", false,
                                             [](const auto &) { return image(); });
        for (unsigned id = 0; id < 64; ++id)
        {
            source->request(sv::SourceAction::Pause, id);
        }
        std::promise<void> start;
        auto go = start.get_future().share();
        auto a = std::async(std::launch::async,
                            [&]
                            {
                                go.wait();
                                source->stop();
                            });
        auto b = std::async(std::launch::async,
                            [&]
                            {
                                go.wait();
                                source->stop();
                            });
        start.set_value();
        EXPECT_EQ(a.wait_for(std::chrono::seconds(1)), std::future_status::ready);
        EXPECT_EQ(b.wait_for(std::chrono::seconds(1)), std::future_status::ready);
        a.get();
        b.get();
        EXPECT_FALSE(source->request(sv::SourceAction::Step, 100));
    }
}

TEST_F(SourceShutdown, SocketConcurrentStopsClosePartialProducerAndPreserveReplacementPath)
{
    for (unsigned run = 0; run < 12; ++run)
    {
        auto source = sv::make_socket_source(config);
        boost::asio::io_context io;
        boost::asio::local::stream_protocol::socket producer(io);
        producer.connect(
            boost::asio::local::stream_protocol::endpoint(config.source.cameras[0].path));
        const std::string prefix = "SV01";
        boost::asio::write(producer, boost::asio::buffer(prefix));
        std::promise<void> start;
        const auto go = start.get_future().share();
        std::vector<std::future<void>> stops;
        for (unsigned index = 0; index < 6; ++index)
        {
            stops.push_back(std::async(std::launch::async,
                                       [&]
                                       {
                                           go.wait();
                                           source->stop();
                                       }));
        }
        start.set_value();
        for (auto &stop : stops)
        {
            EXPECT_EQ(stop.wait_for(std::chrono::seconds(1)), std::future_status::ready);
            EXPECT_NO_THROW(stop.get());
        }
        EXPECT_FALSE(source->request(sv::SourceAction::Pause, 1));
        for (const auto &endpoint : config.source.cameras)
        {
            EXPECT_FALSE(std::filesystem::exists(endpoint.path));
        }
        std::array<char, 16> bytes;
        boost::system::error_code error;
        producer.read_some(boost::asio::buffer(bytes), error);
        EXPECT_TRUE(error == boost::asio::error::eof ||
                    error == boost::asio::error::connection_reset);
        std::ofstream(config.source.cameras[0].path) << "replacement";
        source->stop();
        EXPECT_TRUE(std::filesystem::is_regular_file(config.source.cameras[0].path));
        std::filesystem::remove(config.source.cameras[0].path);
    }
}

TEST_F(SourceShutdown, SocketCleanupPreservesReplacementsMadeWhileListenerWasActive)
{
    for (const auto *kind : {"file", "symlink", "socket"})
    {
        SCOPED_TRACE(kind);
        auto source = sv::make_socket_source(config);
        const auto path = config.source.cameras[0].path;
        std::filesystem::remove(path);
        boost::asio::io_context io;
        boost::asio::local::stream_protocol::acceptor replacement(io);
        if (std::string_view(kind) == "socket")
        {
            replacement.open();
            replacement.bind(boost::asio::local::stream_protocol::endpoint(path));
            replacement.listen();
        }
        else if (std::string_view(kind) == "symlink")
        {
            std::ofstream(root / "target") << "preserve-target";
            std::filesystem::create_symlink(root / "target", path);
        }
        else
        {
            std::ofstream(path) << "preserve-file";
        }
        source->stop();
        source.reset(); // Destructor/repeated stop must also preserve the replacement.
        const auto status = std::filesystem::symlink_status(path);
        EXPECT_TRUE(std::filesystem::exists(status));
        if (std::string_view(kind) == "socket")
        {
            EXPECT_TRUE(std::filesystem::is_socket(status));
        }
        if (std::string_view(kind) == "symlink")
        {
            EXPECT_TRUE(std::filesystem::is_symlink(status));
        }
        if (std::string_view(kind) == "file")
        {
            EXPECT_TRUE(std::filesystem::is_regular_file(status));
        }
        boost::system::error_code ignored;
        replacement.close(ignored);
        std::filesystem::remove(path);
    }
}
