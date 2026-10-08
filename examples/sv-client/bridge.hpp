#pragma once
#include "sv/client.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <atomic>

class FrameProvider : public QQuickImageProvider
{
    QMutex mutex_;
    QImage image_;

  public:

    FrameProvider() : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    void setImage(QImage image);
    QImage requestImage(const QString &, QSize *, const QSize &) override;
};

class Bridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString frameUrl READ frameUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString serverInfo READ serverInfo NOTIFY changed)
    Q_PROPERTY(QString pipelineInfo READ pipelineInfo NOTIFY changed)
    Q_PROPERTY(QString sourceInfo READ sourceInfo NOTIFY changed)
    std::unique_ptr<sv::client::Client> client_;
    QString url_, status_ = "Соединение с сервером…", readyFrame_, lastPresented_;
    QString serverInfo_ = "Ожидание ответа state";
    QString pipelineInfo_ = "Ожидание первого кадра";
    QString sourceInfo_ = "Информация об источниках появится после получения кадра";
    FrameProvider *provider_;
    std::atomic<unsigned> pending_events_{0};
    void consume(sv::client::Event);
    void command(QString, boost::json::object = {});

  public:

    Bridge(sv::client::Endpoint, FrameProvider *);
    ~Bridge() override;

    QString frameUrl() const
    {
        return url_;
    }

    QString status() const
    {
        return status_;
    }

    QString serverInfo() const
    {
        return serverInfo_;
    }

    QString pipelineInfo() const
    {
        return pipelineInfo_;
    }

    QString sourceInfo() const
    {
        return sourceInfo_;
    }

    Q_INVOKABLE void preset(QString);
    Q_INVOKABLE void orbit(double, double);
    Q_INVOKABLE void zoom(double);
    Q_INVOKABLE void action(QString);
    Q_INVOKABLE void imageReady(QString);
    void presented();
  signals:
    void changed();
};
