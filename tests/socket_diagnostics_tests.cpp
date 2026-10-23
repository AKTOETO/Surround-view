#include "sv/client_telemetry.hpp"
#include "sv/client_telemetry_archive.hpp"
#include "sv/source.hpp"
#include "sv/telemetry.hpp"
#include <boost/asio.hpp>
#include <boost/asio/local/stream_protocol.hpp>
#include <cstdlib>
#include <gtest/gtest.h>
#include <thread>

namespace
{
class SocketDiagnostics : public testing::Test
{
  protected:

    std::filesystem::path root;
    sv::Config config;
    std::unique_ptr<sv::FrameSource> source;

    void SetUp() override
    {
        char name[] = "/tmp/sv-socket-diagnostics-XXXXXX";
        const auto path = ::mkdtemp(name);
        ASSERT_NE(path, nullptr);
        root = path;
        config = sv::load_config(SV_TEST_CONFIG_PATH);
        config.queue_size = 1;
        config.source.message_timeout_ms = 50;
        for (int id = 0; id < 4; ++id)
        {
            config.cameras[id].width = config.cameras[id].height = 2;
            config.source.cameras[id].transport = "unix";
            config.source.cameras[id].path = (root / std::to_string(id)).string();
        }
        source = sv::make_socket_source(config);
    }

    void TearDown() override
    {
        source.reset();
        if (!root.empty())
        {
            std::filesystem::remove_all(root);
        }
    }

