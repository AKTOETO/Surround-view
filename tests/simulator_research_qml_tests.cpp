#include "qml_warning_capture.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "sv/json.hpp"
#include <QEventLoop>
#include <QImage>
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTimer>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
QQuickItem *researchItem(QQuickItem *root, const QString &name)
{
    if (root->objectName() == name)
    {
        return root;
    }
    for (auto *child : root->childItems())
    {
        if (auto *found = researchItem(child, name))
        {
            return found;
        }
    }
    return nullptr;
}
} // namespace

TEST(SimulatorResearchQml, FormEditsGenerateTypedDraftAndTabsRenderWithoutWarnings)
{
    QmlWarningCapture warnings;
    ResearchController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("researchController", &controller);
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(1040, 1050);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(1000);
    item->setX(20);
    item->setY(15);
    window.show();
    auto *frames = object->findChild<QObject *>("researchFrames");
    auto *repeats = object->findChild<QObject *>("researchRepeats");
    auto *start = object->findChild<QObject *>("researchStart");
    auto *tabs = object->findChild<QObject *>("researchTabs");
    ASSERT_TRUE(frames && repeats && start && tabs);
    EXPECT_FALSE(start->property("enabled").toBool());
    ASSERT_TRUE(frames->setProperty("value", 2));
    ASSERT_TRUE(repeats->setProperty("value", 4));
    QVariant form;
    ASSERT_TRUE(QMetaObject::invokeMethod(object.get(), "draft", Q_RETURN_ARG(QVariant, form)));
    auto values =
        form.canConvert<QJSValue>() ? form.value<QJSValue>().toVariant().toMap() : form.toMap();
    const auto document = researchDraft(values);
    EXPECT_EQ(document.at("frames").to_number<unsigned>(), 2U);
    EXPECT_EQ(document.at("repeats").to_number<unsigned>(), 4U);
    EXPECT_EQ(document.at("variants").as_array().size(), 4U);
    controller.setConnection(sv::client::Options{});
    QCoreApplication::processEvents();
    EXPECT_TRUE(start->property("enabled").toBool());
    auto *loader = object->findChild<QObject *>("researchFilesLoader");
    ASSERT_TRUE(loader);
    ASSERT_EQ(loader->property("status").toInt(), 1);
    auto *dialogs = loader->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
#if QT_VERSION_MAJOR >= 6
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "open", Q_ARG(QVariant, QVariant(true)),
                                          Q_ARG(QVariant, QVariant(false))));
    QCoreApplication::processEvents();
    auto *chooser = dialogs->findChild<QObject *>("researchFileDialog");
    ASSERT_TRUE(chooser);
    EXPECT_TRUE(chooser->property("visible").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(chooser, "reject"));
#endif
    const auto reference = QUrl::fromLocalFile("/tmp/reference with # and %.json");
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, reference), Q_ARG(bool, true)));
    EXPECT_EQ(object->property("referenceSource").toUrl(), reference);
    auto *sourceMode = object->findChild<QObject *>("researchPlanMode");
    ASSERT_TRUE(sourceMode);
    ASSERT_TRUE(sourceMode->setProperty("currentIndex", 1));
    EXPECT_FALSE(start->property("enabled").toBool());
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, reference), Q_ARG(bool, false)));
    EXPECT_EQ(object->property("scenarioSource").toUrl(), reference);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_FALSE(controller.busy()); // choosing files must not execute or mutate the server
    sourceMode->setProperty("currentIndex", 2);
    EXPECT_FALSE(start->property("enabled").toBool());
    object->setProperty("choosingBatch", true);
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, reference), Q_ARG(bool, false)));
    EXPECT_EQ(object->property("batchSource").toUrl(), reference);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_FALSE(controller.busy());
    object->setProperty("choosingBatch", false);
    sourceMode->setProperty("currentIndex", 0);
    object->setProperty("referenceSource", QUrl{});
    for (int tab = 0; tab < 3; ++tab)
    {
        ASSERT_TRUE(tabs->setProperty("currentIndex", tab));
        QCoreApplication::processEvents();
    }
    ASSERT_TRUE(tabs->setProperty("currentIndex", 0));
    QEventLoop loop;
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();
    const auto image = window.grabWindow();
    ASSERT_FALSE(image.isNull());
    const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QML_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        ASSERT_TRUE(image.save(screenshot));
    }
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}

