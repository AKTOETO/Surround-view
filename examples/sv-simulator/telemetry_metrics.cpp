#include "telemetry_model.hpp"
#include <algorithm>
#include <cmath>

namespace
{
QVariantMap metric(const char *key, const char *label, const char *unit, const char *note)
{
    return {{"key", QString::fromLatin1(key)},
            {"label", QString::fromUtf8(label)},
            {"unit", QString::fromLatin1(unit)},
            {"note", QString::fromUtf8(note)}};
}
} // namespace

QVariantList TelemetryModel::pipelineMetrics() const
{
    return {metric("render_wall", "Render wall", "ms",
                   "Весь вызов renderer; включает вложенные этапы. Интервалы нельзя суммировать."),
            metric("source_poll", "Source poll", "ms",
                   "Получение готовых событий в main loop, не длительность capture/decode worker."),
            metric("pre_render_prepare", "Между poll и render", "ms",
                   "Обработка событий/команд и подготовка; не отдельный GPU projection timer."),
            metric("fusion_cpu", "Fusion CPU wall", "ms",
                   "CPU fusion, включая её вложенные операции. Ноль не доказывает отсутствия всей "
                   "CPU нагрузки."),
            metric("layer_readback_cpu", "Layer readback CPU wall", "ms",
                   "Подготовка/чтение hybrid layers. CPU wall может включать ожидание GPU."),
            metric("upload_cpu", "Upload CPU wall", "ms",
                   "CPU wall вызовов upload изменившихся входов, не время исполнения GPU."),
            metric("readback_copy_cpu", "Readback/copy CPU wall", "ms",
                   "CPU wall readback и копирования результата; может включать ожидание GPU."),
            metric("publish_enqueue", "Publish enqueue", "ms",
                   "Постановка готового результата на отправку; не send/release round-trip."),
            metric("gpu_draw", "GPU draw query", "ms",
                   "Nullable GPU timer; не GPU utilisation и не полный hybrid GPU путь."),
            metric("input_age_at_render", "Возраст входа при render", "ms",
                   "Старейший selected input receive → завершение render. Это возраст, не чистая "
                   "вычислительная latency.")};
}

QVariantList TelemetryModel::sourceMetrics() const
{
    return {metric("queued_batches", "Очередь FrameSource", "count",
                   "Backend-specific gauge: replay/socket batches; camera backend считает pending "
                   "events (включая status/control)."),
            metric("data_busy", "Вывод занят", "0/1",
                   "Не завершена доставка/release. Это sampled flag, не измерение send latency."),
            metric("data_ready", "Data channel готов", "0/1",
                   "Текущее состояние подключения output channel, независимо от нового render."),
            metric("paused", "Источник на паузе", "0/1",
                   "Состояние pause при sampling. Повторные frame spans не считаются новыми render "
                   "events."),
            metric("frame_record_age", "Возраст render record", "ms",
                   "Sample timestamp − timestamp последнего завершённого render. Рост при "
                   "pause/busy не означает, что render стал медленнее."),
            metric("collection_wall", "Collection CPU wall", "ms",
                   "Process read и подготовка sample; без ring write, JSON, network и старых frame "
                   "timers. Не полный overhead collection.")};
}

QVariantMap TelemetryModel::makePoint(const sv::client::TelemetrySample &sample, uint64_t origin,
                                      bool gap)
{
    const auto &frame = sample.frame;
    const bool fresh = frame && frame->fresh;
    QVariantMap point{
        {"seconds", double(sample.timestamp_ns - origin) / 1e9},
        {"gap", gap},
        {"revision", QString::number(sample.config_revision)},
        {"cpu", sample.process.cpu_percent ? QVariant(*sample.process.cpu_percent) : QVariant{}},
        {"rss", sample.process.rss_bytes ? QVariant(double(*sample.process.rss_bytes) / 1048576.0)
                                         : QVariant{}},
        // Large gauges stay exact in sourceDetails/archive; suppress lossy graph conversion.
        {"queued_batches", sample.source.queued_batches <= (uint64_t(1) << 53)
                               ? QVariant(double(sample.source.queued_batches))
                               : QVariant{}},
        {"paused", sample.paused ? 1.0 : 0.0},
        {"data_ready", sample.data_ready ? 1.0 : 0.0},
        {"data_busy", sample.data_busy ? 1.0 : 0.0},
        {"collection_wall", sample.collection_wall_ms},
        {"input_age_at_render", fresh ? QVariant(frame->input_age_at_render_ms) : QVariant{}},
        {"frame_record_age",
         frame ? QVariant(double(sample.timestamp_ns - frame->timestamp_ns) / 1e6) : QVariant{}}};
    for (const auto *key :
         {"render_wall", "source_poll", "pre_render_prepare", "fusion_cpu", "layer_readback_cpu",
          "upload_cpu", "readback_copy_cpu", "publish_enqueue", "gpu_draw"})
    {
        const auto *value = fresh ? frame->spans_ms.if_contains(key) : nullptr;
        point[key] = value && !value->is_null() ? QVariant(value->to_number<double>()) : QVariant{};
    }
    point["render"] = point.value("render_wall"); // Preserve existing chart/summary contract.
    return point;
}

QString TelemetryModel::metricSummary(const QString &key) const
{
    std::vector<double> values;
    for (const auto &point : points_)
    {
        const auto value = point.toMap().value(key);
        if (value.userType() == QMetaType::Double)
        {
            values.push_back(value.toDouble());
        }
    }
    if (values.empty())
    {
        return "Нет пригодных значений в окне (null/repeated frame)";
    }
    std::sort(values.begin(), values.end());
    auto percentile = [&](double q)
    { return QString::number(values[size_t(std::ceil(q * values.size())) - 1], 'g', 5); };
    return QString(
               "n=%1 · p50 / p95 / p99: %2 / %3 / %4\nТолько доступные значения в retained window")
        .arg(values.size())
        .arg(percentile(.50), percentile(.95), percentile(.99));
}

QString TelemetryModel::sourceDetails() const
{
    if (samples_.empty())
    {
        return "Нет source samples";
    }
    const auto &sample = samples_.back();
    const auto &source = sample.source;
    return QString("Последний sample: queue=%1 · paused=%2 · ready=%3 · busy=%4\n"
                   "Counters: received=%5 · decoded=%6\ndropped=%7 · rejected=%8\n"
                   "Counters/queue имеют backend-specific смысл; ноль может означать отсутствие "
                   "инструментации.")
        .arg(QString::number(source.queued_batches))
        .arg(sample.paused ? 1 : 0)
        .arg(sample.data_ready ? 1 : 0)
        .arg(sample.data_busy ? 1 : 0)
        .arg(QString::number(source.received), QString::number(source.decoded),
             QString::number(source.dropped), QString::number(source.rejected));
}
