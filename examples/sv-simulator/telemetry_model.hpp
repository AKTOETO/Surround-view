#pragma once
#include "sv/client.hpp"
#include "sv/client_telemetry_archive.hpp"
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <deque>

// GUI-thread reader; never mutates the server or enables collection implicitly.
class TelemetryModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool offline READ offline NOTIFY changed)
    Q_PROPERTY(bool canImport READ canImport NOTIFY changed)
    Q_PROPERTY(bool canExport READ canExport NOTIFY changed)
    Q_PROPERTY(bool monitoring READ monitoring NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
  public:

    TelemetryModel();

    bool offline() const
    {
        return offline_;
    }

    bool canImport() const
    {
        return !monitoring_ && !catalog_id_ && !read_id_ && client_.expired();
    }

    bool canExport() const
    {
        return catalog_.has_value();
    }

    bool monitoring() const
    {
        return monitoring_;
    }

    QString status() const
    {
        return status_;
    }

    QString summary() const
    {
        return summary_;
    }

    QVariantList points() const
    {
        return points_;
    }

    void attach(const std::shared_ptr<sv::client::Client> &);
    void connected();
    void consume(const sv::Message &);
    void commandFailed(const boost::json::object &, const std::string &);
    Q_INVOKABLE void setMonitoring(bool);
    Q_INVOKABLE void clear();
    Q_INVOKABLE bool saveReport(const QUrl &, const QString &label);
    Q_INVOKABLE bool loadReport(const QUrl &);
  signals:
    void changed();

  private:

    friend struct TelemetryModelTestAccess;
    void poll();
    void acceptCatalog(const sv::client::TelemetryCatalog &);
    void acceptBatch(const sv::client::TelemetryBatch &);
    void resetHistory();
    void updateSummary();
    std::weak_ptr<sv::client::Client> client_;
    QTimer timer_;
    bool offline_ = false;
    std::deque<sv::client::TelemetrySample> samples_;
    bool monitoring_ = false, connected_ = false, supported_ = false;
    uint64_t catalog_id_ = 0, read_id_ = 0, cursor_ = 0, lost_ = 0;
    std::optional<uint64_t> origin_, last_timestamp_;
    std::optional<sv::client::TelemetryCatalog> catalog_;
    QVariantList points_;
    QString status_ = "Мониторинг выключен", summary_;
};
