#include "configuration_editor.hpp"
#include "telemetry_model.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <algorithm>
#include <gtest/gtest.h>

extern sv::client::Endpoint simulator_runtime_endpoint;

struct TelemetryModelTestAccess
{
    static void catalog(TelemetryModel &model, uint64_t epoch = 0)
    {
        sv::client::TelemetryCatalog catalog;
        catalog.server_instance_id = "instance";
        catalog.epoch = epoch;
        catalog.settings = {true, 50, 8};
        model.acceptCatalog(catalog);
    }

    static void batch(TelemetryModel &model, uint64_t first, uint64_t count, uint64_t lost = 0)
    {
        sv::client::TelemetryBatch batch;
        batch.server_instance_id = "instance";
        batch.epoch = model.catalog_->epoch;
        batch.after_cursor = model.cursor_;
        batch.lost_samples = lost;
        for (uint64_t i = first; i < first + count; ++i)
        {
            sv::client::TelemetrySample sample;
            sample.cursor = i;
            sample.timestamp_ns = i * 50000000ULL;
            sample.process.cpu_percent = 200;
            sample.process.rss_bytes = 1048576;
            sample.frame.emplace();
            sample.frame->fresh = i % 2 == 1;
            sample.frame->spans_ms["render_wall"] = double(i);
            batch.samples.push_back(sample);
        }
        batch.next_cursor = first + count - 1;
        model.acceptBatch(batch);
    }

    static void accept(TelemetryModel &model, const sv::client::TelemetryBatch &batch)
    {
        model.acceptBatch(batch);
    }

    static void pendingRead(TelemetryModel &model)
    {
        model.read_id_ = 7;
    }

    static bool pending(const TelemetryModel &model)
    {
        return model.read_id_ != 0;
    }

    static uint64_t cursor(const TelemetryModel &model)
    {
        return model.cursor_;
    }
};

TEST(TelemetryUnit, TelemetryBoundsFreshnessQuantilesAndGaps)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    for (uint64_t first = 1; first <= 130; first += 16)
    {
        TelemetryModelTestAccess::batch(model, first, std::min<uint64_t>(16, 131 - first));
    }
    ASSERT_EQ(model.points().size(), 120);
    EXPECT_DOUBLE_EQ(model.points().first().toMap().value("rss").toDouble(), 1);
    EXPECT_DOUBLE_EQ(model.points().last().toMap().value("cpu").toDouble(), 200);
    EXPECT_FALSE(model.points().last().toMap().value("render").isValid());
    EXPECT_TRUE(model.summary().contains("60 свежих"));
    EXPECT_TRUE(model.summary().contains("69.00 / 123.00 / 129.00"));
    TelemetryModelTestAccess::batch(model, 134, 1, 3);
    EXPECT_TRUE(model.points().last().toMap().value("gap").toBool());
    EXPECT_TRUE(model.summary().contains("ring: 3"));
    model.clear();
    EXPECT_TRUE(model.points().empty());
    EXPECT_EQ(TelemetryModelTestAccess::cursor(model), 134U);
}

TEST(TelemetryUnit, TelemetryEpochAndDisconnectResetWindow)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(model, 1, 3);
    TelemetryModelTestAccess::catalog(model); // Same settings refresh keeps history.
    EXPECT_EQ(model.points().size(), 3);
    TelemetryModelTestAccess::catalog(model, 1);
    EXPECT_TRUE(model.points().empty());
    EXPECT_EQ(TelemetryModelTestAccess::cursor(model), 0U);
    TelemetryModelTestAccess::batch(model, 1, 2);
    model.attach({});
    EXPECT_TRUE(model.points().empty());
}

TEST(TelemetryUnit, TelemetryRejectsWrongSeriesAndBackwardsTime)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    sv::client::TelemetryBatch foreign;
    foreign.server_instance_id = "foreign";
    EXPECT_THROW(TelemetryModelTestAccess::accept(model, foreign), std::runtime_error);
    TelemetryModelTestAccess::batch(model, 1, 4);
    EXPECT_THROW(TelemetryModelTestAccess::batch(model, 1, 1), std::runtime_error);
}

