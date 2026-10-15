#include "qml_warning_capture.hpp"
#include "telemetry_model.hpp"
#include <QEventLoop>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>

struct TelemetryModelTestAccess
{
    static void seed(TelemetryModel &model)
    {
        sv::client::TelemetryCatalog catalog;
        catalog.server_instance_id = "qml-fixture";
        catalog.clock_domain = "server_steady_clock_ns";
        catalog.cpu_percent_basis = "one_logical_cpu_100_percent";
        catalog.settings = {true, 100, 128};
        model.acceptCatalog(catalog);
        sv::client::TelemetryBatch batch;
        batch.server_instance_id = catalog.server_instance_id;
        batch.next_cursor = 25;
        for (uint64_t i = 1; i <= 25; ++i)
        {
            sv::client::TelemetrySample sample;
            sample.cursor = i;
            sample.timestamp_ns = i * 100000000;
            sample.config_revision = 3;
            if (i != 12)
            {
                sample.process.cpu_percent = 50 + (i % 7) * 15;
            }
            sample.process.rss_bytes = (120 + i) * 1048576;
            sample.frame.emplace();
            sample.frame->fresh = i % 3 != 0;
            sample.frame->timestamp_ns = sample.timestamp_ns - 1000000;
            sample.source_work.push_back({0, "camera_read", 0, i, sample.timestamp_ns - 2000000,
                                          sample.timestamp_ns - 1000000, .25, std::nullopt, .25,
                                          false});
            for (const auto *span :
                 {"source_poll", "pre_render_prepare", "fusion_cpu", "layer_readback_cpu",
                  "upload_cpu", "readback_copy_cpu", "publish_enqueue"})
            {
                sample.frame->spans_ms[span] = 0;
            }
            sample.frame->spans_ms["fusion_cpu"] = double(i) * .000001;
            sample.frame->spans_ms["gpu_draw"] =
                i % 2 ? boost::json::value(.20) : boost::json::value(nullptr);
            sample.frame->input_age_at_render_ms = 50 + i;
            sample.source.queued_batches = i % 4;
            sample.data_busy = i % 4 == 0;
            sample.frame->spans_ms["render_wall"] = double(15 + i % 5);
            batch.samples.push_back(sample);
        }
        model.acceptBatch(batch);
    }
};

TEST(SimulatorTelemetryQml, RendersTabsMissingMetricsAndFreshFramesWithoutWarnings)
{
    QmlWarningCapture warnings;
    TelemetryModel model;
    TelemetryModelTestAccess::seed(model);
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    engine.rootContext()->setContextProperty("telemetryModel", &model);
    QQmlComponent component(&engine, QUrl("qrc:/TelemetryPanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(430, 840);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(390);
    item->setX(20);
    item->setY(20);
    window.show();
    auto *tabs = object->findChild<QObject *>("telemetryTabs");
    ASSERT_TRUE(tabs);
    auto draw = [&]
    {
        QEventLoop loop;
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
    };
    for (int i = 0; i < 5; ++i)
    {
        tabs->setProperty("currentIndex", i);
        draw();
    }
    tabs->setProperty("currentIndex", 3);
    auto *stage_choice = object->findChild<QObject *>("pipelineMetricChoice");
    ASSERT_TRUE(stage_choice);
    for (int i = 0; i < model.pipelineMetrics().size(); ++i)
    {
        stage_choice->setProperty("currentIndex", i);
        draw();
    }
    stage_choice->setProperty("currentIndex", 3); // Tiny fusion durations remain visible.
    draw();
    auto *stage_chart = object->findChild<QObject *>("pipelineMetricChoiceChart");
    ASSERT_TRUE(stage_chart);
    auto *canvas = stage_chart->findChild<QObject *>("telemetryCanvas");
    ASSERT_TRUE(canvas);
    EXPECT_GT(canvas->property("upper").toDouble(), 0);
    EXPECT_LT(canvas->property("upper").toDouble(), .001);
    const auto stage_screenshot = qEnvironmentVariable("SV_TELEMETRY_STAGE_SCREENSHOT");
    if (!stage_screenshot.isEmpty())
    {
        window.resize(430, 1100);
        draw();
        ASSERT_TRUE(window.grabWindow().save(stage_screenshot));
        window.resize(430, 840);
    }
    tabs->setProperty("currentIndex", 4);
    auto *source_choice = object->findChild<QObject *>("sourceMetricChoice");
    ASSERT_TRUE(source_choice);
    for (int i = 0; i < model.sourceMetrics().size(); ++i)
    {
        source_choice->setProperty("currentIndex", i);
        draw();
    }
    const auto capture_screenshot = qEnvironmentVariable("SV_TELEMETRY_CAPTURE_SCREENSHOT");
    if (!capture_screenshot.isEmpty())
    {
        source_choice->setProperty("currentIndex", 6); // Camera 0 read wall.
        window.resize(430, 1400);
        draw();
        ASSERT_TRUE(window.grabWindow().save(capture_screenshot));
        window.resize(430, 840);
    }
    tabs->setProperty("currentIndex", 0);
    draw();
    auto *loader = object->findChild<QObject *>("telemetryFilesLoader");
    ASSERT_TRUE(loader);
    ASSERT_EQ(loader->property("status").toInt(), 1); // Ready
    auto *dialogs = loader->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
#if QT_VERSION_MAJOR >= 6
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "open"));
    draw();
    auto *open = dialogs->findChild<QObject *>("telemetryOpenDialog");
    ASSERT_TRUE(open);
    EXPECT_TRUE(open->property("visible").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(open, "reject"));
    draw();
#endif
    QTemporaryDir directory;
    const auto report = QUrl::fromLocalFile(directory.filePath("window.json"));
    const auto before = model.points();
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, report), Q_ARG(bool, true)));
    ASSERT_TRUE(QFileInfo::exists(report.toLocalFile())) << model.status().toStdString();
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, report), Q_ARG(bool, false)));
    EXPECT_TRUE(model.offline()) << model.status().toStdString();
    EXPECT_EQ(model.points(), before);
    draw();
    const auto screenshot = qEnvironmentVariable("SV_TELEMETRY_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        ASSERT_TRUE(window.grabWindow().save(screenshot));
    }
    model.clear();
    draw();
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join("\n").toStdString();
}
