#include "qml_warning_capture.hpp"
#include "research_comparison.hpp"
#include "research_comparison_fixture.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QThread>
#include <fstream>
#include <gtest/gtest.h>

TEST(SimulatorComparisonQml, SeriesSelectionRendersGuardedSummariesAndExportsOffline)
{
    QmlWarningCapture warnings;
    QTemporaryDir directory;
    ResearchComparison model;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchSeriesPanel.qml"));
    std::unique_ptr<QObject> root(component.create());
    ASSERT_TRUE(root) << component.errorString().toStdString();
    root->setProperty("controller", QVariant::fromValue(&model));
    auto *item = qobject_cast<QQuickItem *>(root.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(1100, 1300);
    item->setParentItem(window.contentItem());
    item->setWidth(1060);
    item->setX(20);
    window.show();
    auto *start = root->findChild<QObject *>("seriesCompare");
    ASSERT_TRUE(start);
    EXPECT_FALSE(start->property("enabled").toBool());
    QUrl first;
    for (unsigned index = 0; index < 3; ++index)
    {
        auto report = comparison_fixture::report();
        report["fixture_run"] = index;
        report.at("summaries")
            .as_array()[0]
            .as_object()
            .at("timings")
            .as_object()
            .at("render_wall")
            .as_object()["p50_ms"] = 1 + index;
        if (index == 2)
        {
            report.erase("client_build_passport");
        }
        const auto file = directory.path() + "/report-" + QString::number(index) + ".json";
        std::ofstream(file.toStdString()) << boost::json::serialize(report);
        QVariant result;
        ASSERT_TRUE(QMetaObject::invokeMethod(
            root.get(), "addReport", Q_RETURN_ARG(QVariant, result),
            Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(file)))));
        ASSERT_TRUE(result.toBool());
        if (index == 0)
        {
            first = QUrl::fromLocalFile(file);
        }
    }
    QVariant duplicate;
    ASSERT_TRUE(QMetaObject::invokeMethod(root.get(), "addReport",
                                          Q_RETURN_ARG(QVariant, duplicate),
                                          Q_ARG(QVariant, QVariant::fromValue(first))));
    EXPECT_FALSE(duplicate.toBool());
    ASSERT_TRUE(start->property("enabled").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(start, "clicked"));
    ASSERT_TRUE(model.busy());
    EXPECT_FALSE(start->property("enabled").toBool());
    QElapsedTimer timer;
    timer.start();
    while (model.busy() && timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    ASSERT_FALSE(model.busy());
    ASSERT_EQ(model.result().value("report_count").toInt(), 3);
    const auto row = model.result().value("rows").toList()[0].toMap();
    EXPECT_FALSE(row.value("render_p50_ms").toMap().value("median").isValid());
    EXPECT_EQ(row.value("mae_rgb8").toMap().value("eligible_reports").toInt(), 3);
    auto *save = root->findChild<QObject *>("seriesSave");
    ASSERT_TRUE(save->property("enabled").toBool());
    auto *dialogs =
        root->findChild<QObject *>("seriesFilesLoader")->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
    const auto output = QUrl::fromLocalFile(directory.path() + "/series # %.json");
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "saveChosen", Q_ARG(QUrl, output)));
    EXPECT_TRUE(std::filesystem::exists(output.toLocalFile().toStdString()));
    QCoreApplication::processEvents();
    const auto screenshot = qEnvironmentVariable("SV_REPORT_SERIES_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        ASSERT_TRUE(window.grabWindow().save(screenshot));
    }
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}
