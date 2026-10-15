#include "telemetry_model.hpp"
#include <algorithm>
#include <cmath>

TelemetryModel::TelemetryModel()
{
    timer_.setInterval(500);
    connect(&timer_, &QTimer::timeout, this, &TelemetryModel::poll);
}

void TelemetryModel::resetHistory()
{
    points_.clear();
    origin_.reset();
    last_timestamp_.reset();
    cursor_ = lost_ = 0;
    summary_.clear();
}

void TelemetryModel::attach(const std::shared_ptr<sv::client::Client> &client)
{
    timer_.stop();
    client_ = client;
    connected_ = supported_ = false;
    catalog_id_ = read_id_ = 0;
    catalog_.reset();
    resetHistory();
    status_ = "Нет соединения; история очищена";
    emit changed();
}

void TelemetryModel::connected()
{
    connected_ = true;
    if (monitoring_)
    {
        timer_.start();
        poll();
    }
}

void TelemetryModel::setMonitoring(bool enabled)
{
    monitoring_ = enabled;
    if (enabled && connected_)
    {
        timer_.start();
        poll();
    }
    else
    {
        timer_.stop();
        status_ = enabled ? "Ожидание подключения" : "Чтение остановлено (сбор сервера не изменён)";
    }
    emit changed();
}

void TelemetryModel::clear()
{
    // Preserve cursor/time baseline: local clear must not replay the server's history.
    points_.clear();
    updateSummary();
    emit changed();
}

void TelemetryModel::poll()
{
    if (!monitoring_ || !connected_ || catalog_id_ || read_id_)
    {
        return;
    }
    if (!supported_)
    {
        status_ = "Сервер не объявил telemetry_v1";
        emit changed();
        return;
    }
    try
    {
        if (auto client = client_.lock())
        {
            catalog_id_ = client->telemetry_catalog();
        }
    }
    catch (const std::exception &error)
    {
        timer_.stop();
        status_ = QString::fromUtf8(error.what());
        emit changed();
    }
}

void TelemetryModel::acceptCatalog(const sv::client::TelemetryCatalog &catalog)
{
    if (!catalog_ || catalog_->server_instance_id != catalog.server_instance_id ||
        catalog_->epoch != catalog.epoch)
    {
        resetHistory();
    }
    catalog_ = catalog;
    status_ = catalog.settings.enabled
                  ? QString("Сбор: %1 мс · server history: %2 · epoch %3")
                        .arg(catalog.settings.period_ms)
                        .arg(catalog.settings.history_capacity)
                        .arg(QString::number(catalog.epoch))
                  : "Сбор на сервере выключен; включите в Конфигурация → Телеметрия";
}

void TelemetryModel::acceptBatch(const sv::client::TelemetryBatch &batch)
{
    if (!catalog_ || batch.server_instance_id != catalog_->server_instance_id ||
        batch.epoch != catalog_->epoch || batch.after_cursor != cursor_)
    {
        throw std::runtime_error("telemetry_response_identity_mismatch");
    }
    if (UINT64_MAX - lost_ < batch.lost_samples)
    {
        throw std::overflow_error("telemetry_loss_counter_overflow");
    }
    lost_ += batch.lost_samples;
    bool gap = batch.lost_samples != 0;
    for (const auto &sample : batch.samples)
    {
        if (last_timestamp_ && sample.timestamp_ns < *last_timestamp_)
        {
            throw std::runtime_error("telemetry_time_went_backwards");
        }
        if (!origin_)
        {
            origin_ = sample.timestamp_ns;
        }
        const bool delayed =
            last_timestamp_ && sample.timestamp_ns - *last_timestamp_ >
                                   uint64_t(catalog_->settings.period_ms) * 2500000ULL;
        QVariantMap point{
            {"seconds", double(sample.timestamp_ns - *origin_) / 1e9},
            {"gap", gap || delayed},
            {"revision", QString::number(sample.config_revision)},
            {"cpu",
             sample.process.cpu_percent ? QVariant(*sample.process.cpu_percent) : QVariant{}},
            {"rss", sample.process.rss_bytes
                        ? QVariant(double(*sample.process.rss_bytes) / 1048576.0)
                        : QVariant{}},
            {"render", sample.frame && sample.frame->fresh
                           ? QVariant(sample.frame->spans_ms.at("render_wall").to_number<double>())
                           : QVariant{}}};
        points_.append(point);
        while (points_.size() > 120)
        {
            points_.removeFirst();
        }
        gap = false;
        last_timestamp_ = sample.timestamp_ns;
    }
    cursor_ = batch.next_cursor;
    updateSummary();
}

