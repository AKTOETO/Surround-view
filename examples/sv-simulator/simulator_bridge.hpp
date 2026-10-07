#pragma once

#include "sv/client.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <atomic>
#include <memory>

class SimulatorFrameProvider : public QQuickImageProvider
{
    QMutex mutex_;
    QImage image_;

public:
    SimulatorFrameProvider() : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    void setImage(QImage image);
    QImage requestImage(const QString &, QSize *, const QSize &) override;
};

class SimulatorBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString frameUrl READ frameUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString serverInfo READ serverInfo NOTIFY changed)
    Q_PROPERTY(QString calibrationStatus READ calibrationStatus NOTIFY changed)
    Q_PROPERTY(double vehicleSpeed READ vehicleSpeed WRITE setVehicleSpeed NOTIFY vehicleChanged)
    Q_PROPERTY(double vehicleSteering READ vehicleSteering WRITE setVehicleSteering NOTIFY vehicleChanged)

public:
    SimulatorBridge(sv::client::Endpoint endpoint, SimulatorFrameProvider *provider);
    ~SimulatorBridge() override;

    QString frameUrl() const { return url_; }
    QString status() const { return status_; }
    QString serverInfo() const { return serverInfo_; }
    QString calibrationStatus() const { return calibrationStatus_; }
    double vehicleSpeed() const { return vehicleSpeed_; }
    double vehicleSteering() const { return vehicleSteering_; }

    Q_INVOKABLE void preset(const QString &name);
    Q_INVOKABLE void orbit(double az, double elevation);
    Q_INVOKABLE void zoom(double distance);
    Q_INVOKABLE void action(const QString &type);
    Q_INVOKABLE void submitCalibration(int cameraId, const QString &method);
    Q_INVOKABLE void checkCalibrationStatus(const QString &jobId);
    Q_INVOKABLE void applyCalibration(const QString &jobId);
    Q_INVOKABLE void setVehicleSpeed(double speed);
    Q_INVOKABLE void setVehicleSteering(double steering);
    Q_INVOKABLE void resetVehicle();

signals:
    void changed();
    void vehicleChanged();

private:
    void consume(sv::client::Event event);
    void command(const QString &type, boost::json::object parameters = {});

    std::unique_ptr<sv::client::Client> client_;
    QString url_;
    QString status_ = "Соединение с сервером…";
    QString serverInfo_ = "Ожидание ответа сервера";
    QString calibrationStatus_ = "Калибровочные задачи не запускались";
    SimulatorFrameProvider *provider_;
    std::atomic<unsigned> pending_events_{0};

    double vehicleSpeed_ = 0.0;
    double vehicleSteering_ = 0.0;
    double vehicleX_ = 0.0;
    double vehicleY_ = 0.0;
    double vehicleYaw_ = 0.0;
    QString lastCalibJobId_;
};
