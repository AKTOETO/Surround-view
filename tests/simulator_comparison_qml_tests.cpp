#include "qml_warning_capture.hpp"
#include "research_comparison.hpp"
#include "research_comparison_fixture.hpp"
#include <QEventLoop>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <gtest/gtest.h>

TEST(SimulatorComparisonQml, LoadsRunsRendersAndExportsWithoutServerOrWarnings)
{
    QmlWarningCapture warnings;
    ResearchComparison model;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchComparisonPanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    ASSERT_TRUE(object->setProperty("controller", QVariant::fromValue(&model)));
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(1100, 1050);
    window.setColor(Qt::white);
    item->setParentItem(window.contentItem());
    item->setWidth(1060);
    item->setX(20);
    window.show();
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto path = directory.path() + "/input.json";
    std::ofstream(path.toStdString()) << boost::json::serialize(comparison_fixture::report());
    object->setProperty("leftFile", QUrl::fromLocalFile(path));
    object->setProperty("rightFile", QUrl::fromLocalFile(path));
    auto *start = object->findChild<QObject *>("comparisonStart");
    ASSERT_TRUE(start);
    ASSERT_TRUE(start->property("enabled").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(start, "clicked"));
    ASSERT_TRUE(model.busy());
    QEventLoop loop;
    QObject::connect(&model, &ResearchComparison::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    ASSERT_FALSE(model.busy());
    ASSERT_EQ(model.result().value("rows").toList().size(), 2);
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
    ASSERT_FALSE(window.grabWindow().isNull());
    auto *dialogs =
        object->findChild<QObject *>("comparisonFilesLoader")->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
    const auto output = QUrl::fromLocalFile(directory.path() + "/comparison.json");
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "saveChosen", Q_ARG(QUrl, output)));
    EXPECT_TRUE(std::filesystem::exists(output.toLocalFile().toStdString()));
    EXPECT_EQ(model.result().value("observation_gate").toString(), "matching_observation_claims");
    auto legacy = comparison_fixture::report();
    legacy.erase("observation_policy");
    const auto legacyPath = directory.path() + "/legacy.json";
    std::ofstream(legacyPath.toStdString()) << boost::json::serialize(legacy);
    object->setProperty("rightFile", QUrl::fromLocalFile(legacyPath));
    ASSERT_TRUE(QMetaObject::invokeMethod(start, "clicked"));
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    ASSERT_FALSE(model.busy());
    EXPECT_EQ(model.result().value("observation_gate").toString(),
              "different_or_unverified_observation");
    const auto row = model.result().value("rows").toList().front().toMap();
    EXPECT_TRUE(row.value("timing_delta_ms").isNull());
    EXPECT_FALSE(row.value("left").toMap().value("render_p50_ms").isNull());
    EXPECT_FALSE(row.value("mae_delta").isNull());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}

TEST(SimulatorComparisonQml, CloseWaitsForCancellationTerminalCallback)
{
    QmlWarningCapture warnings;
    ResearchComparison model;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("researchController", &model);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchCloseGuard.qml"));
    std::unique_ptr<QObject> guard(component.create());
    ASSERT_TRUE(guard);
    QQuickWindow window;
    window.show();
    guard->setProperty("targetWindow", QVariant::fromValue(&window));
    ASSERT_TRUE(
        model.compare(QUrl::fromLocalFile("/missing-a"), QUrl::fromLocalFile("/missing-b")));
    window.close();
    EXPECT_TRUE(window.isVisible());
    QEventLoop loop;
    QObject::connect(&model, &ResearchComparison::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(10000, &loop, &QEventLoop::quit);
    loop.exec();
    EXPECT_FALSE(model.busy());
    EXPECT_FALSE(window.isVisible());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
