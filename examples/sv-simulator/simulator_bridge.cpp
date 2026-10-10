#include "simulator_bridge.hpp"
#include "ipc_discovery.hpp"
#include <QDir>
#include <QProcessEnvironment>
#include <QSettings>
#include <QStandardPaths>
#include <QUrl>
#include <chrono>
#include <iostream>
#include <mutex>

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
        const auto label = endpoint.transport == sv::client::Endpoint::Transport::Unix
                               ? QString::fromStdString(endpoint.directory)
                               : QString::fromStdString(endpoint.host);
        beginConnection(std::move(endpoint), timeoutMs_, reconnectMs_, maxRetries_, label);
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
        if (event.kind == sv::client::Event::Kind::Error && event.message)
        {
            runtime_settings_.commandFailed(event.message->header, event.detail);
            const auto *id = event.message->header.if_contains("command_id");
            if (id &&
                calibration_commands_.erase(sv::parse_decimal_u64(std::string(id->as_string()))))
            {
                calibrationStatus_ =
                    "Запрос калибровки не подтверждён: " + QString::fromStdString(event.detail);
            }
        }
        status_ = QString::fromStdString(event.detail);
        if (event.detail == "ready")
        {
            runtime_settings_.refresh();
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
        else if (event.kind == sv::client::Event::Kind::State)
        {
            resetSessionState();
            runtime_settings_.attach(client_);
        }
        emit changed();
        return;
    }
    const auto &m = *event.message;
    const auto &h = m.header;

    if (m.type == 21)
    {
        runtime_settings_.consume(h);
    }

    if (m.type == 21 && h.at("accepted").as_bool())
    {
        uint64_t command_id = 0;
        if (h.contains("command_id"))
        {
            command_id = sv::parse_decimal_u64(std::string_view(
                h.at("command_id").as_string().data(), h.at("command_id").as_string().size()));
        }
        auto calibration_command = calibration_commands_.find(command_id);
        if (calibration_command != calibration_commands_.end())
        {
            if (calibration_command->second == "cancel_calibration")
            {
                calibrationStatus_ = "Сервер подтвердил запрос отмены калибровочной задачи";
            }
            else if (calibration_command->second == "apply_calibration")
            {
                calibrationStatus_ = "Сервер применил калибровку и сохранил новую конфигурацию";
            }
            calibration_commands_.erase(calibration_command);
        }
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
                if (h.contains("validation_rmse_px"))
                {
                    calibrationStatus_ +=
                        QString(" · validation: %1 px · max: %2 px · %3")
                            .arg(h.at("validation_rmse_px").as_double(), 0, 'f', 3)
                            .arg(h.at("validation_max_error_px").as_double(), 0, 'f', 3)
                            .arg(h.at("quality_accepted").as_bool() ? "принята" : "отклонена");
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
        if (h.contains("command_id"))
        {
            const auto command_id = sv::parse_decimal_u64(std::string_view(
                h.at("command_id").as_string().data(), h.at("command_id").as_string().size()));
            auto calibration_command = calibration_commands_.find(command_id);
            if (calibration_command != calibration_commands_.end())
            {
                calibrationStatus_ =
                    "Команда калибровки отклонена: " +
                    QString::fromStdString(std::string(h.at("reason").as_string()));
                calibration_commands_.erase(calibration_command);
            }
        }
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
    const auto session = QString::fromStdString(std::string(h.at("session_id").as_string()));
    url_ = "image://frames/" + QString::fromLatin1(QUrl::toPercentEncoding(session)) + "/" + frame;

    status_ = QString::fromStdString(std::string(h.at("health").as_string())) +
              (h.at("paused").as_bool() ? " · Пауза" : "") +
              QString(" · рендеринг %1 мс").arg(h.at("render_readback_ms").as_double(), 0, 'f', 2);
    const auto gpu = h.at("pipeline_spans_ms").as_object().at("gpu_draw");
    pipelineInfo_ =
        QString("Источник: %1 · fusion: %2 · view: %3\n"
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
        const auto calibration =
            QString::fromStdString(std::string(item.at("calibration_id").as_string()));
        cameras << QString("Камера %1: %2 · %3")
                       .arg(id)
                       .arg(item.at("used").as_bool() ? "кадр использован" : "нет кадра")
                       .arg(calibration);
    }
    sourceInfo_ = QString("Ревизия конфигурации: %1 · размер кадра %2×%3\n%4")
                      .arg(QString::fromStdString(std::string(h.at("config_revision").as_string())))
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

void SimulatorBridge::resetSessionState()
{
    lastCalibJobId_.clear();
    calibration_commands_.clear();
    calibrationStatus_ = "Нет калибровочной задачи в текущей сессии";
    serverInfo_ = "Ожидание состояния текущей сессии";
    pipelineInfo_ = "Нет данных о задержках текущей сессии";
    sourceInfo_ = "Источник и камеры появятся после первого кадра текущей сессии";
    url_.clear();
    provider_->setImage({});
}

void SimulatorBridge::beginConnection(sv::client::Endpoint endpoint, int timeout, int reconnect,
                                      int retries, const QString &label)
{
    if (timeout < 1 || timeout > 60000 || reconnect < 1 || reconnect > 60000 || retries < 0 ||
        retries > 1000)
    {
        status_ = "Проверьте timeout, интервал reconnect и число попыток";
        emit changed();
        return;
    }
    const bool unixEndpoint = endpoint.transport == sv::client::Endpoint::Transport::Unix;
    if ((unixEndpoint && (endpoint.directory.empty() || endpoint.directory.size() > 80 ||
                          !QDir::isAbsolutePath(QString::fromStdString(endpoint.directory)))) ||
        (!unixEndpoint && (endpoint.host.empty() || !endpoint.control_port || !endpoint.data_port ||
                           endpoint.control_port == endpoint.data_port)))
    {
        status_ = "Недопустимый endpoint; текущее соединение сохранено";
        emit changed();
        return;
    }

    ++connection_generation_;
    if (client_)
    {
        client_->stop();
        client_.reset();
    }
    pending_frames_ = 0;
    resetSessionState();
    runtime_settings_.attach({});
    status_ = "Подключение: " + label;
    timeoutMs_ = timeout;
    reconnectMs_ = reconnect;
    maxRetries_ = retries;
    QSettings settings("MAI", "surround-view-simulator");
    if (unixEndpoint)
    {
        unixDirectory_ = QString::fromStdString(endpoint.directory);
    }
    else
    {
        tcpHost_ = QString::fromStdString(endpoint.host);
        controlPort_ = endpoint.control_port;
        dataPort_ = endpoint.data_port;
        settings.setValue("connection/tcp_host", tcpHost_);
        settings.setValue("connection/control_port", controlPort_);
        settings.setValue("connection/data_port", dataPort_);
    }
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
    auto reference_mutex = std::make_shared<std::mutex>();
    auto deliver = [this, generation, client_ref, reference_mutex](sv::client::Event event)
    {
        if (generation != connection_generation_.load())
        {
            return;
        }
        const bool frame = event.kind == sv::client::Event::Kind::Message && event.message &&
                           event.message->type == 11;
        // Only frames may be dropped. Losing an ACK/error/disconnect can leave a
        // command pending forever or keep a snapshot from the previous session.
        if (frame && pending_frames_.fetch_add(1) >= 128)
        {
            --pending_frames_;
            std::shared_ptr<sv::client::Client> client;
            {
                std::lock_guard<std::mutex> lock(*reference_mutex);
                client = client_ref->lock();
            }
            if (client)
            {
                client->release(event.message->header);
            }
            return;
        }
        QMetaObject::invokeMethod(
            this,
            [this, generation, frame, event = std::move(event)]() mutable
            {
                if (generation == connection_generation_.load())
                {
                    if (frame)
                    {
                        --pending_frames_;
                    }
                    consume(std::move(event));
                }
            },
            Qt::QueuedConnection);
    };
    try
    {
        client_ = std::make_shared<sv::client::Client>(options, std::move(deliver));
        {
            std::lock_guard<std::mutex> lock(*reference_mutex);
            *client_ref = client_;
        }
        runtime_settings_.attach(client_);
    }
    catch (const std::exception &error)
    {
        status_ = QString("Ошибка подключения: ") + QString::fromUtf8(error.what());
        emit changed();
    }
}

void SimulatorBridge::connectUnix(const QString &directory, int timeout, int reconnect, int retries)
{
    discovery_candidates_.clear();
    const auto path = QDir::cleanPath(directory.trimmed());
    if (!QDir::isAbsolutePath(path))
    {
        status_ = "Путь Unix socket должен быть абсолютным";
        emit changed();
        return;
    }
    sv::client::Endpoint endpoint;
    endpoint.transport = sv::client::Endpoint::Transport::Unix;
    endpoint.directory = path.toStdString();
    beginConnection(std::move(endpoint), timeout, reconnect, retries, path);
}

void SimulatorBridge::connectTcp(const QString &host, int controlPort, int dataPort, int timeout,
                                 int reconnect, int retries)
{
    discovery_candidates_.clear();
    if (host.trimmed().isEmpty() || controlPort < 1 || controlPort > 65535 || dataPort < 1 ||
        dataPort > 65535 || controlPort == dataPort)
    {
        status_ = "Укажите host и два разных порта 1..65535";
        emit changed();
        return;
    }
    sv::client::Endpoint endpoint;
    endpoint.transport = sv::client::Endpoint::Transport::Tcp;
    endpoint.host = host.trimmed().toStdString();
    endpoint.control_port = static_cast<uint16_t>(controlPort);
    endpoint.data_port = static_cast<uint16_t>(dataPort);
    beginConnection(std::move(endpoint), timeout, reconnect, retries,
                    QString("%1:%2/%3").arg(host.trimmed()).arg(controlPort).arg(dataPort));
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
    pending_frames_ = 0;
    discovery_candidates_.clear();
    resetSessionState();
    runtime_settings_.attach({});
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
        pending_frames_ = 0;
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
    resetSessionState();
    runtime_settings_.attach({});
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
        const auto command_id = client_->command(type.toStdString(), std::move(parameters));
        if (type == "calibration_status" || type == "cancel_calibration" ||
            type == "apply_calibration")
        {
            calibration_commands_[command_id] = type;
            while (calibration_commands_.size() > 64)
            {
                calibration_commands_.erase(calibration_commands_.begin());
            }
        }
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
    calibrationStatus_ = "Калибровка не запущена: GUI пока не передаёт измеренные observations. "
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

void SimulatorBridge::cancelCalibration(const QString &jobId)
{
    QString targetId = jobId.isEmpty() ? lastCalibJobId_ : jobId;
    if (targetId.isEmpty())
    {
        calibrationStatus_ = "Нет активных калибровочных задач";
        emit changed();
        return;
    }
    calibrationStatus_ = QString("Отправлен запрос отмены задачи %1").arg(targetId);
    emit changed();
    command("cancel_calibration", {{"job_id", targetId.toStdString()}});
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
