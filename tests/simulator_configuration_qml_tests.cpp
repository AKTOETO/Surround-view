#include "configuration_editor.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <gtest/gtest.h>

struct ConfigurationEditorTestAccess
{
    static void seed(ConfigurationEditor &editor)
    {
        editor.connected_ = editor.snapshot_loaded_ = editor.catalog_loaded_ = true;
        editor.operations_supported_ = editor.apply_supported_ = true;
        editor.snapshot_.persistence_configured = true;
        editor.snapshot_.revision = 7;
        editor.snapshot_.server_instance_id = "0123456789abcdef0123456789abcdef";
        editor.base_revision_ = "7";
        editor.fusion_ = {{"mode", "edge_feather"},    {"diagnostic", "color"},
                          {"edge_width_px", 24},       {"angle_power", 2},
                          {"smoothness_weight", .1},   {"pyramid_levels", 4},
                          {"pyramid_boundary", "zero"}};
        editor.catalog_ = {
            {"fusion",
             QVariantMap{{"modes", QStringList{"edge_feather", "angular_feather", "multi_band"}},
                         {"diagnostics", QStringList{"color", "coverage", "weights"}},
                         {"pyramid_boundary", QStringList{"zero", "normalized"}},
                         {"seam_solver", QStringList{"binary_pairs", "alpha_expansion"}}}},
            {"surface",
             QVariantMap{
                 {"types", QStringList{"rectangular_bowl_v1", "dome_floor_v1", "cylinder_floor_v1",
                                       "cube_floor_v1"}},
                 {"geometry_fields",
                  QVariantMap{
                      {"dome_floor_v1",
                       QVariantMap{{"required", QStringList{"dome_radius_m", "dome_latitude_cells",
                                                            "dome_longitude_cells",
                                                            "floor_radial_cells"}}}}}}}}};
        editor.setSurfaceType("dome_floor_v1");
    }
};

TEST(SimulatorConfigurationQml, LoadsFormsChangesTabsAndRendersWithoutBindingErrors)
{
    ConfigurationEditor editor;
    ConfigurationEditorTestAccess::seed(editor);
    QQmlApplicationEngine engine;
    QStringList warnings;
    QObject::connect(&engine, &QQmlEngine::warnings,
                     [&](const QList<QQmlError> &errors)
                     {
                         for (const auto &error : errors)
                         {
                             warnings << error.toString();
                         }
                     });
    engine.rootContext()->setContextProperty("configurationEditor", &editor);
    QQmlComponent component(&engine, QUrl("qrc:/RuntimePanel.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_TRUE(object) << component.errorString().toStdString();
    auto *item = qobject_cast<QQuickItem *>(object.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(380, 1050);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(340);
    item->setX(20);
    item->setY(15);
    window.show();
    for (int tab = 0; tab < 3; ++tab)
    {
        auto *tabs = object->findChild<QObject *>("configurationTabs");
        ASSERT_TRUE(tabs);
        ASSERT_TRUE(tabs->setProperty("currentIndex", tab));
        QCoreApplication::processEvents();
    }
    for (const auto &type :
         {"cube_floor_v1", "cylinder_floor_v1", "rectangular_bowl_v1", "dome_floor_v1"})
    {
        editor.setSurfaceType(type);
        QCoreApplication::processEvents();
    }
    object->findChild<QObject *>("configurationTabs")->setProperty("currentIndex", 0);
    QEventLoop loop;
    QTimer::singleShot(120, &loop, &QEventLoop::quit);
    loop.exec();
    const auto image = window.grabWindow();
    ASSERT_FALSE(image.isNull());
    const auto screenshot = qEnvironmentVariable("SV_QML_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        ASSERT_TRUE(image.save(screenshot));
    }
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join('\n').toStdString();
    item->setParentItem(nullptr);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
