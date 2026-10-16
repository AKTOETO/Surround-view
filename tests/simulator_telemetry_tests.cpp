#include "configuration_editor.hpp"
#include "telemetry_model.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTemporaryDir>
#include <QThread>
#include <algorithm>
#include <functional>
#include <gtest/gtest.h>

extern sv::client::Endpoint simulator_runtime_endpoint;

struct TelemetryModelTestAccess
{
    static void catalog(TelemetryModel &model, uint64_t epoch = 0)
    {
        sv::client::TelemetryCatalog catalog;
        catalog.server_instance_id = "instance";
        catalog.clock_domain = "server_steady_clock_ns";
        catalog.cpu_percent_basis = "one_logical_cpu_100_percent";
        catalog.epoch = epoch;
        catalog.settings = {true, 50, 8};
        model.acceptCatalog(catalog);
    }

    static void batch(TelemetryModel &model, uint64_t first, uint64_t count, uint64_t lost = 0,
                      const std::function<void(sv::client::TelemetrySample &)> &modify = {})
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
            for (const auto *span :
                 {"source_poll", "pre_render_prepare", "render_wall", "fusion_cpu",
                  "layer_readback_cpu", "upload_cpu", "readback_copy_cpu", "publish_enqueue"})
            {
                sample.frame->spans_ms[span] = 0;
            }
            sample.frame->spans_ms["gpu_draw"] = nullptr;
            sample.frame->spans_ms["render_wall"] = double(i);
            if (modify)
            {
                modify(sample);
            }
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

TEST(TelemetryUnit, PipelineSpansOnlyUseFreshFramesAndKeepTinyDurations)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(
        model, 1, 3, 0,
        [](auto &sample)
        {
            sample.frame->spans_ms["fusion_cpu"] = double(sample.cursor) * .000001;
            sample.frame->spans_ms["gpu_draw"] =
                sample.cursor == 1 ? boost::json::value(.25) : boost::json::value(nullptr);
            sample.frame->input_age_at_render_ms = 42;
        });
    const auto rows = model.points();
    EXPECT_DOUBLE_EQ(rows[0].toMap().value("fusion_cpu").toDouble(), .000001);
    EXPECT_FALSE(rows[1].toMap().value("fusion_cpu").isValid());
    EXPECT_FALSE(rows[1].toMap().value("input_age_at_render").isValid());
    EXPECT_FALSE(rows[2].toMap().value("gpu_draw").isValid());
    EXPECT_DOUBLE_EQ(rows[0].toMap().value("input_age_at_render").toDouble(), 42);
    EXPECT_TRUE(model.metricSummary("fusion_cpu").contains("n=2"));
    EXPECT_TRUE(model.metricSummary("gpu_draw").contains("n=1"));
    EXPECT_TRUE(model.metricSummary("revision").contains("Нет пригодных"));
    EXPECT_EQ(model.pipelineMetrics().size(), 10);
}

TEST(TelemetryUnit, DeliveryGaugesAndRecordAgeContinueForRepeatedFrames)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(model, 1, 2, 0,
                                    [](auto &sample)
                                    {
                                        sample.paused = true;
                                        sample.data_ready = true;
                                        sample.data_busy = true;
                                        sample.frame->fresh = false;
                                        sample.frame->timestamp_ns = 10000000;
                                        sample.collection_wall_ms = .012;
                                        sample.source.queued_batches =
                                            sample.cursor == 1 ? (uint64_t(1) << 53) + 1 : 2;
                                        sample.source.received = UINT64_MAX;
                                    });
    const auto first = model.points().first().toMap();
    const auto last = model.points().last().toMap();
    EXPECT_FALSE(first.value("queued_batches").isValid());
    EXPECT_DOUBLE_EQ(last.value("queued_batches").toDouble(), 2);
    EXPECT_DOUBLE_EQ(first.value("frame_record_age").toDouble(), 40);
    EXPECT_DOUBLE_EQ(last.value("frame_record_age").toDouble(), 90);
    EXPECT_DOUBLE_EQ(last.value("data_busy").toDouble(), 1);
    EXPECT_DOUBLE_EQ(last.value("paused").toDouble(), 1);
    EXPECT_DOUBLE_EQ(last.value("collection_wall").toDouble(), .012);
    EXPECT_FALSE(last.value("render_wall").isValid());
    EXPECT_TRUE(model.sourceDetails().contains(QString::number(UINT64_MAX)));
    model.clear();
    EXPECT_TRUE(model.sourceDetails().contains("Нет source"));
    EXPECT_EQ(model.sourceMetrics().size(), 42);
}

