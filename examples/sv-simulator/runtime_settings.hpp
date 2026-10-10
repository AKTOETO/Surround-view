#pragma once

#include "sv/client.hpp"
#include <QObject>
#include <QString>
#include <memory>

// GUI adapter only: authoritative validation and configuration live in sv-server.
class RuntimeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString revision READ revision NOTIFY changed)
    Q_PROPERTY(QString fusionJson READ fusionJson NOTIFY changed)
    Q_PROPERTY(QString surfaceJson READ surfaceJson NOTIFY changed)
    Q_PROPERTY(QString catalogJson READ catalogJson NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)

  public:

    using QObject::QObject;

    QString revision() const
    {
        return revision_;
    }

    QString fusionJson() const
    {
        return fusion_;
    }

    QString surfaceJson() const
    {
        return surface_;
    }

    QString catalogJson() const
    {
        return catalog_;
    }

    QString status() const
    {
        return status_;
    }

    bool ready() const
    {
        return !revision_.isEmpty() && !client_.expired();
    }

    bool pending() const
    {
        return pending_id_ != 0;
    }

    void attach(const std::shared_ptr<sv::client::Client> &client);
    void consume(const boost::json::object &header);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void applyFusion(const QString &json, const QString &baseRevision);
    Q_INVOKABLE void applySurface(const QString &json, const QString &baseRevision);

  signals:
    void changed();

  private:

    void apply(const QString &json, const QString &baseRevision, bool fusion);
    std::weak_ptr<sv::client::Client> client_;
    QString revision_, fusion_, surface_, catalog_;
    QString status_ = "Ожидание состояния сервера";
    boost::json::object catalogs_;
    uint64_t pending_id_ = 0;
};
