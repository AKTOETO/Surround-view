#pragma once
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QLocalSocket>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>

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
    QLocalSocket control_, data_;
    QByteArray controlBytes_, dataBytes_;
    QString directory_, session_, token_, url_, status_ = "Соединение с сервером…", readyFrame_,
                                                lastPresented_;
    FrameProvider *provider_;
    quint64 command_ = 0;
    void consume(QLocalSocket *, QByteArray &, bool);
    void write(QLocalSocket &, quint16, const QJsonObject &);
    void command(QJsonObject);

  public:

    Bridge(QString, FrameProvider *);

    QString frameUrl() const
    {
        return url_;
    }

    QString status() const
    {
        return status_;
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