void TelemetryModel::updateSummary()
{
    std::vector<double> renders;
    for (const auto &point : points_)
    {
        const auto render = point.toMap().value("render");
        if (render.isValid())
        {
            renders.push_back(render.toDouble());
        }
    }
    std::sort(renders.begin(), renders.end());
    auto percentile = [&](double q)
    {
        return renders.empty()
                   ? QString("—")
                   : QString::number(renders[size_t(std::ceil(q * renders.size())) - 1], 'f', 2);
    };
    summary_ = QString("Samples: %1/120 · потери server ring: %2\nRender wall p50/p95/p99: %3 / %4 "
                       "/ %5 мс (%6 свежих кадров)")
                   .arg(points_.size())
                   .arg(QString::number(lost_))
                   .arg(percentile(.50), percentile(.95), percentile(.99))
                   .arg(renders.size());
    if (!points_.empty())
    {
        summary_ +=
            "\nПоследняя config revision: " + points_.last().toMap().value("revision").toString();
    }
}

void TelemetryModel::consume(const sv::Message &message)
{
    if (message.type == 2)
    {
        if (const auto *capabilities = message.header.if_contains("capabilities"))
        {
            for (const auto &capability : capabilities->as_array())
            {
                supported_ |= capability.as_string() == "telemetry_v1";
            }
        }
        return;
    }
    if (message.type != 21)
    {
        return;
    }
    const auto id = sv::parse_decimal_u64(std::string(message.header.at("command_id").as_string()));
    if ((!catalog_id_ || id != catalog_id_) && (!read_id_ || id != read_id_))
    {
        return;
    }
    const bool catalog_reply = id == catalog_id_;
    if (!message.header.at("accepted").as_bool())
    {
        catalog_id_ = read_id_ = 0;
        status_ = "Телеметрия отклонена: " +
                  QString::fromStdString(std::string(message.header.at("reason").as_string()));
        emit changed();
        return; // Next bounded tick refreshes catalog, including changed epoch.
    }
    if (catalog_reply)
    {
        acceptCatalog(sv::client::parse_telemetry_catalog(message));
        catalog_id_ = 0;
        if (monitoring_ && catalog_->settings.enabled)
        {
            if (auto client = client_.lock())
            {
                read_id_ = client->read_telemetry(catalog_->server_instance_id, catalog_->epoch,
                                                  cursor_, catalog_->max_read_samples);
            }
        }
    }
    else
    {
        acceptBatch(sv::client::parse_telemetry_batch(message));
        read_id_ = 0;
        // Catch up one bounded batch per tick; never flood the control stream.
    }
    if (!monitoring_)
    {
        status_ = "Чтение остановлено (сбор сервера не изменён)";
    }
    emit changed();
}

void TelemetryModel::commandFailed(const boost::json::object &header, const std::string &reason)
{
    const auto *field = header.if_contains("command_id");
    if (!field)
    {
        return;
    }
    const auto id = sv::parse_decimal_u64(std::string(field->as_string()));
    if ((catalog_id_ && catalog_id_ == id) || (read_id_ && read_id_ == id))
    {
        catalog_id_ = read_id_ = 0;
        timer_.stop();
        status_ = "Чтение не подтверждено: " + QString::fromStdString(reason) +
                  "; включите мониторинг заново";
        emit changed();
    }
}
