#pragma once

#include "runtime_settings.hpp"
#include "sv/client.hpp"
#include <QElapsedTimer>
#include <QImage>
#include <QJsonObject>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QStringList>
#include <atomic>
#include <cstdint>
#include <map>
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
    friend struct SimulatorBridgeTestAccess;
    Q_OBJECT
    Q_PROPERTY(QString frameUrl READ frameUrl NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString serverInfo READ serverInfo NOTIFY changed)
    Q_PROPERTY(QString calibrationStatus READ calibrationStatus NOTIFY changed)
    Q_PROPERTY(QString unixDirectory READ unixDirectory NOTIFY changed)
    Q_PROPERTY(QString tcpHost READ tcpHost NOTIFY changed)
    Q_PROPERTY(int controlPort READ controlPort NOTIFY changed)
    Q_PROPERTY(int dataPort READ dataPort NOTIFY changed)
    Q_PROPERTY(int timeoutMs READ timeoutMs NOTIFY changed)
    Q_PROPERTY(int reconnectMs READ reconnectMs NOTIFY changed)
    Q_PROPERTY(int maxRetries READ maxRetries NOTIFY changed)
    Q_PROPERTY(QString pipelineInfo READ pipelineInfo NOTIFY changed)
    Q_PROPERTY(QString sourceInfo READ sourceInfo NOTIFY changed)

  public:

    SimulatorBridge(sv::client::Endpoint endpoint, SimulatorFrameProvider *provider);
    ~SimulatorBridge() override;

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

    QString calibrationStatus() const
    {
        return calibrationStatus_;
    }

    QString unixDirectory() const
    {
        return unixDirectory_;
    }

    QString tcpHost() const
    {
        return tcpHost_;
    }

    int controlPort() const
    {
        return controlPort_;
    }

    int dataPort() const
    {
        return dataPort_;
    }

    int timeoutMs() const
    {
        return timeoutMs_;
    }

    int reconnectMs() const
    {
        return reconnectMs_;
    }

    int maxRetries() const
    {
        return maxRetries_;
    }

    QString pipelineInfo() const
    {
        return pipelineInfo_;
    }

    QString sourceInfo() const
    {
        return sourceInfo_;
    }

    RuntimeSettings *runtimeSettings()
    {
        return &runtime_settings_;
    }

    Q_INVOKABLE void preset(const QString &name);
    Q_INVOKABLE void orbit(double az, double elevation);
    Q_INVOKABLE void zoom(double distance);
    Q_INVOKABLE void action(const QString &type);
    Q_INVOKABLE void submitCalibration(int cameraId, const QString &method);
    Q_INVOKABLE void checkCalibrationStatus(const QString &jobId);
    Q_INVOKABLE void cancelCalibration(const QString &jobId);
    Q_INVOKABLE void applyCalibration(const QString &jobId);
    Q_INVOKABLE void connectUnix(const QString &directory, int timeoutMs, int reconnectMs,
                                 int maxRetries);
    Q_INVOKABLE void connectTcp(const QString &host, int controlPort, int dataPort, int timeoutMs,
                                int reconnectMs, int maxRetries);
    Q_INVOKABLE void discoverLocal(int timeoutMs, int reconnectMs);
    Q_INVOKABLE void disconnectFromServer();

  signals:
    void changed();
    void frameReceived();

  private:

    void consume(sv::client::Event event);
    void command(const QString &type, boost::json::object parameters = {});
    void beginConnection(sv::client::Endpoint endpoint, int timeoutMs, int reconnectMs,
                         int maxRetries, const QString &label);
    void tryNextDiscoveryCandidate();
    void resetSessionState();

    std::shared_ptr<sv::client::Client> client_;
    RuntimeSettings runtime_settings_;
    QString url_;
    QString status_ = "Соединение с сервером…";
    QString serverInfo_ = "Ожидание ответа сервера";
    QString calibrationStatus_ = "Калибровочные задачи не запускались";
    SimulatorFrameProvider *provider_;
    std::atomic<unsigned> pending_frames_{0};
    std::atomic<uint64_t> connection_generation_{0};
    bool active_unix_endpoint_ = true;
    QStringList discovery_candidates_;
    int discovery_index_ = 0;
    int discovery_timeout_ms_ = 1000;
    int discovery_reconnect_ms_ = 500;

    QString unixDirectory_ = "/tmp/sv-prototype";
    QString tcpHost_ = "127.0.0.1";
    int controlPort_ = 53101;
    int dataPort_ = 53102;
    int timeoutMs_ = 2000;
    int reconnectMs_ = 1000;
    int maxRetries_ = 10;
    QString pipelineInfo_ = "Нет данных о задержках";
    QString sourceInfo_ = "Источник и камеры появятся после первого кадра";

    QString lastCalibJobId_;
    std::map<uint64_t, QString> calibration_commands_;
};
