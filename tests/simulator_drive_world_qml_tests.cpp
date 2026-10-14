#include "qml_warning_capture.hpp"
#include <QQmlComponent>
#include <QQmlEngine>
#include <QVector3D>
#include <cmath>
#include <gtest/gtest.h>

TEST(SimulatorDriveWorldQml, LoadsMovesTurnsAndResetsInputWithoutFinalPropertyConflicts)
{
#if !SV_TEST_QUICK3D
    GTEST_SKIP() << "Qt Quick 3D is not available";
#else
    QmlWarningCapture warnings;
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl("qrc:/DriveWorld.qml"));
    std::unique_ptr<QObject> world(component.create());
    ASSERT_TRUE(world) << component.errorString().toStdString();
    auto key = [&](int code, bool pressed)
    {
        QVariant handled;
        EXPECT_TRUE(QMetaObject::invokeMethod(world.get(), "handleKey",
                                              Q_RETURN_ARG(QVariant, handled),
                                              Q_ARG(QVariant, code), Q_ARG(QVariant, pressed)));
        return handled.toBool();
    };
    auto advance = [&](double dt)
    { EXPECT_TRUE(QMetaObject::invokeMethod(world.get(), "advanceVehicle", Q_ARG(QVariant, dt))); };
    EXPECT_TRUE(key(Qt::Key_W, true));
    EXPECT_TRUE(key(Qt::Key_D, true));
    advance(.1);
    EXPECT_GT(world->property("speed").toDouble(), 0);
    const auto heading = world->property("headingRad").toDouble();
    EXPECT_GT(heading, 0);
    EXPECT_GT(world->property("vehicleX").toDouble(), 0);
    EXPECT_LT(world->property("vehicleZ").toDouble(), 0);
    const auto *vehicle = world->findChild<QObject *>("driveVehicle");
    ASSERT_TRUE(vehicle);
    EXPECT_NEAR(vehicle->property("eulerRotation").value<QVector3D>().y(),
                -heading * 180 / std::acos(-1), 1e-5);
    const auto *body = world->findChild<QObject *>("driveVehicleBody");
    ASSERT_TRUE(body);
    EXPECT_NEAR(body->property("scale").value<QVector3D>().z() * 100, 3.1, 1e-5);
    EXPECT_TRUE(key(Qt::Key_W, false));
    EXPECT_TRUE(key(Qt::Key_D, false));
    const auto speed = world->property("speed").toDouble();
    advance(.1);
    EXPECT_LT(world->property("speed").toDouble(), speed);
    EXPECT_FALSE(world->property("steerRight").toBool());
    EXPECT_TRUE(key(Qt::Key_A, true));
    EXPECT_TRUE(key(Qt::Key_S, true));
    EXPECT_TRUE(world->setProperty("visible", false));
    EXPECT_FALSE(world->property("forward").toBool());
    EXPECT_FALSE(world->property("reverse").toBool());
    EXPECT_FALSE(world->property("steerLeft").toBool());
    EXPECT_FALSE(world->property("steerRight").toBool());
    EXPECT_TRUE(key(Qt::Key_Escape, true));
    EXPECT_FALSE(key(Qt::Key_F1, true));
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
#endif
}
