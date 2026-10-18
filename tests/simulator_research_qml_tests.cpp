#include "batch_plan.hpp"
#include "qml_warning_capture.hpp"
#include "research_batch_draft.hpp"
#include "research_capabilities_fixture.hpp"
#include "research_carrier_workflow.hpp"
#include "research_comparison.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "research_numeric_workflow.hpp"
#include "sv/json.hpp"
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QJSValue>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
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
    EXPECT_FALSE(start->property("enabled").toBool()); // no catalog yet
    object->setProperty("fusionCatalog", capability_fixture::catalog());
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

TEST(SimulatorResearchQml, BatchImportIsTransactionalAndKeepsDraftOnInvalidPlan)
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
    window.resize(1040, 960);
    item->setParentItem(window.contentItem());
    item->setWidth(1000);
    window.show();
    object->findChild<QObject *>("researchPlanMode")->setProperty("currentIndex", 3);
    auto *editor = object->findChild<QObject *>("researchBatchEditor");
    ASSERT_TRUE(editor);
    auto rows = [&]
    {
        const auto value = editor->property("members");
        return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                            : value.toList();
    };
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto file = directory.path() + "/plan # %.json";
    std::ofstream(file.toStdString()) << R"({"schema_version":1,"runs":[
        {"id":"case-1","scenario":"missing # %.json"},{"id":"imported","scenario":"missing # %.json"}]})";
    ASSERT_TRUE(
        QMetaObject::invokeMethod(editor->findChild<QObject *>("batchImportPlan"), "clicked"));
    auto *dialogs =
        object->findChild<QObject *>("researchFilesLoader")->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
#if QT_VERSION_MAJOR >= 6
    ASSERT_TRUE(
        QMetaObject::invokeMethod(dialogs->findChild<QObject *>("researchFileDialog"), "reject"));
#endif
    ASSERT_TRUE(rows().isEmpty()); // cancelling a chooser does not change the draft
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, QUrl::fromLocalFile(file)),
                                          Q_ARG(bool, false)));
    ASSERT_EQ(rows().size(), 2);
    EXPECT_EQ(rows()[1].toMap().value("id").toString(), "imported");
    EXPECT_TRUE(editor->property("importStatus").toString().contains("Импортировано"));
    const auto before = rows();
    std::ofstream(file.toStdString()) << R"({"schema_version":1,"runs":[
        {"id":"a","scenario":"first.json"},{"id":"a","scenario":"second.json"}]})";
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "chosen", Q_ARG(QUrl, QUrl::fromLocalFile(file)),
                                          Q_ARG(bool, false)));
    EXPECT_EQ(rows(), before);
    EXPECT_TRUE(editor->property("importStatus").toString().contains("Импорт не выполнен"));
    ASSERT_TRUE(QMetaObject::invokeMethod(
        editor, "addScenario", Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(file)))));
    EXPECT_EQ(rows().last().toMap().value("id").toString(), "case-2");
    EXPECT_FALSE(controller.busy());
    EXPECT_EQ(controller.exitCode(), -1);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}

TEST(SimulatorResearchQml, SavesDraftWithoutServerRejectsOverwriteAndTracksEdits)
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
    window.resize(1040, 1080);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(1000);
    item->setX(20);
    item->setY(15);
    window.show();
    object->findChild<QObject *>("researchPlanMode")->setProperty("currentIndex", 3);
    auto *editor = object->findChild<QObject *>("researchBatchEditor");
    ASSERT_TRUE(editor);
    auto *save = editor->findChild<QObject *>("batchSavePlan");
    ASSERT_TRUE(save);
    EXPECT_FALSE(save->property("enabled").toBool());
    const auto source = QUrl::fromLocalFile("/tmp/missing # %.json");
    for (unsigned i = 0; i < 2; ++i)
    {
        ASSERT_TRUE(QMetaObject::invokeMethod(editor, "addScenario",
                                              Q_ARG(QVariant, QVariant::fromValue(source))));
    }
    EXPECT_TRUE(save->property("enabled").toBool());
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto target = QUrl::fromLocalFile(directory.path() + "/draft # %.json");
    auto *dialogs =
        object->findChild<QObject *>("researchFilesLoader")->property("item").value<QObject *>();
    ASSERT_TRUE(dialogs);
    ASSERT_TRUE(QMetaObject::invokeMethod(save, "clicked"));