TEST(TelemetryUnit, TelemetryCorrelatesTransportFailuresAndIgnoresUnrelatedReplies)
{
    TelemetryModel model;
    TelemetryModelTestAccess::pendingRead(model);
    model.commandFailed({{"command_id", "8"}}, "unrelated");
    EXPECT_TRUE(TelemetryModelTestAccess::pending(model));
    model.consume({21, {{"command_id", "8"}}, {}});
    EXPECT_TRUE(TelemetryModelTestAccess::pending(model));
    model.commandFailed({{"command_id", "7"}}, "not_ready_or_queue_full");
    EXPECT_FALSE(TelemetryModelTestAccess::pending(model));
    EXPECT_TRUE(model.status().contains("не подтверждено"));
}

namespace
{
bool waitForTelemetry(const std::function<bool()> &done)
{
    QElapsedTimer timer;
    timer.start();
    while (!done() && timer.elapsed() < 5000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(2);
    }
    return done();
}
} // namespace

TEST(SimulatorRuntime, TelemetryEditorHotApplyAndTypedPolling)
{
    ConfigurationEditor editor;
    TelemetryModel model;
    sv::client::Options options;
    options.endpoint = simulator_runtime_endpoint;
    QString failure;
    std::shared_ptr<sv::client::Client> client;
    client = std::make_shared<sv::client::Client>(
        options,
        [&](sv::client::Event event)
        {
            QMetaObject::invokeMethod(
                &model,
                [&, event = std::move(event)]
                {
                    try
                    {
                        if (event.kind == sv::client::Event::Kind::State && event.detail == "ready")
                        {
                            editor.connected();
                            model.connected();
                        }
                        if (event.kind == sv::client::Event::Kind::Error && event.message)
                        {
                            editor.commandFailed(event.message->header, event.detail);
                            model.commandFailed(event.message->header, event.detail);
                        }
                        if (event.kind == sv::client::Event::Kind::Message)
                        {
                            editor.consume(*event.message);
                            model.consume(*event.message);
                            if (event.message->type == 11)
                            {
                                client->release(event.message->header);
                            }
                        }
                    }
                    catch (const std::exception &error)
                    {
                        failure = error.what();
                    }
                },
                Qt::QueuedConnection);
        });
    editor.attach(client);
    model.attach(client);
    ASSERT_TRUE(waitForTelemetry([&] { return editor.canApply() || !failure.isEmpty(); }));
    ASSERT_TRUE(failure.isEmpty()) << failure.toStdString();
    const auto original = editor.telemetry();
    ASSERT_FALSE(editor.catalog().value("telemetry").toMap().empty());
    editor.setField("telemetry", "enabled", true);
    editor.setField("telemetry", "period_ms", 50);
    editor.setField("telemetry", "history_capacity", 8);
    editor.validate();
    ASSERT_TRUE(waitForTelemetry([&] { return !editor.busy(); }));
    ASSERT_TRUE(editor.validated()) << editor.status().toStdString();
    editor.apply(false);
    ASSERT_TRUE(waitForTelemetry([&] { return !editor.busy(); }));
    ASSERT_FALSE(editor.unresolved());
    model.setMonitoring(true);
    ASSERT_TRUE(waitForTelemetry([&] { return !model.points().empty() || !failure.isEmpty(); }));
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    model.setMonitoring(false);
    for (auto it = original.begin(); it != original.end(); ++it)
    {
        editor.setField("telemetry", it.key(), it.value());
    }
    editor.apply(false);
    ASSERT_TRUE(waitForTelemetry([&] { return !editor.busy(); }));
    EXPECT_EQ(editor.telemetry(), original);
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    client->stop();
    QCoreApplication::processEvents();
}
