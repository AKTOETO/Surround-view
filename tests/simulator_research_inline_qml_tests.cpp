#include "qml_warning_capture.hpp"
#include "research_controller.hpp"
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(SimulatorResearchQml, InlineBatchButtonFreezesFormAndImportPreservesTypedMembers)
{
    QmlWarningCapture warnings;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("researchController", &controller);
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
    std::unique_ptr<QObject> root(component.create());
    ASSERT_TRUE(root) << component.errorString().toStdString();
    root->setProperty("width", 1040);
    root->setProperty("selectedModes", QStringList{"edge_feather"});
    auto *frames = root->findChild<QObject *>("researchFrames");
    auto *editor = root->findChild<QObject *>("researchBatchEditor");
    auto *mode = root->findChild<QObject *>("researchPlanMode");
    ASSERT_TRUE(frames && editor && mode);
    mode->setProperty("currentIndex", 3);
    frames->setProperty("value", 1);
    auto rows = [&]()
    {
        const auto value = editor->property("members");
        return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toList()
                                            : value.toList();
    };
    auto *add = editor->findChild<QObject *>("batchAddDraft");
    ASSERT_TRUE(add);
    ASSERT_TRUE(add->property("enabled").toBool());
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    ASSERT_EQ(rows().size(), 1);
    const auto first = rows()[0].toMap().value("scenario").toMap();
    EXPECT_EQ(first.value("frames").toInt(), 1);
    EXPECT_TRUE(first.value("reset_replay").toBool());
    frames->setProperty("value", 2);
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    ASSERT_EQ(rows().size(), 2);
    EXPECT_EQ(rows()[0].toMap().value("scenario").toMap(), first);
    EXPECT_EQ(rows()[1].toMap().value("scenario").toMap().value("frames").toInt(), 2);
    root->setProperty("referenceSource", QUrl("https://example.invalid/ref"));
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    EXPECT_EQ(rows().size(), 2);
    EXPECT_FALSE(editor->property("draftStatus").toString().isEmpty());
    const auto before = rows();
    const auto file = QUrl::fromLocalFile(temporary.path() + "/inline # %.json");
    QVariant result;
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "savePlan", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, QVariant::fromValue(file))));
    ASSERT_TRUE(result.toBool());
    editor->setProperty("members", QVariantList{});
    ASSERT_TRUE(QMetaObject::invokeMethod(editor, "importPlan", Q_RETURN_ARG(QVariant, result),
                                          Q_ARG(QVariant, QVariant::fromValue(file))));
    ASSERT_TRUE(result.toBool());
    EXPECT_EQ(rows(), before);
    EXPECT_FALSE(controller.available());
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
