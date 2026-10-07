#include "simulator_bridge.hpp"
#include <chrono>
#include <iostream>

namespace
{
qint64 monotonic()
{
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void SimulatorFrameProvider::setImage(QImage image)
{
    QMutexLocker lock(&mutex_);
    image_ = std::move(image);
}

QImage SimulatorFrameProvider::requestImage(const QString &, QSize *size, const QSize &)
{
    QMutexLocker lock(&mutex_);
    if (size)
    {
        *size = image_.size();
    }
    return image_;
}

SimulatorBridge::SimulatorBridge(sv::client::Endpoint endpoint, SimulatorFrameProvider *p)
    : provider_(p)
{
    sv::client::Options options;
    options.endpoint = std::move(endpoint);
    auto deliver = [this](sv::client::Event event)
    {
        if (pending_events_.fetch_add(1) >= 64)
        {
            --pending_events_;
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, event = std::move(event)]() mutable
            {
                --pending_events_;
                consume(std::move(event));
            },
            Qt::QueuedConnection);
    };
    try
    {
        client_ = std::make_unique<sv::client::Client>(options, std::move(deliver));
    }
    catch (const std::exception &e)
    {
        status_ = QString("Ошибка подключения: ") + QString::fromUtf8(e.what());
    }
}

SimulatorBridge::~SimulatorBridge()
{
    if (client_)
    {
        client_->stop();
    }
}

void SimulatorBridge::consume(sv::client::Event event)
{
    if (event.kind != sv::client::Event::Kind::Message)
    {
        status_ = QString::fromStdString(event.detail);
        emit changed();
        return;
    }
    const auto &m = *event.message;
    const auto &h = m.header;

    if (m.type == 21 && h.at("accepted").as_bool())
    {
        serverInfo_ =
            QString("Азимут: %1 рад · Высота: %2 рад\nРасстояние: %3 м\nРевизия состояния: %4")
                .arg(h.at("azimuth_rad").as_double(), 0, 'f', 2)
                .arg(h.at("elevation_rad").as_double(), 0, 'f', 2)
                .arg(h.at("distance_m").as_double(), 0, 'f', 2)
                .arg(QString::fromStdString(std::string(h.at("state_revision").as_string())));

        if (h.contains("job_id"))
        {
            QString jid = QString::fromStdString(std::string(h.at("job_id").as_string()));
            lastCalibJobId_ = jid;
            if (h.contains("job_state"))
            {
                QString state = QString::fromStdString(std::string(h.at("job_state").as_string()));
                calibrationStatus_ = QString("Задача %1: %2").arg(jid, state);
                if (h.contains("training_rmse_px"))
                {
                    calibrationStatus_ += QString(" (RMSE: %1 px)")
                                              .arg(h.at("training_rmse_px").as_double(), 0, 'f', 3);
                }
            }
            else
            {
                calibrationStatus_ = QString("Создана калибровочная задача: %1").arg(jid);
            }
        }

        emit changed();
    }
    if (m.type == 21 && !h.at("accepted").as_bool())
    {
        status_ =
            "Команда отклонена: " + QString::fromStdString(std::string(h.at("reason").as_string()));
        emit changed();
    }
    if (m.type != 11)
    {
        return;
    }
    const int w = static_cast<int>(h.at("width").as_int64());
    const int height = static_cast<int>(h.at("height").as_int64());
    provider_->setImage(QImage(m.payload.data(), w, height, w * 4, QImage::Format_RGBA8888).copy());
    auto frame = QString::fromStdString(std::string(h.at("frame_id").as_string()));
    url_ = "image://frames/" + frame;

    status_ = QString::fromStdString(std::string(h.at("health").as_string())) +
              (h.at("paused").as_bool() ? " · Пауза" : "") +
              QString(" · рендеринг %1 мс").arg(h.at("render_readback_ms").as_double(), 0, 'f', 2);
    if (client_)
    {
        client_->release(h);
    }
    emit changed();
}

void SimulatorBridge::command(const QString &type, boost::json::object parameters)
{
    if (!client_)
    {
        status_ = "Нет подключения к серверу";
        emit changed();
        return;
    }
    try
    {
        parameters["ui_event_timestamp_ns"] = std::to_string(monotonic());
        client_->command(type.toStdString(), std::move(parameters));
    }
    catch (const std::exception &e)
    {
        status_ = QString::fromUtf8(e.what());
        emit changed();
    }
}

void SimulatorBridge::preset(const QString &name)
{
    command("preset", {{"name", name.toStdString()}});
}

void SimulatorBridge::orbit(double az, double elevation)
{
    command("orbit", {{"azimuth_delta_rad", az}, {"elevation_delta_rad", elevation}});
}

void SimulatorBridge::zoom(double distance)
{
    command("zoom", {{"distance_delta_m", distance}});
}

void SimulatorBridge::action(const QString &type)
{
    command(type);
}

void SimulatorBridge::submitCalibration(int cameraId, const QString &method)
{
    boost::json::array points = {
        boost::json::array{-1.0, 3.0, 0.0}, boost::json::array{1.0, 3.0, 0.0},
        boost::json::array{-1.0, 5.0, 0.0}, boost::json::array{1.0, 5.0, 0.0},
        boost::json::array{-0.5, 4.0, 0.5}, boost::json::array{0.5, 4.0, 0.5},
        boost::json::array{-1.2, 3.5, -0.2}, boost::json::array{1.2, 3.5, -0.2}
    };
    boost::json::array pixels = {
        boost::json::array{480.0, 240.0}, boost::json::array{800.0, 240.0},
        boost::json::array{520.0, 310.0}, boost::json::array{760.0, 310.0},
        boost::json::array{560.0, 260.0}, boost::json::array{720.0, 260.0},
        boost::json::array{470.0, 230.0}, boost::json::array{810.0, 230.0}
    };
    command("calibrate", {{"camera_id", cameraId},
                          {"points", points},
                          {"pixels", pixels},
                          {"method", method.toStdString()}});
}

void SimulatorBridge::checkCalibrationStatus(const QString &jobId)
{
    QString targetId = jobId.isEmpty() ? lastCalibJobId_ : jobId;
    if (targetId.isEmpty())
    {
        calibrationStatus_ = "Нет активных калибровочных задач";
        emit changed();
        return;
    }
    command("calibration_status", {{"job_id", targetId.toStdString()}});
}

void SimulatorBridge::applyCalibration(const QString &jobId)
{
    QString targetId = jobId.isEmpty() ? lastCalibJobId_ : jobId;
    if (targetId.isEmpty())
    {
        calibrationStatus_ = "Нет завершённых задач для применения";
        emit changed();
        return;
    }
    command("apply_calibration", {{"job_id", targetId.toStdString()}});
}

void SimulatorBridge::setVehicleSpeed(double speed)
{
    vehicleSpeed_ = speed;
    emit vehicleChanged();
}

void SimulatorBridge::setVehicleSteering(double steering)
{
    vehicleSteering_ = steering;
    emit vehicleChanged();
}

void SimulatorBridge::resetVehicle()
{
    vehicleSpeed_ = 0.0;
    vehicleSteering_ = 0.0;
    vehicleX_ = 0.0;
    vehicleY_ = 0.0;
    vehicleYaw_ = 0.0;
    emit vehicleChanged();
}
