#pragma once
#include "sv/client.hpp"
#include <QObject>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <optional>

class ResearchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(unsigned completed READ completed NOTIFY changed)
    Q_PROPERTY(unsigned total READ total NOTIFY changed)
    Q_PROPERTY(QString reportPath READ reportPath NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY changed)

  public:

    explicit ResearchController(QObject *parent = nullptr);
    ~ResearchController() override;
    void setConnection(std::optional<sv::client::Options>);
    bool busy() const;
    bool available() const;
    QString status() const;
    unsigned completed() const;
    unsigned total() const;
    QString reportPath() const;
    QVariantList rows() const;
    int exitCode() const;

    Q_INVOKABLE QString suggestOutput();
    Q_INVOKABLE bool startDraft(const QVariantMap &values, const QString &newDirectory);
    Q_INVOKABLE void cancel();

  signals:
    void changed();
    void finished();

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};
