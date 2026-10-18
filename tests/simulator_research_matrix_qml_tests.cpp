#include "qml_warning_capture.hpp"
#include "research_capabilities_fixture.hpp"
#include "research_controller.hpp"
#include <QJSValue>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQuickItem>
#include <gtest/gtest.h>

namespace
{
QQuickItem *matrixItem(QQuickItem *root, const QString &name)
{
    if (!root || root->objectName() == name)
    {
        return root;
    }
    for (auto *child : root->childItems())
    {
        if (auto *match = matrixItem(child, name))
        {
            return match;
        }
    }
    return nullptr;
}
} // namespace

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

TEST(SimulatorResearchMatrixQml, CarrierSnapshotsAreCopiedDeduplicatedAndCatalogGated)
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
    object->setProperty("surfaceCatalog", QVariantMap{{"types", QStringList{"dome_floor_v1"}}});
    auto *editor = object->findChild<QObject *>("researchCarrierEditor");
    auto *use = object->findChild<QObject *>("researchUseCarriers");
    auto *capture = object->findChild<QObject *>("researchCaptureCarrier");
    auto *start = object->findChild<QObject *>("researchStart");
    ASSERT_TRUE(editor && use && capture && start);
    EXPECT_FALSE(capture->property("enabled").toBool());
    use->setProperty("checked", true);
    EXPECT_FALSE(start->property("enabled").toBool()); // explicit empty matrix
    QVariantMap dome{{"type", "dome_floor_v1"},
                     {"dome_radius_m", 14.},
                     {"dome_latitude_cells", 14},
                     {"dome_longitude_cells", 64},
                     {"floor_radial_cells", 12}};
    object->setProperty("activeSurface", dome);
    object->setProperty("canCaptureSurface", true);
    ASSERT_TRUE(QMetaObject::invokeMethod(capture, "clicked"));
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    ASSERT_TRUE(QMetaObject::invokeMethod(capture, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    EXPECT_TRUE(editor->property("notice").toString().contains("уже добавлен"));
    dome["dome_radius_m"] = 15.;
    object->setProperty("activeSurface", dome);
    EXPECT_EQ(object->property("variantCount").toInt(), 4); // existing snapshot unchanged
    ASSERT_TRUE(QMetaObject::invokeMethod(capture, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 8);
    object->setProperty("surfaceCatalog", QVariantMap{});
    EXPECT_FALSE(start->property("enabled").toBool());
    use->setProperty("checked", false);
    EXPECT_TRUE(start->property("enabled").toBool()); // baseline doesn't need surface catalog
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    use->setProperty("checked", true);
    EXPECT_EQ(object->property("variantCount").toInt(), 8); // retained, not silently filtered
    EXPECT_FALSE(start->property("enabled").toBool());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}

TEST(SimulatorResearchMatrixQml, NumericListsUseControlsKeepSelectionsAndRespectCatalog)
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
    auto catalog = capability_fixture::catalog();
    object->setProperty("fusionCatalog", catalog);
    object->setProperty("selectedModes", QStringList{"angular_feather"});
    object->findChild<QObject *>("researchShowNumericAxes")->setProperty("checked", true);
    auto *edge = object->findChild<QObject *>("researchAxis_edge_width_px");
    auto *angle = object->findChild<QObject *>("researchAxis_angle_power");
    auto *start = object->findChild<QObject *>("researchStart");
    auto *use = object->findChild<QObject *>("researchUse_edge_width_px");
    auto *add = object->findChild<QObject *>("researchAdd_edge_width_px");
    auto *number = object->findChild<QObject *>("researchValue_edge_width_px");
    ASSERT_TRUE(edge && angle && start && use && add && number);
    use->setProperty("checked", true);
    EXPECT_EQ(object->property("variantCount").toInt(), 1);
    number->setProperty("number", 12.5);
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 2);
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 2);
    EXPECT_TRUE(edge->property("notice").toString().contains("уже добавлено"));
    auto *remove =
        matrixItem(qobject_cast<QQuickItem *>(object.get()), "researchRemove_edge_width_px_0");
    ASSERT_TRUE(remove);
    ASSERT_TRUE(QMetaObject::invokeMethod(remove, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 1);
    number->setProperty("number", 24.);
    ASSERT_TRUE(QMetaObject::invokeMethod(add, "clicked"));
    EXPECT_EQ(object->property("variantCount").toInt(), 2);
    object->findChild<QObject *>("researchUse_angle_power")->setProperty("checked", true);
    angle->setProperty("values", QVariantList{1., 2.});
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    EXPECT_TRUE(start->property("enabled").toBool());
    catalog["edge_width_px"] = QVariantMap{{"exclusive_min", 20}, {"max", 24}};
    object->setProperty("fusionCatalog", catalog);
    EXPECT_FALSE(start->property("enabled").toBool()); // retained12.5 still unavailable
    object->setProperty("selectedModes", QStringList{"hard_best_angle"});
    EXPECT_EQ(object->property("variantCount").toInt(), 1);
    EXPECT_TRUE(start->property("enabled").toBool()); // irrelevant axes omitted
    object->setProperty("selectedModes", QStringList{"angular_feather"});
    EXPECT_EQ(object->property("variantCount").toInt(), 4);
    EXPECT_FALSE(start->property("enabled").toBool());
    edge->setProperty("values", QVariantList{});
    EXPECT_FALSE(start->property("enabled").toBool());
    use->setProperty("checked", false);
    EXPECT_EQ(object->property("variantCount").toInt(), 2);
    EXPECT_TRUE(start->property("enabled").toBool());
    EXPECT_FALSE(controller.busy());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
