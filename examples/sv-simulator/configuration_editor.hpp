#pragma once
#include "sv/client.hpp"
#include <QObject>
#include <QVariantMap>
#include <optional>

// GUI-thread model. Config files and semantic validation remain exclusively server-owned.
class ConfigurationEditor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap telemetry READ telemetry NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap fusion READ fusion NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap surface READ surface NOTIFY draftChanged)
    Q_PROPERTY(QVariantMap activeSurface READ activeSurface NOTIFY changed)
    Q_PROPERTY(QVariantMap catalog READ catalog NOTIFY changed)
    Q_PROPERTY(QString baseRevision READ baseRevision NOTIFY draftChanged)
    Q_PROPERTY(QString serverRevision READ serverRevision NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString operationId READ operationId NOTIFY changed)
    Q_PROPERTY(QString operationState READ operationState NOTIFY changed)
    Q_PROPERTY(QVariantList comparison READ comparison NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool unresolved READ unresolved NOTIFY changed)
    Q_PROPERTY(bool serverChanged READ serverChanged NOTIFY changed)
    Q_PROPERTY(bool canRetry READ canRetry NOTIFY changed)
    Q_PROPERTY(bool hasRuntimeChanges READ hasRuntimeChanges NOTIFY changed)
    Q_PROPERTY(bool canPersist READ canPersist NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(bool validated READ validated NOTIFY changed)

  public:

    using QObject::QObject;

    QVariantMap telemetry() const
    {
        return telemetry_;
    }

    QVariantMap fusion() const
    {
        return fusion_;
    }

    QVariantMap surface() const
    {
        return surface_;
    }

    QVariantMap activeSurface() const;

    QVariantMap catalog() const
    {
        return catalog_;
    }

    QString baseRevision() const
    {
        return base_revision_;
    }

    QString serverRevision() const
    {
        return snapshot_loaded_ ? QString::number(snapshot_.revision) : QString("—");
    }

    QString status() const
    {
        return status_;
    }

    QString operationId() const;
    QString operationState() const;
    QVariantList comparison() const;

    bool ready() const
    {
        return connected_ && snapshot_loaded_ && catalog_loaded_;
    }

    bool busy() const
    {
        return read_id_ || catalog_id_ || validate_id_ || apply_id_ || status_id_;
    }

    bool unresolved() const;
    bool canRetry() const;
    bool serverChanged() const;

    bool hasRuntimeChanges() const
    {
        return snapshot_loaded_ && snapshot_.has_runtime_changes;
    }

    bool canPersist() const
    {
        return ready() && snapshot_.persistence_configured;
    }

    bool canApply() const;

    bool validated() const
    {
        return validated_;
    }

    void attach(const std::shared_ptr<sv::client::Client> &client);
    void connected();
    void disconnected();
    void consume(const sv::Message &message);
    void commandFailed(const boost::json::object &header, const std::string &reason);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadActive();
    Q_INVOKABLE void setField(const QString &group, const QString &field, const QVariant &value);
    Q_INVOKABLE void setOptionalEnabled(const QString &field, bool enabled);
    Q_INVOKABLE void setSurfaceType(const QString &type);
    Q_INVOKABLE void validate();
    Q_INVOKABLE void apply(bool persist);
    Q_INVOKABLE void checkOperation();
    Q_INVOKABLE void retryOperation();
    Q_INVOKABLE void acknowledgeUnknown();

  signals:
    void changed();
    void draftChanged();

  private:

    friend struct ConfigurationEditorTestAccess;

    struct Operation
    {
        enum class State
        {
            Awaiting,
            Unknown,
            NotFound,
            Committed,
            Rejected,
            InstanceChanged
        };
        std::string instance, id;
        uint64_t base_revision;
        boost::json::object changes;
        bool persist;
        State state = State::Awaiting;
    };

    static bool replaceDraftPath(QVariantMap &draft, const QString &field, const QVariant &value);
    void normalizeOcclusionMode();
    boost::json::object changes() const;
    std::shared_ptr<sv::client::Client> client() const;
    void receiveOperation(const sv::Message &message);
    void reconcile();
    void sendOperation();
    void fail(const std::exception &error);

    std::weak_ptr<sv::client::Client> client_;
    bool connected_ = false, snapshot_loaded_ = false, catalog_loaded_ = false;
    bool operations_supported_ = false, apply_supported_ = false, validated_ = false;
    sv::client::ConfigurationSnapshot snapshot_;
    QVariantMap fusion_, surface_, catalog_, active_fusion_, telemetry_, active_telemetry_;
    QString base_revision_, status_ = "Загрузите конфигурацию сервера";
    std::optional<Operation> operation_;
    uint64_t read_id_ = 0, catalog_id_ = 0, validate_id_ = 0, apply_id_ = 0, status_id_ = 0;
};
