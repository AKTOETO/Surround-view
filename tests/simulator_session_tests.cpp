#include "bridge.hpp"
#include "simulator_bridge.hpp"
#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <gtest/gtest.h>

// Inject library events at the GUI boundary without exposing a product command
// for fabricating server state. Wire delivery is covered by client_transports.
struct SimulatorBridgeTestAccess
{
    static void deliver(SimulatorBridge &bridge, sv::client::Event event)
    {
        bridge.consume(std::move(event));
    }
};

struct ClientBridgeTestAccess
{
    static void deliver(Bridge &bridge, sv::client::Event event)
    {
        bridge.consume(std::move(event));
    }
};

namespace
{
sv::client::Event ack(bool job = true)
{
    boost::json::object header{{"command_id", "1"},
                               {"accepted", true},
                               {"azimuth_rad", .4},
                               {"elevation_rad", .8},
                               {"distance_m", 10.0},
                               {"state_revision", "2"},
                               {"config_revision", "1"},
                               {"fusion", boost::json::object{}},
                               {"surface", boost::json::object{}}};
    if (job)
    {
        header["job_id"] = "old-session-job";
        header["job_state"] = "completed";
    }
    return {sv::client::Event::Kind::Message,
            {},
            std::make_shared<sv::Message>(sv::Message{21, std::move(header), {}})};
}

class SimulatorSession : public ::testing::Test
{
  protected:

    QTemporaryDir directory;

    void SetUp() override
    {
        ASSERT_TRUE(directory.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory.path());
    }