TEST(SimulatorResearchQml, BatchEditorAddsReordersRemovesAndBoundsMembersWithoutRunning)
{
    QmlWarningCapture warnings;
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("researchController", &controller);
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto *editor = object->findChild<QObject *>("researchBatchEditor");
    auto *mode = object->findChild<QObject *>("researchPlanMode");
    auto *start = object->findChild<QObject *>("researchStart");
    ASSERT_TRUE(editor && mode && start);
    mode->setProperty("currentIndex", 3);
    EXPECT_FALSE(start->property("enabled").toBool());
    auto members = [&]
    {
        const auto value = editor->property("members");
        return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                            : value.toList();
    };
    auto *loader = object->findChild<QObject *>("researchFilesLoader");
    ASSERT_TRUE(loader);
    auto *dialogs = loader->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
    object->setProperty("choosingMember", true);
    const auto source = QUrl::fromLocalFile("/tmp/scenario # and %.json");
    for (unsigned i = 0; i < 9; ++i)
    {
        ASSERT_TRUE(
            QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, source), Q_ARG(bool, false)));
    }
    ASSERT_EQ(members().size(), 8);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_FALSE(editor->findChild<QObject *>("batchAddScenario")->property("enabled").toBool());
    QVariant changed;
    QCoreApplication::processEvents();
    auto *down = researchItem(qobject_cast<QQuickItem *>(editor), "batchMoveDown_case-1");
    ASSERT_TRUE(down);
    ASSERT_TRUE(QMetaObject::invokeMethod(down, "clicked"));
    EXPECT_EQ(members()[0].toMap().value("id").toString(), "case-2");
    EXPECT_EQ(members()[1].toMap().value("id").toString(), "case-1");
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "moveMember", Q_RETURN_ARG(QVariant, changed),
                                          Q_ARG(QVariant, 0), Q_ARG(QVariant, -1)));
    EXPECT_FALSE(changed.toBool());
    auto *remove = researchItem(qobject_cast<QQuickItem *>(editor), "batchRemove_case-1");
    ASSERT_TRUE(remove);
    ASSERT_TRUE(QMetaObject::invokeMethod(remove, "clicked"));
    EXPECT_EQ(members().size(), 7);
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, source), Q_ARG(bool, false)));
    EXPECT_EQ(members().last().toMap().value("id").toString(), "case-1");
    // Editing alone never prepares inputs, opens a lease, or starts a worker.
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}

