#pragma once
#include "sv/client.hpp"
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>
#include <optional>

class ResearchController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool batchMode READ batchMode NOTIFY changed)
    Q_PROPERTY(QVariantList batchRows READ batchRows NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(unsigned completed READ completed NOTIFY changed)
    Q_PROPERTY(unsigned total READ total NOTIFY changed)
    Q_PROPERTY(QString reportPath READ reportPath NOTIFY changed)
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(int exitCode READ exitCode NOTIFY changed)
    Q_PROPERTY(QString qualityStatus READ qualityStatus NOTIFY changed)
    Q_PROPERTY(QString referenceInfo READ referenceInfo NOTIFY changed)

  public:

    explicit ResearchController(QObject *parent = nullptr);
    ~ResearchController() override;
    void setConnection(std::optional<sv::client::Options>);
    bool busy() const;
    bool available() const;
    bool batchMode() const;
    QVariantList batchRows() const;
    QString status() const;
    unsigned completed() const;
    unsigned total() const;
    QString reportPath() const;
    QVariantList rows() const;
    int exitCode() const;
    QString qualityStatus() const;
    QString referenceInfo() const;

    Q_INVOKABLE QString suggestOutput();
    Q_INVOKABLE QVariantMap draftSummary(const QVariantMap &values,
                                         const QUrl &reference = {}) const;
    Q_INVOKABLE QVariantMap draftCapabilities(const QVariantMap &catalog) const;
    Q_INVOKABLE QString draftCapabilityError(const QVariantMap &values, const QVariantMap &catalog,
                                             const QUrl &reference = {}) const;
    Q_INVOKABLE bool startCatalogDraft(const QVariantMap &values, const QString &newDirectory,
                                       const QUrl &reference, const QVariantMap &catalog);
    Q_INVOKABLE bool startDraft(const QVariantMap &values, const QString &newDirectory);
    Q_INVOKABLE bool startDraftWithReference(const QVariantMap &values, const QString &newDirectory,
                                             const QUrl &reference);
    Q_INVOKABLE bool startScenario(const QUrl &file, const QString &newDirectory);
    Q_INVOKABLE bool startBatch(const QUrl &file, const QString &newDirectory);
    Q_INVOKABLE bool startBatchDraft(const QVariantList &members, const QString &newDirectory);
    Q_INVOKABLE QVariantMap importBatch(const QUrl &file);
    Q_INVOKABLE QVariantMap saveBatch(const QVariantList &members, const QUrl &file);
    Q_INVOKABLE void cancel();

  signals:
    void changed();
    void finished();

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool launch(std::optional<boost::json::object>, const QString &scenarioFile,
                const QString &directory, bool batch = false);
    bool reject(const QString &message);
};