#if QT_VERSION_MAJOR >= 6
    auto *chooser = dialogs->findChild<QObject *>("researchSaveDialog");
    ASSERT_TRUE(chooser);
    EXPECT_TRUE(chooser->property("visible").toBool());
    EXPECT_NE(chooser->property("options").toInt() & 4, 0); // DontConfirmOverwrite
    ASSERT_TRUE(QMetaObject::invokeMethod(chooser, "reject"));
#endif
    EXPECT_TRUE(editor->property("savedPath").toString().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(target.toLocalFile().toStdString()));
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "saveChosen", Q_ARG(QUrl, target)));
    ASSERT_EQ(editor->property("savedPath").toString(), target.toLocalFile());
    EXPECT_TRUE(editor->property("saveStatus").toString().contains("Черновик сохранён"));
    const auto bytes = sv::research::read_batch_plan(target.toLocalFile().toStdString());
    ASSERT_EQ(sv::research::parse_batch_plan(bytes).at("runs").as_array().size(), 2U);
    const auto screenshot = qEnvironmentVariable("SV_RESEARCH_SAVE_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        QEventLoop paint;
        QTimer::singleShot(100, &paint, &QEventLoop::quit);
        paint.exec();
        ASSERT_TRUE(window.grabWindow().save(screenshot));
    }
    ASSERT_TRUE(QMetaObject::invokeMethod(dialogs, "saveChosen", Q_ARG(QUrl, target)));
    EXPECT_TRUE(editor->property("saveStatus").toString().contains("не сохранён"));
    EXPECT_EQ(sv::research::read_batch_plan(target.toLocalFile().toStdString()), bytes);
    QCoreApplication::processEvents();
    auto *down = researchItem(qobject_cast<QQuickItem *>(editor), "batchMoveDown_case-1");
    ASSERT_TRUE(down);
    ASSERT_TRUE(QMetaObject::invokeMethod(down, "clicked"));
    EXPECT_TRUE(editor->property("saveStatus").toString().contains("Состав изменён"));
    QVariant restored;
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "importPlan", Q_RETURN_ARG(QVariant, restored),
                                          Q_ARG(QVariant, QVariant::fromValue(target))));
    EXPECT_TRUE(restored.toBool());
    EXPECT_TRUE(editor->property("saveStatus").toString().contains("совпадает"));
    EXPECT_EQ(sv::research::read_batch_plan(target.toLocalFile().toStdString()), bytes);
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(controller.available());
    EXPECT_EQ(controller.exitCode(), -1);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}