TEST(TelemetryUnit, NoRenderRecordProducesMissingFrameMetrics)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(model, 1, 1, 0, [](auto &sample) { sample.frame.reset(); });
    const auto first = model.points().first().toMap();
    EXPECT_FALSE(first.value("frame_record_age").isValid());
    EXPECT_FALSE(first.value("source_poll").isValid());
    EXPECT_TRUE(first.value("queued_batches").isValid());
    EXPECT_TRUE(model.metricSummary("source_poll").contains("Нет пригодных"));
}

TEST(TelemetryUnit, FileRoundTripPreservesWindowAndInvalidImportIsAtomic)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto url = QUrl::fromLocalFile(directory.filePath("window.json"));
    TelemetryModel live;
    TelemetryModelTestAccess::catalog(live);
    TelemetryModelTestAccess::batch(live, 1, 4);
    TelemetryModelTestAccess::batch(live, 7, 2, 2);
    ASSERT_TRUE(live.saveReport(url, "window test")) << live.status().toStdString();
    TelemetryModel restored;
    ASSERT_TRUE(restored.loadReport(url)) << restored.status().toStdString();
    EXPECT_TRUE(restored.offline());
    EXPECT_EQ(restored.points(), live.points());
    EXPECT_EQ(restored.summary(), live.summary());
    const auto before = restored.points();
    QFile file(url.toLocalFile());
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("{broken");
    file.close();
    EXPECT_FALSE(restored.loadReport(url));
    EXPECT_EQ(restored.points(), before);
    EXPECT_TRUE(restored.offline());
    restored.setMonitoring(true);
    EXPECT_FALSE(restored.monitoring());
    restored.attach({});
    EXPECT_FALSE(restored.offline());
    EXPECT_TRUE(restored.points().empty());
}

TEST(TelemetryUnit, TruncatedWindowRoundTripPreservesAbsoluteOriginAndTotalLoss)
{
    QTemporaryDir directory;
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    for (uint64_t first = 1; first <= 130; first += 16)
    {
        TelemetryModelTestAccess::batch(model, first, std::min<uint64_t>(16, 131 - first));
    }
    const auto url = QUrl::fromLocalFile(directory.filePath("truncated.json"));
    ASSERT_TRUE(model.saveReport(url, "retained window")) << model.status().toStdString();
    TelemetryModel restored;
    ASSERT_TRUE(restored.loadReport(url)) << restored.status().toStdString();
    EXPECT_EQ(restored.points(), model.points());
    EXPECT_EQ(restored.summary(), model.summary());
    EXPECT_GT(restored.points().first().toMap().value("seconds").toDouble(), 0);
}

TEST(TelemetryUnit, LocalClearDoesNotReexportOldSamplesAndWriteFailurePreservesGraph)
{
    QTemporaryDir directory;
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(model, 1, 3);
    const auto before = model.points();
    EXPECT_FALSE(
        model.saveReport(QUrl::fromLocalFile(directory.path()), "cannot replace directory"));
    EXPECT_EQ(model.points(), before);
    EXPECT_FALSE(model.loadReport(QUrl("https://example.invalid/report.json")));
    model.clear();
    const auto url = QUrl::fromLocalFile(directory.filePath("empty.json"));
    ASSERT_TRUE(model.saveReport(url, ""));
    TelemetryModel restored;
    ASSERT_TRUE(restored.loadReport(url));
    EXPECT_TRUE(restored.points().empty());
    EXPECT_EQ(TelemetryModelTestAccess::cursor(restored), 3U);
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
    QTemporaryDir report_directory;
    const auto report_url = QUrl::fromLocalFile(report_directory.filePath("live.json"));
    ASSERT_TRUE(model.saveReport(report_url, "live transport")) << model.status().toStdString();
    TelemetryModel offline;
    ASSERT_TRUE(offline.loadReport(report_url)) << offline.status().toStdString();
    EXPECT_EQ(offline.points(), model.points());
    EXPECT_FALSE(
        model.loadReport(report_url)); // A live attached client must be disconnected first.
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

TEST(TelemetryUnit, CameraHealthGraphsPreserveMissingAndLargeValuesWithoutFreshRender)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(
        model, 1, 2, 0,
        [](auto &sample)
        {
            sample.frame.reset();
            if (sample.cursor == 2)
            {
                sample.source.camera_diagnostics = sv::CameraDiagnosticsSet{};
                sample.source.camera_diagnostics->at(2) = {UINT64_MAX, 4, 3, 2, 1, true};
            }
        });
    const auto first = model.points().first().toMap();
    const auto last = model.points().last().toMap();
    EXPECT_FALSE(first.value("camera_2_read_failures").isValid());
    EXPECT_FALSE(last.value("camera_2_open_attempts").isValid());
    EXPECT_DOUBLE_EQ(last.value("camera_2_read_failures").toDouble(), 4);
    EXPECT_DOUBLE_EQ(last.value("camera_2_opencv_exceptions").toDouble(), 3);
    EXPECT_DOUBLE_EQ(last.value("camera_2_reconnects").toDouble(), 2);
    EXPECT_DOUBLE_EQ(last.value("camera_2_release_failures").toDouble(), 1);
    EXPECT_DOUBLE_EQ(last.value("camera_2_worker_stopped").toDouble(), 1);
    EXPECT_TRUE(model.sourceDetails().contains("open_attempts=" + QString::number(UINT64_MAX)));
    EXPECT_FALSE(last.value("render_wall").isValid());
}

