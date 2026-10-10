#include "runtime_settings.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <gtest/gtest.h>

namespace
{
sv::client::Endpoint endpoint;

bool wait_until(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
    }
    return done();
}

TEST(SimulatorRuntime, AppliesRejectsAndRestoresThroughClientLibrary)
{
    RuntimeSettings settings;
    sv::client::Options options;
    options.endpoint = endpoint;
    std::shared_ptr<sv::client::Client> client;
    client = std::make_shared<sv::client::Client>(
        options,
        [&](sv::client::Event event)
        {
            QMetaObject::invokeMethod(
                &settings,
                [&, event = std::move(event)]
                {
                    if (event.kind == sv::client::Event::Kind::State && event.detail == "ready")
                    {
                        settings.refresh();
                    }
                    if (event.kind == sv::client::Event::Kind::Error && event.message)
                    {
                        settings.commandFailed(event.message->header, event.detail);
                    }
                    if (event.kind == sv::client::Event::Kind::Message)
                    {
                        if (event.message->type == 21)
                        {
                            settings.consume(event.message->header);
                        }
                        if (event.message->type == 11)
                        {
                            client->release(event.message->header);
                        }
                    }
                },
                Qt::QueuedConnection);
        });
    settings.attach(client);
    ASSERT_TRUE(wait_until(
        [&] { return settings.ready() && settings.catalogJson().contains("surface_catalog"); }));
    const auto original = settings.fusionJson();
    const auto surface = settings.surfaceJson();
    const auto initial_revision = settings.revision();
    auto fusion = boost::json::parse(original.toStdString()).as_object();
    fusion["diagnostic"] = "weights";
    fusion["mode"] = "multi_band";
    fusion["pyramid_boundary"] = "normalized";
    settings.applyFusion(QString::fromStdString(boost::json::serialize(fusion)), initial_revision);
    ASSERT_TRUE(settings.pending());
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_NE(settings.revision(), initial_revision);
    EXPECT_EQ(boost::json::parse(settings.fusionJson().toStdString()).at("diagnostic"), "weights");
    EXPECT_EQ(boost::json::parse(settings.fusionJson().toStdString()).at("pyramid_boundary"),
              "normalized");
    const auto updated_revision = settings.revision();

    settings.applyFusion(original, initial_revision);
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_TRUE(settings.status().contains("stale_config_revision"))
        << settings.status().toStdString();
    EXPECT_EQ(settings.revision(), updated_revision);

    fusion["mode"] = "not_an_algorithm";
    settings.applyFusion(QString::fromStdString(boost::json::serialize(fusion)), updated_revision);
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_TRUE(settings.status().contains("Отклонено сервером"));
    EXPECT_EQ(settings.revision(), updated_revision);

    fusion["pyramid_levels"] = 2.5;
    settings.applyFusion(QString::fromStdString(boost::json::serialize(fusion)), updated_revision);
    EXPECT_FALSE(settings.pending());
    EXPECT_TRUE(settings.status().contains("unsigned_integer"));
    EXPECT_EQ(settings.revision(), updated_revision);

    auto changed_surface = boost::json::parse(surface.toStdString()).as_object();
    changed_surface["type"] = "unknown_carrier";
    settings.applySurface(QString::fromStdString(boost::json::serialize(changed_surface)),
                          updated_revision);
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_TRUE(settings.status().contains("Отклонено сервером"));
    EXPECT_EQ(settings.surfaceJson(), surface);
    EXPECT_EQ(settings.revision(), updated_revision);

    changed_surface = boost::json::parse(surface.toStdString()).as_object();
    changed_surface["uniform_cells"] = boost::json::array{24, 24};
    settings.applySurface(QString::fromStdString(boost::json::serialize(changed_surface)),
                          updated_revision);
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_NE(settings.revision(), updated_revision);
    EXPECT_EQ(boost::json::parse(settings.surfaceJson().toStdString()), changed_surface);

    settings.applySurface(surface, settings.revision());
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_EQ(settings.surfaceJson(), surface);

    settings.applyFusion(original, settings.revision());
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_EQ(settings.fusionJson(), original);
    client->stop();
    settings.attach({});
    EXPECT_FALSE(settings.ready());
    EXPECT_TRUE(settings.revision().isEmpty());
}

TEST(SimulatorRuntimeUnit, LocalCommandFailureEndsPendingAndDoesNotClaimServerRejection)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    RuntimeSettings settings;
    sv::client::Options options;
    options.endpoint.directory = directory.path().toStdString();
    options.max_retries = 0;
    bool exhausted = false;
    auto client = std::make_shared<sv::client::Client>(
        options,
        [&](sv::client::Event event)
        {
            QMetaObject::invokeMethod(
                &settings,
                [&, event = std::move(event)]
                {
                    if (event.kind == sv::client::Event::Kind::State &&
                        event.detail == "retry_exhausted")
                    {
                        exhausted = true;
                    }
                    if (event.kind == sv::client::Event::Kind::Error && event.message)
                    {
                        settings.commandFailed(event.message->header, event.detail);
                    }
                },
                Qt::QueuedConnection);
        });
    settings.attach(client);
    ASSERT_TRUE(wait_until([&] { return exhausted; }));
    // Inject a previously read snapshot, then exercise an actual library-local
    // rejection (no live session), not a synthetic server ACK.
    const boost::json::object fusion{{"mode", "edge_feather"},  {"diagnostic", "color"},
                                     {"edge_width_px", 24.0},   {"angle_power", 2.0},
                                     {"smoothness_weight", .1}, {"pyramid_levels", 4}};
    settings.consume(
        {{"config_revision", "1"}, {"fusion", fusion}, {"surface", boost::json::object{}}});
    const auto original = settings.fusionJson();
    settings.applyFusion(original, "1");
    ASSERT_TRUE(settings.pending());
    settings.commandFailed({{"command_id", "999"}}, "unrelated_error");
    EXPECT_TRUE(settings.pending());
    ASSERT_TRUE(wait_until([&] { return !settings.pending(); }));
    EXPECT_TRUE(settings.status().contains("not_ready_or_queue_full"));
    EXPECT_TRUE(settings.status().contains("не подтверждён"));
    EXPECT_EQ(settings.revision(), "1");
    EXPECT_EQ(settings.fusionJson(), original);
    client->stop();
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const bool unit = argc == 2 && std::string(argv[1]) == "--unit";
    if (!unit && argc == 3 && std::string(argv[1]) == "--unix")
    {
        endpoint.directory = argv[2];
    }
    else if (argc == 5 && std::string(argv[1]) == "--tcp")
    {
        endpoint.transport = sv::client::Endpoint::Transport::Tcp;
        endpoint.host = argv[2];
        endpoint.control_port = static_cast<uint16_t>(std::stoul(argv[3]));
        endpoint.data_port = static_cast<uint16_t>(std::stoul(argv[4]));
    }
    else if (!unit)
    {
        return 2;
    }
    argc = 1;
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) = unit ? "SimulatorRuntimeUnit.*" : "SimulatorRuntime.*";
    return RUN_ALL_TESTS();
}
