#include "configuration_editor.hpp"
#include "qml_warning_capture.hpp"
#include "sv/surface_runtime.hpp"
#include <QGuiApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
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
            {"surface", QJsonDocument::fromJson(QByteArray::fromStdString(
                                                    boost::json::serialize(sv::surface_catalog())))
                            .object()
                            .toVariantMap()}};
        editor.setSurfaceType("dome_floor_v1");
    }
};

TEST(SimulatorConfigurationQml, LoadsFormsChangesTabsAndRendersWithoutBindingErrors)
{
    QmlWarningCapture log_warnings;
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
    object->findChild<QObject *>("configurationTabs")->setProperty("currentIndex", 1);
    editor.setSurfaceType("rectangular_bowl_v1");
    auto *surface_tabs = object->findChild<QObject *>("surfaceTabs");
    ASSERT_TRUE(surface_tabs);
    auto toggle = [&](const char *name, bool checked)
    {
        auto *control = object->findChild<QObject *>(name);
        return control && control->setProperty("checked", checked) &&
               QMetaObject::invokeMethod(control, "clicked");
    };
    surface_tabs->setProperty("currentIndex", 1);
    QCoreApplication::processEvents();
    ASSERT_TRUE(toggle("referenceHeightToggle", true));
    ASSERT_TRUE(toggle("localSupportToggle", true));
    EXPECT_TRUE(editor.surface().contains("reference_height_m"));
    EXPECT_TRUE(editor.surface().contains("local_support_patch"));
    surface_tabs->setProperty("currentIndex", 2);
    QCoreApplication::processEvents();
    ASSERT_TRUE(toggle("sourceOcclusionToggle", true));
    ASSERT_TRUE(toggle("sourceUncertaintyToggle", true));
    EXPECT_TRUE(editor.surface().value("source_occlusion_box").toMap().contains("uncertainty"));
    auto *mode_choice = object->findChild<QObject *>("occlusionModeChoice");
    ASSERT_TRUE(mode_choice);
    ASSERT_TRUE(QMetaObject::invokeMethod(mode_choice, "picked",
                                          Q_ARG(QString, "bilinear_box_surface_v1")));
    QCoreApplication::processEvents();
    auto *support = object->findChild<QObject *>("supportToleranceField");
    ASSERT_TRUE(support);
    ASSERT_TRUE(QMetaObject::invokeMethod(support, "edited", Q_ARG(double, .000012)));
    EXPECT_DOUBLE_EQ(editor.surface()
                         .value("source_occlusion_box")
                         .toMap()
                         .value("support_tolerance_m")
                         .toDouble(),
                     .000012);
    auto *guard = object->findChild<QObject *>("guardTexelsField");
    ASSERT_TRUE(guard);
    ASSERT_TRUE(QMetaObject::invokeMethod(guard, "edited", Q_ARG(double, 1)));
    ASSERT_TRUE(QMetaObject::invokeMethod(mode_choice, "picked", Q_ARG(QString, "center")));
    EXPECT_FALSE(editor.surface().value("source_occlusion_box").toMap().contains("guard_texels"));
    EXPECT_FALSE(
        editor.surface().value("source_occlusion_box").toMap().contains("support_tolerance_m"));
    ASSERT_TRUE(QMetaObject::invokeMethod(mode_choice, "picked",
                                          Q_ARG(QString, "bilinear_box_surface_v1")));
    editor.setField("surface", "source_occlusion_box.support_tolerance_m", .000012);
    editor.setField("surface", "source_occlusion_box.uncertainty.radius_m", .005);
    editor.setField("surface", "source_occlusion_box.uncertainty.ambiguous_policy", "reject");
    const auto surface_screenshot = qEnvironmentVariable("SV_QML_SURFACE_SCREENSHOT");
    if (!surface_screenshot.isEmpty())
    {
        window.resize(380, 2200);
        QEventLoop capture_loop;
        QTimer::singleShot(120, &capture_loop, &QEventLoop::quit);
        capture_loop.exec();
        ASSERT_TRUE(window.grabWindow().save(surface_screenshot));
        window.resize(380, 1050);
    }
    ASSERT_TRUE(toggle("sourceUncertaintyToggle", false));
    ASSERT_TRUE(toggle("sourceOcclusionToggle", false));
    ASSERT_TRUE(toggle("referenceHeightToggle", false));
    ASSERT_TRUE(toggle("localSupportToggle", false));
    EXPECT_FALSE(editor.surface().contains("source_occlusion_box"));
    EXPECT_FALSE(editor.surface().contains("reference_height_m"));
    EXPECT_FALSE(editor.surface().contains("local_support_patch"));
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
    EXPECT_TRUE(log_warnings.messages().isEmpty())
        << log_warnings.messages().join('\n').toStdString();
    EXPECT_TRUE(warnings.isEmpty()) << warnings.join('\n').toStdString();
    item->setParentItem(nullptr);
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
