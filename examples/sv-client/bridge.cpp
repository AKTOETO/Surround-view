#include "bridge.hpp"
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

void FrameProvider::setImage(QImage image)
{
    QMutexLocker lock(&mutex_);
    image_ = std::move(image);
}

QImage FrameProvider::requestImage(const QString &, QSize *size, const QSize &)
{
    QMutexLocker lock(&mutex_);
    if (size)
    {
        *size = image_.size();
    }
    return image_;
}

Bridge::Bridge(sv::client::Endpoint endpoint, FrameProvider *p) : provider_(p)
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
    client_ = std::make_unique<sv::client::Client>(options, std::move(deliver));
}

Bridge::~Bridge()
{
    client_->stop();
}

void Bridge::consume(sv::client::Event event)
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
    const int w = h.at("width").as_int64(), height = h.at("height").as_int64();
    provider_->setImage(QImage(m.payload.data(), w, height, w * 4, QImage::Format_RGBA8888).copy());
    auto frame = QString::fromStdString(std::string(h.at("frame_id").as_string()));
    url_ = "image://frames/" + frame;
    // Server monotonic timestamps cannot be subtracted from a remote client's clock.
    status_ = QString::fromStdString(std::string(h.at("health").as_string())) +
              (h.at("paused").as_bool() ? " · Пауза" : "") +
              QString(" · обработка %1 мс").arg(h.at("render_readback_ms").as_double(), 0, 'f', 2);
    client_->release(h);
    emit changed();
    std::cout << "{\"event\":\"ui_receive\",\"frame_id\":\"" << frame.toStdString()
              << "\",\"timestamp_ns\":\"" << monotonic() << "\"}" << std::endl;
}

void Bridge::command(QString type, boost::json::object parameters)
{
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

void Bridge::preset(QString name)
{
    command("preset", {{"name", name.toStdString()}});
}

void Bridge::orbit(double az, double elevation)
{
    command("orbit", {{"azimuth_delta_rad", az}, {"elevation_delta_rad", elevation}});
}

void Bridge::zoom(double distance)
{
    command("zoom", {{"distance_delta_m", distance}});
}

void Bridge::action(QString type)
{
    command(type);
}

void Bridge::imageReady(QString url)
{
    readyFrame_ = url.section('/', -1);
}

void Bridge::presented()
{
    if (readyFrame_.isEmpty() || readyFrame_ == lastPresented_)
    {
        return;
    }
    lastPresented_ = readyFrame_;
    std::cout << "{\"event\":\"ui_present_submit\",\"frame_id\":\"" << readyFrame_.toStdString()
              << "\",\"timestamp_ns\":\"" << monotonic() << "\"}" << std::endl;
}
