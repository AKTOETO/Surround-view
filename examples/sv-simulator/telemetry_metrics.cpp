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

QString automaticPropertiesText(const sv::CameraAutomaticProperties &automatic)
{
    return QString("Automatic readback (без проверки запроса): fps=%1 · FOURCC=%2 · buffers=%3")
        .arg(automatic.fps ? QString::number(*automatic.fps, 'g', 8) : "неизвестно")
        .arg(automatic.fourcc ? QString::fromStdString(*automatic.fourcc) : "неизвестно")
        .arg(automatic.buffer_count ? QString::number(*automatic.buffer_count) : "неизвестно");
}

const std::array<std::pair<const char *, const char *>, 6> cameraMetrics{
    {{"open_attempts", "Попытки открытия"},
     {"read_failures", "Ошибки чтения"},
     {"opencv_exceptions", "Исключения OpenCV"},
     {"reconnects", "Переподключения"},
     {"release_failures", "Ошибки закрытия"},
     {"worker_stopped", "Worker остановлен"}}};

std::array<uint64_t, 6> cameraValues(const sv::CameraDiagnostics &camera)
{
    return {camera.open_attempts, camera.read_failures,    camera.opencv_exceptions,
            camera.reconnects,    camera.release_failures, uint64_t(camera.worker_stopped)};
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
    QVariantList result{
        metric("queued_batches", "Очередь FrameSource", "count",
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
    for (int camera = 0; camera < 4; ++camera)
    {
        for (const auto &entry : {std::pair<const char *, const char *>{"read", "OpenCV read"},
                                  {"load", "Replay image load"},
                                  {"prepare", "RGB prepare/copy"}})
        {
            result.append(
                QVariantMap{{"key", QString("input_%1_%2").arg(camera).arg(entry.first)},
                            {"label", QString("Камера %1: %2").arg(camera).arg(entry.second)},
                            {"unit", "ms"},
                            {"note", "Успешная операция выбранного входа; source_sequence см. "
                                     "ниже. Fresh render не означает новый capture: при rerender "
                                     "возможны повторы. Нет failed-read/exposure/send latency."}});
        }
    }
    for (int camera = 0; camera < 4; ++camera)
    {
        for (const auto &entry : cameraMetrics)
        {
            result.append(QVariantMap{
                {"key", QString("camera_%1_%2").arg(camera).arg(entry.first)},
                {"label",
                 QString("Камера %1: %2").arg(camera).arg(QString::fromUtf8(entry.second))},
                {"unit", "count"},
                {"note", "Накопительный счётчик текущего camera source; worker stopped — флаг 0/1. "
                         "Не сбрасывается при смене telemetry epoch. Null: backend не поддерживает "
                         "диагностику. Это не частота ошибок и не latency."}});
        }
    }
    return result;
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
    for (int camera = 0; camera < 4; ++camera)
    {
        for (const auto *name : {"read", "load", "prepare"})
        {
            point[QString("input_%1_%2").arg(camera).arg(name)] = QVariant{};
        }
    }
    if (fresh)
    {
        for (const auto &timing : sample.source_work)
        {
            auto put = [&](const char *name, const std::optional<double> &value)
            {
                if (value)
                {
                    point[QString("input_%1_%2").arg(timing.camera_id).arg(name)] = *value;
                }
            };
            put("read", timing.camera_read_wall_ms);
            put("load", timing.image_load_wall_ms);
            put("prepare", timing.image_prepare_wall_ms);
        }
    }
    for (size_t camera = 0; camera < 4; ++camera)
    {
        const auto values = sample.source.camera_diagnostics
                                ? cameraValues((*sample.source.camera_diagnostics)[camera])
                                : std::array<uint64_t, 6>{};
        for (size_t i = 0; i < cameraMetrics.size(); ++i)
        {
            point[QString("camera_%1_%2").arg(camera).arg(cameraMetrics[i].first)] =
                sample.source.camera_diagnostics && values[i] <= (uint64_t(1) << 53)
                    ? QVariant(double(values[i]))
                    : QVariant{};
        }
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
    auto details =
        QString("Последний sample: queue=%1 · paused=%2 · ready=%3 · busy=%4\n"
                "Counters: received=%5 · decoded=%6\ndropped=%7 · rejected=%8\n"
                "Counters/queue имеют backend-specific смысл; ноль может означать отсутствие "
                "инструментации.")
            .arg(QString::number(source.queued_batches))
            .arg(sample.paused ? 1 : 0)
            .arg(sample.data_ready ? 1 : 0)
            .arg(sample.data_busy ? 1 : 0)
            .arg(QString::number(source.received), QString::number(source.decoded),
                 QString::number(source.dropped), QString::number(source.rejected));
    if (source.camera_diagnostics)
    {
        for (size_t camera = 0; camera < 4; ++camera)
        {
            const auto values = cameraValues((*source.camera_diagnostics)[camera]);
            details += QString("\nCamera %1 diagnostics:").arg(camera);
            for (size_t i = 0; i < cameraMetrics.size(); ++i)
            {
                details +=
                    QString(" %1=%2").arg(cameraMetrics[i].first).arg(QString::number(values[i]));
            }
            const auto &recovery = source.camera_diagnostics->at(camera).recovery;
            if (recovery)
            {
                details +=
                    QString("\n  Recovery: consecutive_failures=%1 · retry_delay_ms=%2 · "
                            "terminal_reason=%3")
                        .arg(QString::number(recovery->consecutive_failures))
                        .arg(recovery->retry_delay_ms ? QString::number(*recovery->retry_delay_ms)
                                                      : "нет ожидания")
                        .arg(recovery->terminal_reason
                                 ? QString::fromStdString(*recovery->terminal_reason)
                                 : "worker активен");
            }
            else
            {
                details += "\n  Recovery: недоступно";
            }
            const auto &mode = source.camera_diagnostics->at(camera).negotiated_mode;
            if (mode)
            {
                details +=
                    QString("\n  Checked mode: %1×%2 · open_attempt=%3 · fps=%4 · FOURCC=%5 · "
                            "buffers=%6")
                        .arg(mode->width)
                        .arg(mode->height)
                        .arg(QString::number(mode->open_attempt))
                        .arg(mode->fps ? QString::number(*mode->fps, 'g', 8) : "не проверен")
                        .arg(mode->fourcc ? QString::fromStdString(*mode->fourcc) : "не проверен")
                        .arg(mode->buffer_count ? QString::number(*mode->buffer_count)
                                                : "не проверен");
                if (mode->automatic_properties)
                {
                    details += "\n  " + automaticPropertiesText(*mode->automatic_properties);
                }
            }
            else
            {
                details += "\n  Checked mode: недоступен (закрыт/не поддерживается)";
            }
        }
    }
    else
    {
        details += "\nCamera diagnostics: недоступно";
    }
    if (sample.frame)
    {
        for (const auto &input : sample.frame->camera_inputs)
        {
            details += QString("\nSelected camera %1: source_sequence=%2 · capture open_attempt=%3")
                           .arg(input.camera_id)
                           .arg(QString::number(input.source_sequence))
                           .arg(QString::number(input.capture.open_attempt));
            if (const auto &mode = input.capture.negotiated_mode; mode)
            {
                details +=
                    QString(" · %1×%2 · checked fps=%3")
                        .arg(mode->width)
                        .arg(mode->height)
                        .arg(mode->fps ? QString::number(*mode->fps, 'g', 8) : "не проверен");
                if (mode->automatic_properties)
                {
                    details +=
                        "\n  Selected " + automaticPropertiesText(*mode->automatic_properties);
                }
            }
        }
    }
    for (const auto &timing : sample.source_work)
    {
        details += QString("\nCamera %1: %2 · source_sequence=%3 · epoch=%4 · cache=%5")
                       .arg(timing.camera_id)
                       .arg(QString::fromStdString(timing.kind))
                       .arg(QString::number(timing.source_sequence), QString::number(timing.epoch))
                       .arg(timing.cache_hit ? 1 : 0);
    }
    return details;
}