TEST(TelemetryUnit, CheckedCameraModeIsShownWithoutNewMetricChoices)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(
        model, 1, 1, 0,
        [](auto &sample)
        {
            sample.source.camera_diagnostics = sv::CameraDiagnosticsSet{};
            sample.source.camera_diagnostics->at(0).open_attempts = UINT64_MAX;
            sample.source.camera_diagnostics->at(0).recovery =
                sv::CameraRecoveryStatus{UINT64_MAX, 5000, {}};
            sample.source.camera_diagnostics->at(0).negotiated_mode =
                sv::CameraCaptureMode{1280, 720, UINT64_MAX, 29.97, "MJPG", {}};
            sample.source.camera_diagnostics->at(0).negotiated_mode->automatic_properties =
                sv::CameraAutomaticProperties{{}, {}, 64};
        });
    EXPECT_TRUE(model.sourceDetails().contains("1280×720"));
    EXPECT_TRUE(model.sourceDetails().contains("fps=29.97"));
    EXPECT_TRUE(
        model.sourceDetails().contains("consecutive_failures=" + QString::number(UINT64_MAX)));
    EXPECT_TRUE(model.sourceDetails().contains("retry_delay_ms=5000"));
    EXPECT_TRUE(model.sourceDetails().contains("FOURCC=MJPG"));
    EXPECT_TRUE(model.sourceDetails().contains("Automatic readback"));
    EXPECT_TRUE(model.sourceDetails().contains("buffers=64"));
    EXPECT_TRUE(model.sourceDetails().contains("open_attempt=" + QString::number(UINT64_MAX)));
    EXPECT_EQ(model.sourceMetrics().size(), 42);
}

TEST(TelemetryUnit, SelectedFrameCaptureIsDistinctFromCurrentCameraMode)
{
    TelemetryModel model;
    TelemetryModelTestAccess::catalog(model);
    TelemetryModelTestAccess::batch(
        model, 1, 1, 0,
        [](auto &sample)
        {
            sample.source.camera_diagnostics = sv::CameraDiagnosticsSet{};
            sample.source.camera_diagnostics->at(0).open_attempts = 2;
            sample.source.camera_diagnostics->at(0).negotiated_mode =
                sv::CameraCaptureMode{1280, 720, 2, 30, "MJPG", 2};
            sample.frame->camera_inputs.push_back(
                {0, UINT64_MAX, {1, sv::CameraCaptureMode{640, 480, 1, 15, "YUYV", {}}}});
            sample.frame->camera_inputs.back().capture.negotiated_mode->automatic_properties =
                sv::CameraAutomaticProperties{{}, {}, 8};
        });
    const auto details = model.sourceDetails();
    EXPECT_TRUE(details.contains("1280×720"));
    EXPECT_TRUE(details.contains("Selected camera 0"));
    EXPECT_TRUE(details.contains("capture open_attempt=1"));
    EXPECT_TRUE(details.contains("640×480"));
    EXPECT_TRUE(details.contains("checked fps=15"));
    EXPECT_TRUE(details.contains("Selected Automatic readback"));
    EXPECT_TRUE(details.contains("buffers=8"));
    EXPECT_TRUE(details.contains("source_sequence=" + QString::number(UINT64_MAX)));
    EXPECT_EQ(model.sourceMetrics().size(), 42);
}
