#include "simulator_bridge.hpp"
#include "ipc_discovery.hpp"
#include <QDir>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
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
    QSettings settings("MAI", "surround-view-simulator");
    unixDirectory_ = settings.value("connection/unix_directory", unixDirectory_).toString();
    tcpHost_ = settings.value("connection/tcp_host", tcpHost_).toString();
    controlPort_ = settings.value("connection/control_port", controlPort_).toInt();
    dataPort_ = settings.value("connection/data_port", dataPort_).toInt();
    timeoutMs_ = settings.value("connection/timeout_ms", timeoutMs_).toInt();
    reconnectMs_ = settings.value("connection/reconnect_ms", reconnectMs_).toInt();
    maxRetries_ = settings.value("connection/max_retries", maxRetries_).toInt();

    if (endpoint.transport == sv::client::Endpoint::Transport::Unix && endpoint.directory.empty())
    {
        discoverLocal(timeoutMs_, reconnectMs_);
    }
    else
    {
        beginConnection(std::move(endpoint), timeoutMs_, reconnectMs_, maxRetries_,
                        endpoint.transport == sv::client::Endpoint::Transport::Unix
                            ? unixDirectory_
                            : tcpHost_);
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
        if (event.detail == "ready")
        {
            discovery_candidates_.clear();
            status_ = "Подключено к серверу";
            if (active_unix_endpoint_)
            {
                QSettings settings("MAI", "surround-view-simulator");
                settings.setValue("connection/unix_directory", unixDirectory_);
            }
        }
        else if (event.detail == "retry_exhausted" && !discovery_candidates_.isEmpty())
        {
            tryNextDiscoveryCandidate();
            return;
        }
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
    const auto gpu = h.at("pipeline_spans_ms").as_object().at("gpu_draw");
    pipelineInfo_ = QString("Источник: %1 · fusion: %2 · view: %3\n"
                            "server receive → render: %4 мс · render wall: %5 мс · GPU draw: %6")
                        .arg(QString::fromStdString(std::string(h.at("source_type").as_string())))
                        .arg(QString::fromStdString(std::string(h.at("fusion_mode").as_string())))
                        .arg(QString::fromStdString(std::string(h.at("diagnostic_view").as_string())))
                        .arg(h.at("server_receive_to_render_ms").as_double(), 0, 'f', 2)
                        .arg(h.at("render_readback_ms").as_double(), 0, 'f', 2)
                        .arg(gpu.is_double() ? QString::number(gpu.as_double(), 'f', 2) + " мс"
                                             : QString("нет GPU timer"));

    QStringList cameras;
    for (const auto &input : h.at("inputs").as_array())
    {
        const auto &item = input.as_object();
        const auto id = item.at("camera_id").as_int64();
        const auto calibration = QString::fromStdString(
            std::string(item.at("calibration_id").as_string()));
        cameras << QString("Камера %1: %2 · %3")
                       .arg(id)
                       .arg(item.at("used").as_bool() ? "кадр использован" : "нет кадра")
                       .arg(calibration);
    }
    sourceInfo_ = QString("Ревизия конфигурации: %1 · размер кадра %2×%3\n%4")
                      .arg(QString::fromStdString(std::string(h.at("state_revision").as_string())))
                      .arg(w)
                      .arg(height)
                      .arg(cameras.join('\n'));
    if (client_)
    {
        client_->release(h);
    }
    emit changed();
    emit frameReceived();
}

void SimulatorBridge::beginConnection(sv::client::Endpoint endpoint, int timeout,
                                      int reconnect, int retries, const QString &label)
{
    if (timeout < 1 || timeout > 60000 || reconnect < 1 || reconnect > 60000 ||
        retries < 0 || retries > 1000)
    {
        status_ = "Проверьте timeout, интервал reconnect и число попыток";
        emit changed();
        return;
    }

    ++connection_generation_;
    if (client_)
    {
        client_->stop();
        client_.reset();
    }
    pending_events_ = 0;
    status_ = "Подключение: " + label;
    timeoutMs_ = timeout;
    reconnectMs_ = reconnect;
    maxRetries_ = retries;
    QSettings settings("MAI", "surround-view-simulator");
    settings.setValue("connection/timeout_ms", timeoutMs_);
    settings.setValue("connection/reconnect_ms", reconnectMs_);
    settings.setValue("connection/max_retries", maxRetries_);
    emit changed();

    sv::client::Options options;
    active_unix_endpoint_ = endpoint.transport == sv::client::Endpoint::Transport::Unix;
    options.endpoint = std::move(endpoint);
    options.timeout_ms = static_cast<unsigned>(timeoutMs_);
    options.reconnect_ms = static_cast<unsigned>(reconnectMs_);
    options.max_retries = static_cast<unsigned>(maxRetries_);
    const auto generation = connection_generation_.load();
    auto client_ref = std::make_shared<std::weak_ptr<sv::client::Client>>();
    auto deliver = [this, generation, client_ref](sv::client::Event event)
    {
        if (generation != connection_generation_.load())
        {
            return;
        }
        if (pending_events_.fetch_add(1) >= 128)
        {
            if (event.kind == sv::client::Event::Kind::Message && event.message &&
                event.message->type == 11)
            {
                if (const auto client = client_ref->lock())
                {
                    client->release(event.message->header);
                }
            }
            --pending_events_;
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, event = std::move(event)]() mutable
            {
                if (generation == connection_generation_.load())
                {
                    --pending_events_;
                    consume(std::move(event));
                }
            },
            Qt::QueuedConnection);
    };
    try
    {
        client_ = std::make_shared<sv::client::Client>(options, std::move(deliver));
        *client_ref = client_;
    }
    catch (const std::exception &error)
    {
        status_ = QString("Ошибка подключения: ") + QString::fromUtf8(error.what());
        emit changed();
    }
}