TEST(SimulatorResearchQml, ReferenceWorkflowRendersMeasuredResults)
{
    const auto source = qEnvironmentVariable("SV_RESEARCH_GUI_SCENARIO");
    if (source.isEmpty())
    {
        GTEST_SKIP() << "Executed by transport harness with regression reference";
    }
    for (const int mode : {1, 2, 3})
    {
        const bool batch = mode >= 2;
        const auto endpoint =
            sv::parse_json(qEnvironmentVariable("SV_RESEARCH_GUI_ENDPOINT").toStdString())
                .as_object();
        sv::client::Options options;
        options.timeout_ms = 3000;
        if (endpoint.contains("directory"))
        {
            options.endpoint.directory = std::string(endpoint.at("directory").as_string());
        }
        else
        {
            options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
            options.endpoint.host = std::string(endpoint.at("host").as_string());
            options.endpoint.control_port = endpoint.at("control_port").to_number<uint16_t>();
            options.endpoint.data_port = endpoint.at("data_port").to_number<uint16_t>();
        }
        QmlWarningCapture warnings;
        ResearchController controller;
        controller.setConnection(options);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("researchController", &controller);
        engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
        QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
        std::unique_ptr<QObject> object(component.create());
        ASSERT_TRUE(object) << component.errorString().toStdString();
        auto *item = qobject_cast<QQuickItem *>(object.get());
        ASSERT_TRUE(item);
        QQuickWindow window;
        window.resize(1040, 630);
        window.setColor(QColor("#1a1f2c"));
        item->setParentItem(window.contentItem());
        item->setWidth(1000);
        item->setX(20);
        item->setY(15);
        window.show();
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        object->findChild<QObject *>("researchPlanMode")->setProperty("currentIndex", mode);
        if (mode == 2)
        {
            const auto plan = directory.path() + "/batch.json";
            const boost::json::object member{{"id", "first"}, {"scenario", source.toStdString()}};
            auto second = member;
            second["id"] = "second";
            std::ofstream(plan.toStdString()) << boost::json::serialize(boost::json::object{
                {"schema_version", 1}, {"runs", boost::json::array{member, second}}});
            object->setProperty("batchSource", QUrl::fromLocalFile(plan));
        }
        else if (mode == 3)
        {
            auto *editor = object->findChild<QObject *>("researchBatchEditor");
            ASSERT_TRUE(editor);
            for (unsigned i = 0; i < 2; ++i)
            {
                ASSERT_TRUE(QMetaObject::invokeMethod(
                    editor, "addScenario",
                    Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(source)))));
            }
            const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
            if (!screenshot.isEmpty())
            {
                window.resize(1040, 960);
                QEventLoop paint;
                QTimer::singleShot(100, &paint, &QEventLoop::quit);
                paint.exec();
                ASSERT_TRUE(window.grabWindow().save(screenshot + ".editor.png"));
                window.resize(1040, 630);
            }
        }
        else
        {
            object->setProperty("scenarioSource", QUrl::fromLocalFile(source));
        }
        object->findChild<QObject *>("researchOutput")
            ->setProperty("text", directory.path() + "/result");
        ASSERT_TRUE(
            QMetaObject::invokeMethod(object->findChild<QObject *>("researchStart"), "clicked"));
        ASSERT_TRUE(controller.busy());
        QEventLoop loop;
        QObject::connect(&controller, &ResearchController::finished, &loop, &QEventLoop::quit);
        QTimer::singleShot(15000, &loop, &QEventLoop::quit);
        loop.exec();
        ASSERT_FALSE(controller.busy());
        ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
        EXPECT_EQ(controller.qualityStatus(), "passed");
        EXPECT_EQ(controller.batchMode(), batch);
        if (batch)
        {
            ASSERT_EQ(controller.batchRows().size(), 2);
            EXPECT_TRUE(controller.rows().isEmpty());
        }
        else
        {
            EXPECT_TRUE(controller.referenceInfo().contains("regression"));
            ASSERT_EQ(controller.rows().size(), 1);
        }
        object->findChild<QObject *>("researchTabs")->setProperty("currentIndex", 2);
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
        const auto image = window.grabWindow();
        ASSERT_FALSE(image.isNull());
        const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
        if (!screenshot.isEmpty())
        {
            ASSERT_TRUE(image.save(mode == 3 ? screenshot + ".composed.png"
                                   : batch   ? screenshot + ".batch.png"
                                             : screenshot));
        }
        EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
        item->setParentItem(nullptr);
    }
}

TEST(SimulatorResearchQml, CloseWaitsForWorkerCompletionWithoutBlockingEventLoop)
{
    QmlWarningCapture warnings;
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("researchController", &controller);
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchCloseGuard.qml"));
    std::unique_ptr<QObject> guard(component.create());
    ASSERT_TRUE(guard) << component.errorString().toStdString();
    QQuickWindow window;
    window.show();
    ASSERT_TRUE(guard->setProperty("targetWindow", QVariant::fromValue(&window)));
    QTemporaryDir existing;
    ASSERT_TRUE(existing.isValid());
    // Existing output fails in the worker before networking. Its terminal event is queued,
    // so busy stays true until the UI event loop resumes, independently of worker speed.
    const QVariantMap form{{"modes", QStringList{"multi_band"}},
                           {"frames", 1},
                           {"warmup", 0},
                           {"repeats", 1},
                           {"seed", 17},
                           {"boundary", "zero"},
                           {"pyramid_levels", 3},
                           {"seam_solver", "binary_pairs"}};
    ASSERT_TRUE(controller.startDraft(form, existing.path()));
    window.close();
    EXPECT_TRUE(window.isVisible());
    EXPECT_TRUE(guard->property("closeWhenDone").toBool());
    EXPECT_TRUE(controller.busy());
    QEventLoop loop;
    QObject::connect(&controller, &ResearchController::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(window.isVisible());
    EXPECT_FALSE(guard->property("closeWhenDone").toBool());
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
