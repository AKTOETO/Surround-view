#include "bridge.hpp"
#include <QDateTime>
#include <QJsonDocument>
#include <QTimer>
#include <QtEndian>
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

Bridge::Bridge(QString directory, FrameProvider *p) : directory_(std::move(directory)), provider_(p)
{
    connect(&control_, &QLocalSocket::connected, this,
            [this]
            {
                command_ = 0;
                controlBytes_.clear();
                write(control_, 1, {{"role", "control"}});
            });
    connect(&data_, &QLocalSocket::connected, this,
            [this]
            {
                dataBytes_.clear();
                write(data_, 1,
                      {{"role", "data"}, {"session_id", session_}, {"data_token", token_}});
            });
    connect(&control_, &QLocalSocket::readyRead, this,
            [this] { consume(&control_, controlBytes_, true); });
    connect(&data_, &QLocalSocket::readyRead, this, [this] { consume(&data_, dataBytes_, false); });
    connect(&control_, &QLocalSocket::disconnected, this,
            [this]
            {
                data_.abort();
                status_ = "Сервер отключён";
                emit changed();
            });
    connect(&data_, &QLocalSocket::disconnected, this,
            [this]
            {
                status_ = "Видеоканал отключён";
                emit changed();
            });
    auto *retry = new QTimer(this);
    retry->setInterval(1000);
    connect(retry, &QTimer::timeout, this,
            [this]
            {
                if (control_.state() == QLocalSocket::UnconnectedState)
                {
                    control_.connectToServer(directory_ + "/control.sock");
                }
                else if (control_.state() == QLocalSocket::ConnectedState && !session_.isEmpty() &&
                         data_.state() == QLocalSocket::UnconnectedState)
                {
                    data_.connectToServer(directory_ + "/data.sock");
                }
            });
    retry->start();
    control_.connectToServer(directory_ + "/control.sock");
}

Bridge::~Bridge()
{
    // QLocalSocket may emit disconnected during destruction. Its callbacks must not access
    // strings/buffers after those members have already been destroyed.
    control_.disconnect(this);
    data_.disconnect(this);
    control_.abort();
    data_.abort();
}

void Bridge::write(QLocalSocket &socket, quint16 type, const QJsonObject &header)
{
    auto metadata = QJsonDocument(header).toJson(QJsonDocument::Compact);
    QByteArray bytes(24, '\0');
    bytes.replace(0, 4, "SV01");
    qToBigEndian<quint16>(1, reinterpret_cast<uchar *>(bytes.data() + 4));
    qToBigEndian(type, reinterpret_cast<uchar *>(bytes.data() + 6));
    qToBigEndian<quint32>(metadata.size() + 24, reinterpret_cast<uchar *>(bytes.data() + 8));
    qToBigEndian<quint64>(0, reinterpret_cast<uchar *>(bytes.data() + 12));
    if (socket.bytesToWrite() + bytes.size() + metadata.size() > 65536)
    {
        status_ = "Очередь управления заполнена";
        emit changed();
        return;
    }
    socket.write(bytes + metadata);
}

void Bridge::consume(QLocalSocket *socket, QByteArray &buffer, bool control)
{
    buffer += socket->readAll();
    if (buffer.size() > 64 * 1024 * 1024 + 65536)
    {
        socket->abort();
        return;
    }
    while (buffer.size() >= 24)
    {
        const auto *p = reinterpret_cast<const uchar *>(buffer.constData());
        auto type = qFromBigEndian<quint16>(p + 6);
        quint32 hs = qFromBigEndian<quint32>(p + 8);
        quint64 ps = qFromBigEndian<quint64>(p + 12);
        if (buffer.left(4) != "SV01" || qFromBigEndian<quint16>(p + 4) != 1 ||
            qFromBigEndian<quint32>(p + 20) != 0 || hs < 24 || hs > 65536 || ps > 64 * 1024 * 1024)
        {
            socket->abort();
            return;
        }
        if (quint64(buffer.size()) < hs + ps)
        {
            return;
        }
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(buffer.mid(24, hs - 24), &error);
        auto h = doc.object();
        QByteArray payload = buffer.mid(hs, ps);
        buffer.remove(0, hs + ps);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
        {
            socket->abort();
            return;
        }
        if (type == 2 && control)
        {
            session_ = h["session_id"].toString();
            token_ = h["data_token"].toString();
            data_.connectToServer(directory_ + "/data.sock");
        }
        else if (type == 21 && control && !h["accepted"].toBool())
        {
            status_ = "Команда отклонена: " + h["reason"].toString();
            emit changed();
        }
        else if (type == 11 && !control)
        {
            int w = h["width"].toInt(), height = h["height"].toInt();
            if (w < 2 || w > 2048 || height < 2 || height > 2048 || ps != quint64(w) * height * 4 ||
                h["pixel_format"] != "RGBA8" || h["session_id"].toString() != session_)
            {
                socket->abort();
                return;
            }
            provider_->setImage(QImage(reinterpret_cast<const uchar *>(payload.constData()), w,
                                       height, w * 4, QImage::Format_RGBA8888)
                                    .copy());
            url_ = "image://frames/" + h["frame_id"].toString();
            double age =
                (monotonic() - h["oldest_release_timestamp_ns"].toString().toLongLong()) / 1e6;
            status_ = h["health"].toString() + (h["paused"].toBool() ? " · Пауза" : "") +
                      QString(" · возраст %1 мс · обработка %2 мс")
                          .arg(age, 0, 'f', 1)
                          .arg(h["render_readback_ms"].toDouble(), 0, 'f', 2);
            write(data_, 22,
                  {{"session_id", session_},
                   {"frame_id", h["frame_id"]},
                   {"buffer_token", h["buffer_token"]}});
            emit changed();
            std::cout << "{\"event\":\"ui_receive\",\"frame_id\":\""
                      << h["frame_id"].toString().toStdString() << "\",\"timestamp_ns\":\""
                      << monotonic() << "\"}" << std::endl;
        }
    }
}

void Bridge::command(QJsonObject object)
{
    if (control_.state() != QLocalSocket::ConnectedState)
    {
        return;
    }
    object["command_id"] = QString::number(++command_);
    object["ui_event_timestamp_ns"] = QString::number(monotonic());
    write(control_, 20, object);
}

void Bridge::preset(QString name)
{
    command({{"type", "preset"}, {"name", name}});
}

void Bridge::orbit(double az, double elevation)
{
    command({{"type", "orbit"}, {"azimuth_delta_rad", az}, {"elevation_delta_rad", elevation}});
}

void Bridge::zoom(double distance)
{
    command({{"type", "zoom"}, {"distance_delta_m", distance}});
}

void Bridge::action(QString type)
{
    command({{"type", type}});
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