TEST(SimulatorResearchQml, ReferenceWorkflowRendersMeasuredResults)
{
    const auto source = qEnvironmentVariable("SV_RESEARCH_GUI_SCENARIO");
    if (source.isEmpty())
    {
        GTEST_SKIP() << "Executed by transport harness with regression reference";
    }
    QTemporaryDir retained;
    ASSERT_TRUE(retained.isValid());
    const auto reportA = retained.path() + "/a.json";
    for (const int mode : {1, 0, 2, 3, 4, 5, 6})
    {
        const bool batch = mode == 2 || mode == 3;
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
        ResearchComparison comparison;
        ResearchController controller;
        controller.setConnection(options);
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("researchController", &controller);
        engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
        engine.rootContext()->setContextProperty("reportComparisonController", &comparison);
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
        object->findChild<QObject *>("researchPlanMode")
            ->setProperty("currentIndex", mode >= 4 ? 0 : mode);
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
            const auto plan = directory.path() + "/import.json";
            const auto relative =
                std::filesystem::relative(source.toStdString(), directory.path().toStdString())
                    .string();
            const boost::json::object first{{"id", "case-1"}, {"scenario", relative}};
            const boost::json::object second{{"id", "retired"}, {"scenario", relative}};
            std::ofstream(plan.toStdString()) << boost::json::serialize(boost::json::object{
                {"schema_version", 1}, {"runs", boost::json::array{first, second}}});
            QVariant imported;
            ASSERT_TRUE(QMetaObject::invokeMethod(
                editor, "importPlan", Q_RETURN_ARG(QVariant, imported),
                Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(plan)))));
            ASSERT_TRUE(imported.toBool());
            const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
            if (!screenshot.isEmpty())
            {
                window.resize(1040, 960);
                QEventLoop paint;
                QTimer::singleShot(100, &paint, &QEventLoop::quit);
                paint.exec();
                ASSERT_TRUE(window.grabWindow().save(screenshot + ".import.png"));
                window.resize(1040, 630);
            }
            QCoreApplication::processEvents();
            auto *remove = researchItem(qobject_cast<QQuickItem *>(editor), "batchRemove_retired");
            ASSERT_TRUE(remove);
            ASSERT_TRUE(QMetaObject::invokeMethod(remove, "clicked"));
            ASSERT_TRUE(QMetaObject::invokeMethod(
                editor, "addScenario",
                Q_ARG(QVariant, QVariant::fromValue(QUrl::fromLocalFile(source)))));
            auto *down = researchItem(qobject_cast<QQuickItem *>(editor), "batchMoveDown_case-1");
            ASSERT_TRUE(down);
            ASSERT_TRUE(QMetaObject::invokeMethod(down, "clicked"));
            const auto savedDraft = QUrl::fromLocalFile(directory.path() + "/saved-draft.json");
            QVariant saved;
            ASSERT_TRUE(
                QMetaObject::invokeMethod(editor, "savePlan", Q_RETURN_ARG(QVariant, saved),
                                          Q_ARG(QVariant, QVariant::fromValue(savedDraft))));
            ASSERT_TRUE(saved.toBool());
            ASSERT_TRUE(
                QMetaObject::invokeMethod(editor, "importPlan", Q_RETURN_ARG(QVariant, imported),
                                          Q_ARG(QVariant, QVariant::fromValue(savedDraft))));
            ASSERT_TRUE(imported.toBool());
            EXPECT_FALSE(controller.busy());
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
        else if (mode == 0 || mode >= 4)
        {
            QFile native(reportA);
            ASSERT_TRUE(native.open(QIODevice::ReadOnly));
            const auto catalog = QJsonDocument::fromJson(native.readAll())
                                     .object()
                                     .value("catalog")
                                     .toObject()
                                     .toVariantMap();
            object->setProperty("fusionCatalog", catalog);
            const auto sourceDocument =
                sv::parse_json(sv::research::read_batch_plan(source.toStdString())).as_object();
            const auto &variant = sourceDocument.at("variants").as_array()[0].as_object();
            object->setProperty("selectedModes", QStringList{QString::fromStdString(
                                                     std::string(variant.at("mode").as_string()))});
            object->setProperty(
                "selectedBoundary",
                QString::fromStdString(std::string(variant.at("pyramid_boundary").as_string())));
            object->setProperty("selectedSolver", QString::fromStdString(std::string(
                                                      variant.at("seam_solver").as_string())));
            object->findChild<QObject *>("researchLevels")
                ->setProperty("value", variant.at("pyramid_levels").to_number<int>());
            object->findChild<QObject *>("researchFrames")
                ->setProperty("value", sourceDocument.at("frames").to_number<int>());
            object->findChild<QObject *>("researchWarmup")
                ->setProperty("value", sourceDocument.at("warmup").to_number<int>());
            object->findChild<QObject *>("researchRepeats")
                ->setProperty("value", sourceDocument.at("repeats").to_number<int>());
            if (mode == 0)
            {
                const auto reference =
                    std::filesystem::path(source.toStdString()).parent_path() /
                    std::string(sourceDocument.at("quality_reference").as_string());
                object->setProperty("referenceSource", QUrl::fromLocalFile(QString::fromStdString(
                                                           reference.string())));
                object->findChild<QObject *>("researchOverrideCriteria")
                    ->setProperty("checked", true);
                object->findChild<QObject *>("researchUseMae")->setProperty("checked", true);
            }
            else if (mode == 4)
            {
                object->findChild<QObject *>("researchFrames")->setProperty("value", 1);
                object->findChild<QObject *>("researchWarmup")->setProperty("value", 0);
                object->findChild<QObject *>("researchRepeats")->setProperty("value", 1);
                object->findChild<QObject *>("researchSweepLevels")->setProperty("checked", true);
                object->findChild<QObject *>("researchLevelFrom")->setProperty("value", 2);
                object->findChild<QObject *>("researchLevelTo")->setProperty("value", 3);
                object->findChild<QObject *>("researchSweepSolvers")->setProperty("checked", true);
                EXPECT_EQ(object->property("variantCount").toInt(), 4);
            }
            if (mode == 5)
            {
                object->setProperty("selectedModes", QStringList{"edge_feather"});
                object->findChild<QObject *>("researchFrames")->setProperty("value", 1);
                object->findChild<QObject *>("researchWarmup")->setProperty("value", 0);
                object->findChild<QObject *>("researchRepeats")->setProperty("value", 1);
                ASSERT_NO_THROW(carrier_workflow::capture(object.get(), options));
                EXPECT_EQ(object->property("variantCount").toInt(), 5);
            }
            if (mode == 6)
            {
                numeric_workflow::prepare(object.get());
                EXPECT_EQ(object->property("variantCount").toInt(), 10);
            }
            ASSERT_TRUE(object->property("draftIssue").toString().isEmpty())
                << object->property("draftIssue").toString().toStdString();
            ASSERT_TRUE(
                object->findChild<QObject *>("researchStart")->property("enabled").toBool());
            const auto capture = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
            if (!capture.isEmpty())
            {
                window.resize(1040, mode == 6 ? 2500 : 1740);
                QEventLoop paint;
                QTimer::singleShot(100, &paint, &QEventLoop::quit);
                paint.exec();
                const auto formImage = window.grabWindow();
                if (mode == 6)
                {
                    ASSERT_TRUE(formImage.save(capture + ".numeric.png"));
                }
                else if (mode == 5)
                {
                    ASSERT_TRUE(formImage.save(capture + ".carriers.png"));
                }
                else if (mode == 4)
                {
                    ASSERT_TRUE(formImage.save(capture + ".matrix.png"));
                }
                else
                {
                    ASSERT_TRUE(formImage.save(capture + ".catalog.png"));
                    ASSERT_TRUE(formImage.save(capture + ".criteria.png"));
                }
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
        EXPECT_EQ(controller.qualityStatus(), mode >= 4 ? "not_requested" : "passed");
        EXPECT_EQ(controller.batchMode(), batch);
        if (mode == 0)
        {
            QFile reportFile(controller.reportPath());
            ASSERT_TRUE(reportFile.open(QIODevice::ReadOnly));
            const auto report = QJsonDocument::fromJson(reportFile.readAll()).object();
            const auto reference = report.value("quality_reference").toObject();
            EXPECT_EQ(reference.value("criteria_source").toString(), "scenario_override_v1");
            EXPECT_TRUE(reference.contains("manifest_criteria"));
            EXPECT_EQ(reference.value("criteria").toObject().size(), 1);
            EXPECT_EQ(reference.value("criteria").toObject().value("max_mae_rgb8").toDouble(), 0);
            EXPECT_TRUE(report.value("scenario").toObject().contains("quality_criteria"));
        }
        if (mode == 4)
        {
            QFile reportFile(controller.reportPath());
            ASSERT_TRUE(reportFile.open(QIODevice::ReadOnly));
            const auto report = QJsonDocument::fromJson(reportFile.readAll()).object();
            const auto variants = report.value("scenario").toObject().value("variants").toArray();
            ASSERT_EQ(variants.size(), 4);
            EXPECT_EQ(report.value("samples").toArray().size(), 4);
            for (const auto &entry : report.value("samples").toArray())
            {
                const auto sample = entry.toObject();
                const int index = sample.value("variant").toInt(-1);
                ASSERT_GE(index, 0);
                ASSERT_LT(index, variants.size());
                const auto declared = variants[index].toObject();
                const auto metadata = sample.value("metadata").toObject();
                const auto applied = metadata.value("fusion").toObject();
                EXPECT_EQ(applied.value("pyramid_levels").toInt(-1),
                          declared.value("pyramid_levels").toInt());
                EXPECT_EQ(applied.value("seam_solver").toString("binary_pairs"),
                          declared.value("seam_solver").toString("binary_pairs"));
                EXPECT_EQ(applied.value("pyramid_boundary").toString(),
                          declared.value("pyramid_boundary").toString());
                EXPECT_EQ(metadata.value("inputs"),
                          report.value("baseline").toObject().value("inputs"));
            }
            EXPECT_FALSE(report.contains("quality_reference"));
            QSet<QString> profiles;
            for (const auto &entry : variants)
            {
                const auto v = entry.toObject();
                profiles.insert(QString::number(v.value("pyramid_levels").toInt()) + ":" +
                                v.value("seam_solver").toString("binary_pairs"));
            }
            EXPECT_EQ(profiles, (QSet<QString>{"2:binary_pairs", "2:alpha_expansion",
                                               "3:binary_pairs", "3:alpha_expansion"}));
        }
        if (mode == 6)
        {
            numeric_workflow::check(controller, options);
        }
        if (mode == 5)
        {
            QFile file(controller.reportPath());
            ASSERT_TRUE(file.open(QIODevice::ReadOnly));
            const auto report = QJsonDocument::fromJson(file.readAll()).object();
            const auto variants = report.value("scenario").toObject().value("variants").toArray();
            ASSERT_EQ(variants.size(), 5);
            ASSERT_EQ(report.value("samples").toArray().size(), 5);
            for (const auto &value : report.value("samples").toArray())
            {
                const auto sample = value.toObject();
                const int index = sample.value("variant").toInt(-1);
                ASSERT_GE(index, 0);
                ASSERT_LT(index, variants.size());
                EXPECT_EQ(sample.value("metadata").toObject().value("surface"),
                          variants[index].toObject().value("surface"));
                EXPECT_EQ(sample.value("metadata").toObject().value("inputs"),
                          report.value("baseline").toObject().value("inputs"));
            }
            EXPECT_TRUE(report.value("restored").toBool());
            sv::research::Connection connection(options);
            const auto state = connection.request("state");
            EXPECT_EQ(carrier_workflow::map(state.at("surface")), report.value("initial_state")
                                                                      .toObject()
                                                                      .value("surface")
                                                                      .toObject()
                                                                      .toVariantMap());
        }
        if (mode == 4)
        {
            QSet<QString> labels;
            for (const auto &value : controller.rows())
            {
                const auto row = value.toMap();
                EXPECT_TRUE(row.value("profile").toString().contains(
                    "levels=" + row.value("pyramidLevels").toString()));
                EXPECT_TRUE(row.value("profile").toString().contains(
                    "solver=" + row.value("solver").toString()));
                labels.insert(row.value("profile").toString());
            }
            EXPECT_EQ(labels.size(), 4);
        }
        if (batch)
        {
            ASSERT_EQ(controller.batchRows().size(), 2);
            EXPECT_TRUE(controller.rows().isEmpty());
            if (mode == 3)
            {
                EXPECT_EQ(controller.batchRows()[0].toMap().value("id").toString(), "case-2");
                EXPECT_EQ(controller.batchRows()[1].toMap().value("id").toString(), "case-1");
            }
        }
        else
        {
            if (mode < 4)
            {
                EXPECT_TRUE(controller.referenceInfo().contains("regression"));
            }
            ASSERT_EQ(controller.rows().size(), mode == 6 ? 10 : mode == 5 ? 5 : mode == 4 ? 4 : 1);
        }
        object->findChild<QObject *>("researchTabs")->setProperty("currentIndex", 2);
        if (mode >= 4)
        {
            window.resize(1040, mode == 6 ? 1600 : mode == 5 ? 1550 : 1250);
        }
        QTimer::singleShot(100, &loop, &QEventLoop::quit);
        loop.exec();
        const auto image = window.grabWindow();
        ASSERT_FALSE(image.isNull());
        const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
        if (!screenshot.isEmpty())
        {
            ASSERT_TRUE(image.save(mode == 6   ? screenshot + ".numeric-results.png"
                                   : mode == 5 ? screenshot + ".carrier-results.png"
                                   : mode == 4 ? screenshot + ".matrix-results.png"
                                   : mode == 3 ? screenshot + ".composed.png"
                                   : batch     ? screenshot + ".batch.png"
                                               : screenshot));
        }
        if (mode == 1)
        {
            ASSERT_TRUE(QFile::copy(controller.reportPath(), reportA));
        }
        if (mode == 3)
        {
            const auto reportB = directory.path() + "/result/case-2.json";
            auto *panel = object->findChild<QObject *>("researchComparisonPanel");
            ASSERT_TRUE(panel);
            panel->setProperty("leftFile", QUrl::fromLocalFile(reportA));
            panel->setProperty("rightFile", QUrl::fromLocalFile(reportB));
            object->findChild<QObject *>("researchTabs")->setProperty("currentIndex", 3);
            ASSERT_TRUE(QMetaObject::invokeMethod(panel->findChild<QObject *>("comparisonStart"),
                                                  "clicked"));
            QEventLoop compareLoop;
            QObject::connect(&comparison, &ResearchComparison::finished, &compareLoop,
                             &QEventLoop::quit);
            QTimer::singleShot(15000, &compareLoop, &QEventLoop::quit);
            compareLoop.exec();
            ASSERT_FALSE(comparison.busy());
            const auto result = comparison.result();
            ASSERT_EQ(result.value("inputs").toString(), "matching_claims")
                << comparison.status().toStdString();
            EXPECT_EQ(result.value("timing_gate").toString(), "matching_workload_claims");
            EXPECT_EQ(result.value("quality_gate").toString(), "matching_reference_claims");
            ASSERT_EQ(result.value("rows").toList().size(), 1);
            EXPECT_EQ(result.value("rows").toList()[0].toMap().value("mae_delta").toDouble(), 0);
            ASSERT_TRUE(comparison.save(QUrl::fromLocalFile(retained.path() + "/comparison.json")));
            EXPECT_EQ(controller.batchRows().size(), 2);
            if (!screenshot.isEmpty())
            {
                window.resize(1040, 1180);
                QTimer::singleShot(100, &loop, &QEventLoop::quit);
                loop.exec();
                ASSERT_TRUE(window.grabWindow().save(screenshot + ".comparison.png"));
            }
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

TEST(SimulatorResearchQml, CatalogChangesKeepUnsupportedSelectionsVisibleAndGateLaunch)
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
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(1080, 1460);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(1040);
    item->setX(20);
    item->setY(15);
    window.show();
    auto *start = object->findChild<QObject *>("researchStart");
    ASSERT_TRUE(start);
    EXPECT_FALSE(start->property("enabled").toBool());
    auto catalog = capability_fixture::catalog();
    object->setProperty("fusionCatalog", catalog);
    EXPECT_TRUE(start->property("enabled").toBool());
    catalog["pyramid_boundary"] = QStringList{"zero"};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_EQ(object->property("selectedBoundary").toString(), "both");
    EXPECT_FALSE(start->property("enabled").toBool());
    auto *boundary = object->findChild<QObject *>("researchBoundary");
    ASSERT_TRUE(boundary);
    EXPECT_EQ(boundary->property("currentIndex").toInt(), -1);
    ASSERT_TRUE(QMetaObject::invokeMethod(boundary, "activated", Q_ARG(int, 0)));
    EXPECT_EQ(object->property("selectedBoundary").toString(), "zero");
    EXPECT_TRUE(start->property("enabled").toBool());
    catalog["modes"] = QStringList{"edge_feather", "multi_band"};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_FALSE(start->property("enabled").toBool());
    QCoreApplication::processEvents();
    auto *unsupported = researchItem(item, "researchMethod_graph_cut_multi_band");
    ASSERT_TRUE(unsupported);
    EXPECT_TRUE(unsupported->property("checked").toBool());
    EXPECT_TRUE(unsupported->property("enabled").toBool()); // permit explicit removal
    const auto screenshot = qEnvironmentVariable("SV_RESEARCH_CATALOG_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        QEventLoop paint;
        QTimer::singleShot(100, &paint, &QEventLoop::quit);
        paint.exec();
        ASSERT_TRUE(window.grabWindow().save(screenshot));
    }
    unsupported->setProperty("checked", false);
    ASSERT_TRUE(QMetaObject::invokeMethod(unsupported, "toggled"));
    EXPECT_TRUE(start->property("enabled").toBool());
    auto *levels = object->findChild<QObject *>("researchLevels");
    ASSERT_TRUE(levels);
    catalog["pyramid_levels"] = QVariantMap{{"min", 5}, {"max", 6}};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_EQ(levels->property("value").toInt(), 4); // retain explicit draft
    EXPECT_FALSE(start->property("enabled").toBool());
    levels->setProperty("value", 5);
    EXPECT_TRUE(start->property("enabled").toBool());
    object->setProperty("fusionCatalog", QVariantMap{}); // reset/disconnect
    EXPECT_FALSE(start->property("enabled").toBool());
    EXPECT_EQ(object->property("selectedBoundary").toString(), "zero");
    EXPECT_EQ(levels->property("value").toInt(), 5);
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
    item->setParentItem(nullptr);
}

TEST(SimulatorResearchQml, CriteriaEditorRequiresReferenceAndRetainsExplicitThresholds)
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
    object->setProperty("fusionCatalog", capability_fixture::catalog());
    auto *start = object->findChild<QObject *>("researchStart");
    auto *choice = object->findChild<QObject *>("researchOverrideCriteria");
    auto *mae = object->findChild<QObject *>("researchUseMae");
    auto *editor = object->findChild<QObject *>("researchCriteriaEditor");
    ASSERT_TRUE(start && choice && mae && editor);
    EXPECT_TRUE(start->property("enabled").toBool());
    choice->setProperty("checked", true);
    EXPECT_FALSE(start->property("enabled").toBool());
    object->setProperty("referenceSource", QUrl::fromLocalFile("/missing reference # %.json"));
    EXPECT_FALSE(start->property("enabled").toBool()); // explicit empty override
    mae->setProperty("checked", true);
    EXPECT_TRUE(start->property("enabled").toBool()); // IO remains deferred to preflight
    auto *number = object->findChild<QObject *>("researchMaeLimit");
    ASSERT_TRUE(number);
    ASSERT_TRUE(QMetaObject::invokeMethod(number, "edited", Q_ARG(double, .125)));
    EXPECT_DOUBLE_EQ(editor->property("maeLimit").toDouble(), .125);
    QVariant form;
    ASSERT_TRUE(QMetaObject::invokeMethod(object.get(), "draft", Q_RETURN_ARG(QVariant, form)));
    const auto values = form.value<QJSValue>().toVariant().toMap();
    EXPECT_EQ(values.value("quality_criteria").toMap().size(), 1);
    EXPECT_DOUBLE_EQ(values.value("quality_criteria").toMap().value("max_mae_rgb8").toDouble(),
                     .125);
    choice->setProperty("checked", false);
    ASSERT_TRUE(QMetaObject::invokeMethod(object.get(), "draft", Q_RETURN_ARG(QVariant, form)));
    EXPECT_FALSE(form.value<QJSValue>().toVariant().toMap().contains("quality_criteria"));
    choice->setProperty("checked", true);
    EXPECT_DOUBLE_EQ(editor->property("maeLimit").toDouble(), .125);
    object->setProperty("referenceSource", QUrl{});
    EXPECT_FALSE(start->property("enabled").toBool());
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
