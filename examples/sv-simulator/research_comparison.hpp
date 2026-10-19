#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

class ResearchComparison : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool series READ series NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantMap result READ result NOTIFY changed)
  public:

    explicit ResearchComparison(QObject *parent = nullptr);
    ~ResearchComparison() override;
    bool busy() const;
    bool series() const;
    QString status() const;
    QVariantMap result() const;
    Q_INVOKABLE bool compare(const QUrl &left, const QUrl &right);
    Q_INVOKABLE bool compareSeries(const QVariantList &files);
    Q_INVOKABLE void cancel();
    Q_INVOKABLE bool save(const QUrl &);
  signals:
    void changed();
    void finished();

  private:

    bool launch(const QVariantList &, bool series);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
