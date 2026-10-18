#include "qml_warning_capture.hpp"
#include "research_capabilities_fixture.hpp"
#include "research_controller.hpp"
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <gtest/gtest.h>

TEST(SimulatorResearchMatrixQml, PreviewAndCatalogGatesTrackAxesWithoutSilentlyEditingTheDraft)
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
    object->setProperty("width", 1040);
    auto catalog = capability_fixture::catalog();
    object->setProperty("fusionCatalog", catalog);
    auto *start = object->findChild<QObject *>("researchStart");
    auto *sweepLevels = object->findChild<QObject *>("researchSweepLevels");
    auto *sweepSolvers = object->findChild<QObject *>("researchSweepSolvers");
    auto *first = object->findChild<QObject *>("researchLevelFrom");
    auto *last = object->findChild<QObject *>("researchLevelTo");
    auto *step = object->findChild<QObject *>("researchLevelStep");
    ASSERT_TRUE(start && sweepLevels && sweepSolvers && first && last && step);
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    sweepLevels->setProperty("checked", true);
    EXPECT_EQ(object->property("variantCount").toInt(), 12);
    EXPECT_FALSE(object->findChild<QObject *>("researchLevels")->property("enabled").toBool());
    sweepSolvers->setProperty("checked", true);
    EXPECT_EQ(object->property("variantCount").toInt(), 18);
    EXPECT_FALSE(object->findChild<QObject *>("researchSolver")->property("enabled").toBool());
    step->setProperty("value", 2);
    EXPECT_EQ(object->property("variantCount").toInt(), 12);
    catalog["pyramid_levels"] = QVariantMap{{"min", 3}, {"max", 4}};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_EQ(first->property("value").toInt(), 2);
    EXPECT_FALSE(start->property("enabled").toBool());
    first->setProperty("value", 3);
    EXPECT_EQ(object->property("variantCount").toInt(), 6);
    EXPECT_TRUE(start->property("enabled").toBool());
    catalog["seam_solver"] = QStringList{"binary_pairs"};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_TRUE(sweepSolvers->property("checked").toBool());
    EXPECT_TRUE(sweepSolvers->property("enabled").toBool());
    EXPECT_FALSE(start->property("enabled").toBool());
    sweepSolvers->setProperty("checked", false);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    first->setProperty("value", 5);
    EXPECT_FALSE(start->property("enabled").toBool());
    const auto summaryValue = object->property("formSummary");
    const auto summary = summaryValue.canConvert<QJSValue>()
                             ? summaryValue.value<QJSValue>().toVariant().toMap()
                             : summaryValue.toMap();
    EXPECT_FALSE(summary.value("valid").toBool());
    EXPECT_FALSE(summary.contains("variants"));
    EXPECT_TRUE(object->findChild<QObject *>("researchVariantSummary")
                    ->property("text")
                    .toString()
                    .startsWith("Профиль недопустим:"));
    object->setProperty("selectedModes", QStringList{"edge_feather"});
    EXPECT_TRUE(start->property("enabled").toBool()); // irrelevant axes are omitted
    EXPECT_EQ(object->property("variantCount").toInt(), 1);
    object->setProperty("selectedModes", QStringList{"graph_cut_multi_band"});
    EXPECT_EQ(first->property("value").toInt(), 5); // retain invalid range across mode changes
    EXPECT_FALSE(start->property("enabled").toBool());
    first->setProperty("value", 3);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