void SimulatorBridge::connectUnix(const QString &directory, int timeout,
                                  int reconnect, int retries)
{
    discovery_candidates_.clear();
    unixDirectory_ = QDir::cleanPath(directory.trimmed());
    if (!QDir::isAbsolutePath(unixDirectory_))
    {
        status_ = "Путь Unix socket должен быть абсолютным";
        emit changed();
        return;
    }
    QSettings settings("MAI", "surround-view-simulator");
    settings.setValue("connection/unix_directory", unixDirectory_);
    sv::client::Endpoint endpoint;
    endpoint.transport = sv::client::Endpoint::Transport::Unix;
    endpoint.directory = unixDirectory_.toStdString();
    beginConnection(std::move(endpoint), timeout, reconnect, retries, unixDirectory_);
}

void SimulatorBridge::connectTcp(const QString &host, int controlPort, int dataPort,
                                int timeout, int reconnect, int retries)
{
    discovery_candidates_.clear();
    if (host.trimmed().isEmpty() || controlPort < 1 || controlPort > 65535 ||
        dataPort < 1 || dataPort > 65535 || controlPort == dataPort)
    {
        status_ = "Укажите host и два разных порта 1..65535";
        emit changed();
        return;
    }
    tcpHost_ = host.trimmed();
    controlPort_ = controlPort;
    dataPort_ = dataPort;
    QSettings settings("MAI", "surround-view-simulator");
    settings.setValue("connection/tcp_host", tcpHost_);
    settings.setValue("connection/control_port", controlPort_);
    settings.setValue("connection/data_port", dataPort_);
    sv::client::Endpoint endpoint;
    endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    endpoint.host = tcpHost_.toStdString();
    endpoint.control_port = static_cast<uint16_t>(controlPort_);
    endpoint.data_port = static_cast<uint16_t>(dataPort_);
    beginConnection(std::move(endpoint), timeout, reconnect, retries,
                    QString("%1:%2/%3").arg(tcpHost_).arg(controlPort_).arg(dataPort_));
}

void SimulatorBridge::discoverLocal(int timeout, int reconnect)
{
    if (timeout < 1 || timeout > 60000 || reconnect < 1 || reconnect > 60000)
    {
        status_ = "Для поиска задайте timeout и reconnect в диапазоне 1..60000 мс";
        emit changed();
        return;
    }
    ++connection_generation_;
    if (client_)
    {
        client_->stop();
        client_.reset();
    }
    pending_events_ = 0;
    discovery_candidates_.clear();
    discovery_index_ = 0;
    discovery_timeout_ms_ = timeout;
    discovery_reconnect_ms_ = reconnect;

    QStringList candidates;
    const auto env = QProcessEnvironment::systemEnvironment();
    if (env.contains("SV_IPC_DIR"))
    {
        candidates << env.value("SV_IPC_DIR");
    }
    candidates << unixDirectory_;
    const auto runtime = env.value("XDG_RUNTIME_DIR");
    if (!runtime.isEmpty())
    {
        candidates << QDir(runtime).filePath("sv-prototype");
    }
    candidates << "/tmp/sv-prototype" << "/tmp/sv-street" << "/tmp/sv-blender";

    discovery_candidates_ = discover_local_ipc_candidates(candidates);

    if (discovery_candidates_.isEmpty())
    {
        status_ = "Локальные IPC endpoints не найдены; укажите путь или TCP вручную";
        emit changed();
        return;
    }
    tryNextDiscoveryCandidate();
}

void SimulatorBridge::tryNextDiscoveryCandidate()
{
    if (discovery_index_ >= discovery_candidates_.size())
    {
        discovery_candidates_.clear();
        ++connection_generation_;
        if (client_)
        {
            client_->stop();
            client_.reset();
        }
        pending_events_ = 0;
        status_ = "IPC сокеты найдены, но сервер не ответил; проверьте права и конфигурацию";
        emit changed();
        return;
    }
    unixDirectory_ = discovery_candidates_.at(discovery_index_++);
    sv::client::Endpoint endpoint;
    endpoint.transport = sv::client::Endpoint::Transport::Unix;
    endpoint.directory = unixDirectory_.toStdString();
    beginConnection(std::move(endpoint), discovery_timeout_ms_, discovery_reconnect_ms_, 0,
                    "поиск IPC: " + unixDirectory_);
}

void SimulatorBridge::disconnectFromServer()
{
    ++connection_generation_;
    discovery_candidates_.clear();
    if (client_)
    {
        client_->stop();
        client_.reset();
    }
    status_ = "Отключено";
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

void SimulatorBridge::submitCalibration(int, const QString &)
{
    calibrationStatus_ =
        "Калибровка не запущена: GUI пока не передаёт измеренные observations. "
        "Фиктивные точки не используются.";
    emit changed();
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
