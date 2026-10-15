#include "qml_warning_capture.hpp"
#include "telemetry_model.hpp"
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <gtest/gtest.h>

struct TelemetryModelTestAccess
{
    static void seed(TelemetryModel &model)
    {
        sv::client::TelemetryCatalog catalog;
        catalog.server_instance_id = "qml-fixture";
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
    engine.rootContext()->setContextProperty("telemetryModel", &model);
    QQmlComponent component(&engine, QUrl("qrc:/TelemetryPanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(430, 640);
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
    for (int i = 0; i < 3; ++i)
    {
        tabs->setProperty("currentIndex", i);
        draw();
    }
    tabs->setProperty("currentIndex", 0);
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