    sv::SocketSourceDiagnostics wait(
        const std::function<bool(const sv::SocketSourceDiagnostics &)> &predicate)
    {
        const auto deadline = sv::now_ns() + 2000000000;
        while (sv::now_ns() < deadline)
        {
            const auto value = source->stats().socket_diagnostics->at(0);
            if (predicate(value))
            {
                return value;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("socket diagnostic timeout");
    }
};

class Producer
{
    boost::asio::io_context io;
    int camera_;

  public:

    boost::asio::local::stream_protocol::socket socket{io};
    std::string session;

    explicit Producer(const sv::Config &config, int camera = 0) : camera_(camera)
    {
        socket.connect(
            boost::asio::local::stream_protocol::endpoint(config.source.cameras[camera_].path));
        socket.non_blocking(true);
    }

    void send(const sv::Message &message, size_t fragment = 0)
    {
        const auto bytes = sv::encode(message);
        if (fragment)
        {
            boost::asio::write(socket, boost::asio::buffer(bytes.data(), fragment));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        boost::asio::write(socket,
                           boost::asio::buffer(bytes.data() + fragment, bytes.size() - fragment));
    }

    void hello(const sv::Config &config)
    {
        send({1,
              {{"role", "producer"},
               {"camera_id", camera_},
               {"calibration_id", config.cameras[camera_].calibration_id},
               {"width", 2},
               {"height", 2},
               {"pixel_format", "RGB8"},
               {"row_origin", "top_left"},
               {"clock_domain", "fixture"}},
              {}});
        sv::Decoder decoder;
        const auto deadline = sv::now_ns() + 2000000000;
        while (sv::now_ns() < deadline)
        {
            unsigned char bytes[4096];
            boost::system::error_code error;
            const auto count = socket.read_some(boost::asio::buffer(bytes), error);
            if (!error)
            {
                const auto messages = decoder.feed(bytes, count);
                if (!messages.empty())
                {
                    session = std::string(messages[0].header.at("session_id").as_string());
                    return;
                }
            }
            else if (error != boost::asio::error::would_block &&
                     error != boost::asio::error::try_again)
            {
                throw std::runtime_error("producer handshake closed");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        throw std::runtime_error("producer handshake timeout");
    }

    void frame(const sv::Config &config, unsigned sequence, size_t fragment = 0)
    {
        send({10,
              {{"session_id", session},
               {"camera_id", camera_},
               {"calibration_id", config.cameras[camera_].calibration_id},
               {"width", 2},
               {"height", 2},
               {"stride_bytes", 6},
               {"pixel_format", "RGB8"},
               {"row_origin", "top_left"},
               {"clock_domain", "fixture"},
               {"sequence_id", std::to_string(sequence)},
               {"source_timestamp_ns", std::to_string(sequence)},
               {"scenario_timestamp_ns", "0"}},
              std::vector<unsigned char>(12, 10)},
             fragment);
    }
};

sv::Message reply(boost::json::object fields)
{
    return {21,
            {{"accepted", true},
             {"command_id", "1"},
             {"config_revision", "0"},
             {"telemetry", std::move(fields)}},
            {}};
}
} // namespace

TEST_F(SocketDiagnostics, PauseDropsAndBoundedQueueHavePerCameraCounters)
{
    Producer producer(config);
    producer.hello(config);
    producer.frame(config, 0);
    producer.frame(config, 1);
    auto value =
        wait([](const auto &d) { return d.counters.received == 2 && d.counters.dropped == 1; });
    EXPECT_EQ(value.connection.state, "ready");
    EXPECT_EQ(value.connection.session_id, producer.session);
    EXPECT_EQ(value.counters.reconnects(), 0U);
    ASSERT_TRUE(source->request(sv::SourceAction::Pause, 100));
    bool paused = false;
    const auto deadline = sv::now_ns() + 2000000000;
    while (!paused && sv::now_ns() < deadline)
    {
        for (const auto &event : source->poll())
        {
            if (event.request_id == 100 && event.kind == sv::SourceEvent::Kind::Control)
            {
                paused = event.paused;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    ASSERT_TRUE(paused);
    producer.frame(config, 2);
    value = wait([](const auto &d) { return d.counters.received == 3 && d.counters.dropped == 2; });
    source->stop();
    value = source->stats().socket_diagnostics->at(0);
    EXPECT_EQ(value.connection.state, "disconnected");
    EXPECT_FALSE(value.connection.session_id);
    EXPECT_EQ(value.connection.last_close_reason, "source_stopped");
    EXPECT_EQ(value.counters.closed, 1U);
}

TEST_F(SocketDiagnostics, BusyRefusalProtocolRejectionAndReconnectPreserveOtherCameras)
{
    Producer first(config);
    first.hello(config);
    Producer refused(config);
    auto value = wait([](const auto &d) { return d.counters.refused == 1; });
    EXPECT_EQ(value.counters.accepted, 1U);
    EXPECT_EQ(value.connection.session_id, first.session);
    first.frame(config, 0);
    first.frame(config, 0);
    value = wait([](const auto &d) { return d.counters.rejected == 1; });
    EXPECT_EQ(value.connection.last_close_reason, "protocol_rejected");
    EXPECT_EQ(value.counters.closed, 1U);
    Producer next(config);
    next.hello(config);
    value = wait([](const auto &d) { return d.counters.handshakes == 2; });
    EXPECT_EQ(value.counters.reconnects(), 1U);
    EXPECT_NE(first.session, next.session);
    EXPECT_EQ(value.connection.session_id, next.session);
    const auto stats = source->stats();
    for (unsigned id = 1; id < 4; ++id)
    {
        EXPECT_EQ(stats.socket_diagnostics->at(id).counters.accepted, 0U);
    }
    boost::system::error_code ignored;
    next.socket.close(ignored);
    value = wait([](const auto &d) { return d.counters.closed == 2; });
    EXPECT_EQ(value.connection.last_close_reason, "peer_disconnected");
}

TEST_F(SocketDiagnostics, PartialHandshakeTimeoutDoesNotCountAsReconnect)
{
    Producer partial(config);
    const std::string bytes = "SV";
    boost::asio::write(partial.socket, boost::asio::buffer(bytes));
    const auto value = wait([](const auto &d) { return d.counters.timeouts == 1; });
    EXPECT_EQ(value.counters.accepted, 1U);
    EXPECT_EQ(value.counters.handshakes, 0U);
    EXPECT_EQ(value.counters.reconnects(), 0U);
    EXPECT_EQ(value.connection.last_close_reason, "message_timeout");
}

TEST_F(SocketDiagnostics, TypedSdkAndArchivePreserveCountersAndRejectInconsistentStates)
{
    Producer producer(config);
    producer.hello(config);
    producer.frame(config, 0);
    wait([](const auto &d) { return d.counters.received == 1; });
    sv::TelemetryCollector collector("socket-fixture", {true, 50, 16});
    collector.sample(sv::now_ns(), 0, source->stats(), true, false, false);
    auto fields = collector.read("socket-fixture", 0, 0, 16);
    const auto batch = sv::client::parse_telemetry_batch(reply(fields));
    ASSERT_TRUE(batch.samples[0].source.socket_diagnostics);
    EXPECT_EQ(batch.samples[0].source.socket_diagnostics->at(0).connection.session_id,
              producer.session);
    auto catalog_reply = reply({});
    catalog_reply.header["telemetry_catalog"] = collector.catalog();
    auto catalog = sv::client::parse_telemetry_catalog(catalog_reply);
    EXPECT_TRUE(catalog.socket_source_diagnostics_supported);
    sv::client::TelemetryArchive archive{
        catalog,      batch.samples[0].timestamp_ns, batch.latest_cursor, 0, "socket mock",
        batch.samples};
    const auto restored =
        sv::client::parse_telemetry_archive(sv::client::serialize_telemetry_archive(archive));
    EXPECT_TRUE(restored.catalog.socket_source_diagnostics_supported);
    EXPECT_EQ(restored.samples[0].source.socket_diagnostics->at(0).counters.received, 1U);
    for (unsigned kind = 0; kind < 8; ++kind)
    {
        auto changed = fields;
        auto &camera = changed.at("samples")
                           .as_array()[0]
                           .as_object()
                           .at("source")
                           .as_object()
                           .at("socket_diagnostics")
                           .as_array()[0]
                           .as_object();
        auto &counts = camera.at("counters").as_object();
        auto &connection = camera.at("connection").as_object();
        if (kind == 0)
        {
            camera["camera_id"] = 1;
        }
        if (kind == 1)
        {
            counts["closed"] = "2";
        }
        if (kind == 2)
        {
            connection["state"] = "unknown";
        }
        if (kind == 3)
        {
            connection["session_id"] = nullptr;
        }
        if (kind == 4)
        {
            counts["dropped"] = "2";
        }
        if (kind == 5)
        {
            counts["handshakes"] = "2";
        }
        if (kind == 6)
        {
            connection["last_close_reason"] = "unknown";
        }
        if (kind == 7)
        {
            camera["extra"] = true;
        }
        EXPECT_ANY_THROW(sv::client::parse_telemetry_batch(reply(changed))) << kind;
    }
    auto legacy = fields;
    legacy.at("samples").as_array()[0].as_object().at("source").as_object().erase(
        "socket_diagnostics");
    EXPECT_FALSE(
        sv::client::parse_telemetry_batch(reply(legacy)).samples[0].source.socket_diagnostics);
}

TEST(SocketDiagnosticsCodec, MaximumCountersRetainIdentityWithinPaginatedReplyBudget)
{
    sv::TelemetryCollector collector("socket-budget", {true, 50, 16});
    sv::SourceStats source;
    source.socket_diagnostics = sv::SocketSourceDiagnosticsSet{};
    for (auto &camera : *source.socket_diagnostics)
    {
        camera.counters = {UINT64_MAX, UINT64_MAX, UINT64_MAX, UINT64_MAX,
                           0,          UINT64_MAX, UINT64_MAX, UINT64_MAX};
        camera.connection.last_close_reason = "message_timeout";
    }
    for (uint64_t index = 0; index < 16; ++index)
    {
        collector.sample(1000000000 + index * 50000000, 0, source, false, false, false);
    }
    uint64_t cursor = 0;
    unsigned count = 0;
    bool more;
    do
    {
        const auto fields = collector.read("socket-budget", 0, cursor, 16);
        EXPECT_LE(boost::json::serialize(fields).size(), sv::max_header / 2);
        const auto batch = sv::client::parse_telemetry_batch(reply(fields));
        ASSERT_FALSE(batch.samples.empty());
        EXPECT_EQ(batch.samples[0].source.socket_diagnostics->at(0).counters.reconnects(),
                  UINT64_MAX - 1);
        count += batch.samples.size();
        cursor = batch.next_cursor;
        more = batch.has_more;
    } while (more);
    EXPECT_EQ(count, 16U);
}

TEST_F(SocketDiagnostics, DiagnosticsSurviveSaturatedStatusMailbox)
{
    for (unsigned index = 1; index <= 20; ++index)
    {
        Producer malformed(config);
        malformed.send({1, {{"role", "invalid"}}, {}});
        const auto value = wait([index](const auto &d) { return d.counters.rejected == index; });
        EXPECT_EQ(value.counters.closed, index);
    }
    const auto events = source->poll();
    unsigned statuses = 0;
    for (const auto &event : events)
    {
        statuses += event.kind == sv::SourceEvent::Kind::Status;
    }
    EXPECT_EQ(statuses, 16U);
    const auto stats = source->stats();
    EXPECT_EQ(stats.socket_diagnostics->at(0).counters.rejected, 20U);
    EXPECT_EQ(stats.socket_diagnostics->at(0).connection.last_close_reason, "protocol_rejected");
}

TEST_F(SocketDiagnostics, CompletedFragmentsCancelDeadlineWhilePartialFrameTimesOutOnlyItsCamera)
{
    source.reset();
    config.source.message_timeout_ms = 200;
    source = sv::make_socket_source(config);
    Producer camera0(config), camera1(config, 1);
    camera0.hello(config);
    camera1.hello(config);
    for (unsigned sequence = 0; sequence < 4; ++sequence)
    {
        camera0.frame(config, sequence, sequence + 1);
        wait([sequence](const auto &d) { return d.counters.received == sequence + 1; });
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    auto stats = source->stats();
    EXPECT_EQ(stats.socket_diagnostics->at(0).connection.state, "ready");
    EXPECT_EQ(stats.socket_diagnostics->at(0).counters.timeouts, 0U);
    EXPECT_EQ(stats.socket_diagnostics->at(1).connection.state, "ready");
    const std::string partial = "SV";
    boost::asio::write(camera0.socket, boost::asio::buffer(partial));
    auto closed = wait([](const auto &d) { return d.counters.timeouts == 1; });
    EXPECT_EQ(closed.connection.last_close_reason, "message_timeout");
    EXPECT_EQ(source->stats().socket_diagnostics->at(1).counters.timeouts, 0U);
    Producer recovered(config);
    recovered.hello(config);
    recovered.frame(config, 0);
    const auto ready = wait([](const auto &d) { return d.counters.received == 5; });
    EXPECT_EQ(ready.connection.state, "ready");
    EXPECT_EQ(ready.counters.reconnects(), 1U);
    EXPECT_EQ(ready.counters.timeouts, 1U);
}
