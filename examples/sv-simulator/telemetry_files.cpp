#include "telemetry_model.hpp"
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace
{
QString path(const QUrl &url)
{
    if (!url.isLocalFile() || url.toLocalFile().isEmpty())
    {
        throw std::invalid_argument("telemetry_report_requires_local_file");
    }
    return url.toLocalFile();
}
} // namespace

bool TelemetryModel::saveReport(const QUrl &url, const QString &label)
{
    try
    {
        if (!catalog_)
        {
            throw std::runtime_error("telemetry_catalog_not_loaded");
        }
        const auto filename = path(url);
        sv::client::TelemetryArchive archive{
            *catalog_, origin_.value_or(0), cursor_,
            lost_,     label.toStdString(), {samples_.begin(), samples_.end()}};
        archive.first_gap = !points_.empty() && points_.first().toMap().value("gap").toBool();
        const auto data = sv::client::serialize_telemetry_archive(archive);
        QSaveFile file(filename);
        // Default atomic replacement; never fall back to truncating the destination.
        if (!file.open(QIODevice::WriteOnly) ||
            file.write(data.data(), qint64(data.size())) != qint64(data.size()) || !file.commit())
        {
            throw std::runtime_error("telemetry_report_write_failed");
        }
        status_ = "Окно сохранено: " + QFileInfo(filename).fileName();
        emit changed();
        return true;
    }
    catch (const std::exception &error)
    {
        status_ = QString::fromUtf8(error.what());
        emit changed();
        return false;
    }
}

bool TelemetryModel::loadReport(const QUrl &url)
{
    try
    {
        if (!canImport())
        {
            throw std::runtime_error("Отключитесь от сервера и остановите reader перед импортом");
        }
        const auto filename = path(url);
        QFile file(filename);
        if (!file.open(QIODevice::ReadOnly) || file.size() < 0 ||
            uint64_t(file.size()) > sv::client::telemetry_archive_max_bytes)
        {
            throw std::runtime_error("telemetry_report_read_failed_or_too_large");
        }
        const auto data = file.read(sv::client::telemetry_archive_max_bytes + 1);
        if (file.error() != QFileDevice::NoError)
        {
            throw std::runtime_error("telemetry_report_read_failed");
        }
        const auto archive = sv::client::parse_telemetry_archive(
            std::string_view(data.constData(), size_t(data.size())));
        // Build a complete replacement first: bad reports preserve the current graph.
        TelemetryModel restored;
        restored.acceptCatalog(archive.catalog);
        restored.origin_ = archive.origin_ns;
        for (const auto &sample : archive.samples)
        {
            const auto after = restored.samples_.empty() ? sample.cursor - 1 : restored.cursor_;
            restored.cursor_ = after;
            sv::client::TelemetryBatch batch;
            batch.server_instance_id = archive.catalog.server_instance_id;
            batch.epoch = archive.catalog.epoch;
            batch.after_cursor = after;
            batch.next_cursor = sample.cursor;
            batch.lost_samples = sample.cursor - after - 1;
            batch.samples.push_back(sample);
            restored.acceptBatch(batch);
        }
        if (!restored.points_.empty())
        {
            auto first = restored.points_.first().toMap();
            first["gap"] = archive.first_gap;
            restored.points_.first() = first;
        }
        restored.cursor_ = archive.latest_cursor;
        restored.lost_ = archive.lost_samples_total;
        restored.updateSummary();
        const auto status = "OFFLINE: " + QFileInfo(filename).fileName() + " · " +
                            QString::fromStdString(archive.label);
        catalog_ = std::move(restored.catalog_);
        points_ = std::move(restored.points_);
        samples_ = std::move(restored.samples_);
        origin_ = restored.origin_;
        last_timestamp_ = restored.last_timestamp_;
        cursor_ = restored.cursor_;
        lost_ = restored.lost_;
        summary_ = std::move(restored.summary_);
        offline_ = true;
        status_ = status;
        emit changed();
        return true;
    }
    catch (const std::exception &error)
    {
        status_ = QString::fromUtf8(error.what());
        emit changed();
        return false;
    }
}