    sv::client::Endpoint endpoint() const
    {
        sv::client::Endpoint result;
        result.directory = directory.path().toStdString();
        return result;
    }
};

TEST_F(SimulatorSession, ExplicitUnixEndpointReplacesSavedUiAddress)
{
    QSettings settings("MAI", "surround-view-simulator");
    settings.setValue("connection/unix_directory", "/tmp/old-server");
    SimulatorFrameProvider provider;
    SimulatorBridge bridge(endpoint(), &provider);
    EXPECT_EQ(bridge.unixDirectory(), directory.path());
    EXPECT_TRUE(bridge.status().contains(directory.path()));
    SimulatorBridgeTestAccess::deliver(bridge, ack());
    ASSERT_TRUE(bridge.runtimeSettings()->ready());
    // Reject before discarding the existing snapshot/connection or updating UI.
    bridge.connectUnix("/tmp/" + QString(81, 'x'), 2000, 1000, 0);
    EXPECT_EQ(bridge.unixDirectory(), directory.path());
    EXPECT_TRUE(bridge.runtimeSettings()->ready());
    EXPECT_TRUE(bridge.status().contains("текущее соединение сохранено"));
    EXPECT_EQ(settings.value("connection/unix_directory").toString(), "/tmp/old-server");
}

TEST_F(SimulatorSession, ExplicitTcpEndpointAndInvalidOptions)
{
    auto remote = endpoint();
    remote.transport = sv::client::Endpoint::Transport::Tcp;
    remote.host = "127.0.0.2";
    remote.control_port = 32101;
    remote.data_port = 32102;
    SimulatorFrameProvider provider;
    SimulatorBridge bridge(remote, &provider);
    EXPECT_EQ(bridge.tcpHost(), "127.0.0.2");
    EXPECT_EQ(bridge.controlPort(), 32101);
    EXPECT_EQ(bridge.dataPort(), 32102);
    bridge.connectTcp("127.0.0.3", 32103, 32104, 0, 1000, 0);
    EXPECT_EQ(bridge.tcpHost(), "127.0.0.2");
    EXPECT_EQ(bridge.controlPort(), 32101);
    EXPECT_EQ(bridge.dataPort(), 32102);
    QSettings settings("MAI", "surround-view-simulator");
    EXPECT_EQ(settings.value("connection/tcp_host").toString(), "127.0.0.2");
}

TEST_F(SimulatorSession, SessionLossAndExplicitDisconnectForgetJobAndImage)
{
    SimulatorFrameProvider provider;
    SimulatorBridge bridge(endpoint(), &provider);
    for (const bool explicitDisconnect : {false, true})
    {
        SimulatorBridgeTestAccess::deliver(bridge, ack());
        ASSERT_TRUE(bridge.calibrationStatus().contains("old-session-job"));
        QImage oldImage(2, 2, QImage::Format_RGBA8888);
        oldImage.fill(Qt::red);
        provider.setImage(oldImage);
        ASSERT_FALSE(provider.requestImage({}, nullptr, {}).isNull());
        if (explicitDisconnect)
        {
            bridge.disconnectFromServer();
        }
        else
        {
            SimulatorBridgeTestAccess::deliver(
                bridge, {sv::client::Event::Kind::State, "disconnected", {}});
        }
        EXPECT_FALSE(bridge.runtimeSettings()->ready());
        EXPECT_TRUE(provider.requestImage({}, nullptr, {}).isNull());
        EXPECT_TRUE(bridge.frameUrl().isEmpty());
        bridge.checkCalibrationStatus("");
        EXPECT_EQ(bridge.calibrationStatus(), "Нет активных калибровочных задач");
        EXPECT_FALSE(bridge.serverInfo().contains("Азимут"));
    }
}

TEST_F(SimulatorSession, TouchClientIgnoresImageCompletionFromLostSession)
{
    FrameProvider provider;
    Bridge bridge(endpoint(), &provider);
    boost::json::object header{{"width", 2},
                               {"height", 2},
                               {"frame_id", "1"},
                               {"session_id", "old-session"},
                               {"buffer_token", "old-buffer"},
                               {"health", "READY"},
                               {"paused", false},
                               {"render_readback_ms", 1.0},
                               {"server_receive_to_render_ms", 1.0},
                               {"pipeline_spans_ms", boost::json::object{{"gpu_draw", nullptr}}},
                               {"inputs", boost::json::array{}},
                               {"source_type", "replay"},
                               {"fusion_mode", "edge_feather"},
                               {"diagnostic_view", "color"},
                               {"config_revision", "7"},
                               {"state_revision", "99"}};
    ClientBridgeTestAccess::deliver(
        bridge, {sv::client::Event::Kind::Message,
                 {},
                 std::make_shared<sv::Message>(
                     sv::Message{11, header, std::vector<unsigned char>(16, 127)})});
    const auto oldUrl = bridge.frameUrl();
    ASSERT_FALSE(oldUrl.isEmpty());
    EXPECT_TRUE(bridge.sourceInfo().contains("config revision: 7"));
    bridge.imageReady(oldUrl);
    ClientBridgeTestAccess::deliver(bridge, {sv::client::Event::Kind::State, "disconnected", {}});
    EXPECT_TRUE(bridge.frameUrl().isEmpty());
    EXPECT_TRUE(provider.requestImage({}, nullptr, {}).isNull());
    bridge.imageReady(oldUrl); // Delayed QML image-ready notification.
    ::testing::internal::CaptureStdout();
    bridge.presented();
    EXPECT_TRUE(::testing::internal::GetCapturedStdout().empty());

    // The new session may reuse frame_id=1 before the old QML load completes.
    header["session_id"] = "new-session";
    ClientBridgeTestAccess::deliver(bridge, {sv::client::Event::Kind::Message,
                                             {},
                                             std::make_shared<sv::Message>(sv::Message{
                                                 11, header, std::vector<unsigned char>(16, 63)})});
    EXPECT_NE(bridge.frameUrl(), oldUrl);
    bridge.imageReady(oldUrl);
    ::testing::internal::CaptureStdout();
    bridge.presented();
    EXPECT_TRUE(::testing::internal::GetCapturedStdout().empty());
    bridge.imageReady(bridge.frameUrl());
    ::testing::internal::CaptureStdout();
    bridge.presented();
    EXPECT_NE(::testing::internal::GetCapturedStdout().find("ui_present_submit"),
              std::string::npos);
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
