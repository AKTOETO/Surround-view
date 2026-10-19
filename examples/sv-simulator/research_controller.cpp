#include "research_controller.hpp"
#include "batch.hpp"
#include "batch_bundle.hpp"
#include "batch_plan.hpp"
#include "file_experiment.hpp"
#include "report_output.hpp"
#include "research_batch_draft.hpp"
#include "research_draft.hpp"
#include "research_results.hpp"
#include "scenario_bundle.hpp"
#include "trial_plan.hpp"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTimer>
#include <atomic>
#include <thread>
#include <tuple>

struct ResearchController::Impl
{
    struct Progress
    {
        std::atomic_bool cancel{false};
        std::atomic_uint completed{0}, total{0}, member{0};
    } progress;

    std::optional<sv::client::Options> connection;
    std::thread worker;
    QTimer timer;
    bool busy = false, batch = false;
    QString status = "Исследования ещё не запускались", report;
    QString quality_status = "not_available", reference_info = "Эталон не задан";
    QVariantList rows, batch_rows;
    QVariantMap bundle_info;
    int exit_code = -1;
    unsigned output_counter = 0;
};

namespace
{
auto key(const sv::client::Options &options)
{
    const auto &endpoint = options.endpoint;
    return std::make_tuple(endpoint.transport, endpoint.directory, endpoint.host,
                           endpoint.control_port, endpoint.data_port, options.timeout_ms);
}
} // namespace

ResearchController::ResearchController(QObject *parent)
    : QObject(parent), impl_(std::make_unique<Impl>())
{
    impl_->timer.setInterval(100);
    connect(&impl_->timer, &QTimer::timeout, this, [this] { emit changed(); });
}

ResearchController::~ResearchController()
{
    impl_->timer.stop();
    impl_->progress.cancel = true;
    if (impl_->worker.joinable())
    {
        impl_->worker.join();
    }
}

void ResearchController::setConnection(std::optional<sv::client::Options> options)
{
    if (bool(options) == bool(impl_->connection) &&
        (!options || key(*options) == key(*impl_->connection)))
    {
        return;
    }
    impl_->connection = std::move(options);
    emit changed();
}

bool ResearchController::busy() const
{
    return impl_->busy;
}

bool ResearchController::available() const
{
    return impl_->connection.has_value();
}

bool ResearchController::batchMode() const
{
    return impl_->batch;
}

QVariantList ResearchController::batchRows() const
{
    return impl_->batch_rows;
}

QString ResearchController::status() const
{
    if (impl_->busy && impl_->batch && !impl_->progress.cancel && impl_->progress.member > 0)
    {
        return QString("Сценарий №%1: trials текущего сценария %2 / %3; ожидается также restore.")
            .arg(impl_->progress.member.load())
            .arg(completed())
            .arg(total());
    }
    if (impl_->busy && !impl_->progress.cancel && impl_->progress.completed > 0)
    {
        return impl_->progress.completed == impl_->progress.total
                   ? "Все trials собраны; ожидается завершение восстановления настроек…"
                   : "Выполняется исследование…";
    }
    return impl_->status;
}

unsigned ResearchController::completed() const
{
    return impl_->progress.completed.load();
}

unsigned ResearchController::total() const
{
    return impl_->progress.total.load();
}

QString ResearchController::reportPath() const
{
    return impl_->report;
}

QVariantList ResearchController::rows() const
{
    return impl_->rows;
}

int ResearchController::exitCode() const
{
    return impl_->exit_code;
}

QString ResearchController::suggestOutput()
{
    return QDir::current().filePath(
        "artifacts/gui-research-" +
        QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss-zzz") + "-" +
        QString::number(++impl_->output_counter));
}

QString ResearchController::qualityStatus() const
{
    return impl_->quality_status;
}

QString ResearchController::referenceInfo() const
{
    return impl_->reference_info;
}

QVariantMap ResearchController::bundleInfo() const
{
    return impl_->bundle_info;
}

bool ResearchController::startBundle(const QUrl &directory, const QString &output, bool batch)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        return launch({}, researchLocalPath(directory), output, batch, BundleAction::Run);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::verifyBundle(const QUrl &directory, bool batch)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        return launch({}, researchLocalPath(directory), {}, batch, BundleAction::Verify);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::exportBundle(const QUrl &scenario, const QUrl &parent, const QString &name,
                                      bool batch)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        if (name.trimmed().isEmpty() || name.trimmed() != name || name == "." || name == ".." ||
            name.contains('/') || name.contains('\\') || name.contains(QChar(0)))
        {
            throw std::invalid_argument("Укажите имя новой папки без разделителей пути");
        }
        const auto destination = QDir(researchLocalPath(parent)).filePath(name);
        return launch({}, researchLocalPath(scenario), destination, batch, BundleAction::Export);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::reject(const QString &message)
{
    if (!impl_->busy)
    {
        impl_->status = message;
        impl_->quality_status = "not_available";
        impl_->reference_info = "Подготовка не выполнена";
        impl_->bundle_info.clear();
        impl_->rows.clear();
        impl_->batch_rows.clear();
        impl_->report.clear();
        impl_->exit_code = 3;
        impl_->progress.completed = 0;
        impl_->progress.total = 0;
        emit changed();
    }
    return false;
}

bool ResearchController::startDraft(const QVariantMap &values, const QString &directory)
{
    return startDraftWithReference(values, directory, {});
}

bool ResearchController::startDraftWithReference(const QVariantMap &values,
                                                 const QString &directory, const QUrl &reference)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        auto document =
            researchDraft(values, reference.isEmpty() ? QString{} : researchLocalPath(reference));
        return launch(std::move(document), {}, directory);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::startScenario(const QUrl &file, const QString &directory)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        return launch({}, researchLocalPath(file), directory);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::startBatch(const QUrl &file, const QString &directory)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        return launch({}, researchLocalPath(file), directory, true);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

bool ResearchController::startBatchDraft(const QVariantList &members, const QString &directory)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        return launch(researchBatchDraft(members), {}, directory, true);
    }
    catch (const std::exception &error)
    {
        return reject(QString::fromUtf8(error.what()));
    }
}

QVariantMap ResearchController::importBatch(const QUrl &file)
{
    if (impl_->busy)
    {
        return {{"success", false}, {"message", "Дождитесь завершения текущего исследования"}};
    }
    try
    {
        const auto members = researchImportBatch(file);
        return {
            {"success", true},
            {"members", members},
            {"message",
             QString(
                 "Импортировано сценариев: %1. Состав заменён; входы будут проверены при запуске.")
                 .arg(members.size())}};
    }
    catch (const std::exception &error)
    {
        return {{"success", false},
                {"message", "Импорт не выполнен: " + QString::fromUtf8(error.what())}};
    }
}

QVariantMap ResearchController::saveBatch(const QVariantList &members, const QUrl &file)
{
    if (impl_->busy)
    {
        return {{"success", false}, {"message", "Дождитесь завершения текущего исследования"}};
    }
    try
    {
        const auto document = researchBatchDraft(members);
        const auto path = researchLocalPath(file);
        const auto digest = sv::research::write_batch_plan(document, path.toStdString());
        return {{"success", true},
                {"path", path},
                {"sha256", QString::fromStdString(digest)},
                {"message",
                 "Черновик сохранён: " + path + ". Полный preflight будет выполнен при запуске."}};
    }
    catch (const std::exception &error)
    {
        return {{"success", false},
                {"message", "Черновик не сохранён: " + QString::fromUtf8(error.what())}};
    }
}

bool ResearchController::launch(std::optional<boost::json::object> document,
                                const QString &scenarioFile, const QString &newDirectory,
                                bool batch, BundleAction bundle)
{
    if (impl_->busy)
    {
        return false;
    }
    try
    {
        const bool offline = bundle == BundleAction::Verify || bundle == BundleAction::Export;
        if (!offline && !impl_->connection)
        {
            throw std::runtime_error("Сначала подключитесь к серверу");
        }
        const auto trials =
            document && !batch
                ? sv::research::make_trial_plan(sv::research::parse_scenario(*document)).size()
                : 0;
        if (bundle != BundleAction::Verify &&
            (newDirectory.trimmed().isEmpty() || newDirectory.contains(QChar(0))))
        {
            throw std::invalid_argument("Укажите новый каталог отчёта");
        }
        const auto directory = QFileInfo(newDirectory.trimmed()).absoluteFilePath().toStdString();
        const auto connection = impl_->connection.value_or(sv::client::Options{});
        impl_->progress.cancel = false;
        impl_->progress.member = 0;
        impl_->batch = batch;
        impl_->progress.completed = 0;
        impl_->progress.total = static_cast<unsigned>(trials);
        impl_->status = bundle == BundleAction::Verify   ? "Проверка и фиксация входов набора…"
                        : bundle == BundleAction::Export ? "Экспорт переносимого набора…"
                                                         : "Подготовка сценария и нового каталога…";
        impl_->bundle_info.clear();
        impl_->rows.clear();
        impl_->batch_rows.clear();
        impl_->report.clear();
        impl_->exit_code = -1;
        impl_->quality_status = "not_available";
        impl_->reference_info = "Подготовка входов…";
        impl_->busy = true;
        impl_->worker = std::thread(
            [this, document = std::move(document), scenarioFile, directory, connection, batch,
             bundle]
            {
                QString status, path, qualityStatus = "not_available",
                                      referenceInfo = "Не загружен";
                QVariantList rows, batchRows;
                QVariantMap bundleInfo;
                int code = 3;
                try
                {
                    const std::filesystem::path output(directory);
                    if (bundle == BundleAction::Verify || bundle == BundleAction::Export)
                    {
                        const auto receipt =
                            bundle == BundleAction::Export
                                ? (batch ? sv::research::export_batch_bundle(
                                               scenarioFile.toStdString(), output,
                                               &impl_->progress.cancel)
                                         : sv::research::export_scenario_bundle(
                                               scenarioFile.toStdString(), output,
                                               &impl_->progress.cancel))
                                : (batch ? sv::research::BatchBundle(scenarioFile.toStdString(),
                                                                     &impl_->progress.cancel)
                                               .summary()
                                         : sv::research::ScenarioBundle(scenarioFile.toStdString(),
                                                                        &impl_->progress.cancel)
                                               .summary());
                        bundleInfo = QJsonDocument::fromJson(
                                         QByteArray::fromStdString(boost::json::serialize(receipt)))
                                         .toVariant()
                                         .toMap();
                        bundleInfo["directory"] = bundle == BundleAction::Export
                                                      ? QString::fromStdString(output.string())
                                                      : scenarioFile;
                        code = 0;
                        referenceInfo = "Проверена целостность входов, качество не оценивалось";
                        status = bundle == BundleAction::Export
                                     ? "Набор экспортирован. Запись камер и конфигурация сервера "
                                       "не включены."
                                     : "Набор проверен без подключения. При запуске проверка "
                                       "повторится.";
                    }
                    else if (batch)
                    {
                        auto progress = [this](unsigned member, unsigned done, unsigned total)
                        {
                            impl_->progress.total = total;
                            impl_->progress.completed = done;
                            impl_->progress.member = member + 1;
                        };
                        std::unique_ptr<sv::research::BatchBundle> verifiedBatch;
                        if (bundle == BundleAction::Run)
                        {
                            verifiedBatch = std::make_unique<sv::research::BatchBundle>(
                                scenarioFile.toStdString(), &impl_->progress.cancel);
                            bundleInfo = QJsonDocument::fromJson(
                                             QByteArray::fromStdString(
                                                 boost::json::serialize(verifiedBatch->summary())))
                                             .toVariant()
                                             .toMap();
                            bundleInfo["directory"] = scenarioFile;
                        }
                        const auto summary =
                            verifiedBatch
                                ? verifiedBatch->execute(output, connection,
                                                         &impl_->progress.cancel, progress)
                            : document ? sv::research::run_batch(*document, output.parent_path(),
                                                                 output, connection,
                                                                 &impl_->progress.cancel, progress)
                                       : sv::research::run_batch(scenarioFile.toStdString(), output,
                                                                 connection,
                                                                 &impl_->progress.cancel, progress);
                        path = QString::fromStdString((output / "batch.json").string());
                        batchRows = researchBatchRows(summary);
                        code = sv::research::research_exit_code(summary);
                        qualityStatus =
                            QString::fromStdString(sv::research::quality_status(summary));
                        referenceInfo = "Эталоны и критерии заданы отдельно в каждом сценарии";
                        status =
                            QString("Batch: %1; завершено %2 из %3; restore: %4. Критерии: %5.")
                                .arg(QString::fromStdString(
                                    std::string(summary.at("status").as_string())))
                                .arg(summary.at("completed").to_number<unsigned>())
                                .arg(summary.at("runs").as_array().size())
                                .arg(summary.at("restored").as_bool() ? "подтверждено"
                                                                      : "НЕ подтверждено")
                                .arg(qualityStatus);
                    }
                    else
                    {
                        // Freeze all scenario/reference bytes before output creation or networking.
                        std::unique_ptr<sv::research::FileExperiment> experiment;
                        std::unique_ptr<sv::research::ScenarioBundle> verifiedBundle;
                        if (bundle == BundleAction::Run)
                        {
                            verifiedBundle = std::make_unique<sv::research::ScenarioBundle>(
                                scenarioFile.toStdString(), &impl_->progress.cancel);
                            const auto receipt = verifiedBundle->summary();
                            bundleInfo =
                                QJsonDocument::fromJson(
                                    QByteArray::fromStdString(boost::json::serialize(receipt)))
                                    .toVariant()
                                    .toMap();
                            bundleInfo["directory"] = scenarioFile;
                            impl_->progress.total = receipt.at("trials").to_number<unsigned>();
                        }
                        else if (document)
                        {
                            const auto text = boost::json::serialize(*document) + '\n';
                            experiment = std::make_unique<sv::research::FileExperiment>(
                                text, output.parent_path());
                        }
                        else
                        {
                            experiment = std::make_unique<sv::research::FileExperiment>(
                                scenarioFile.toStdString());
                        }
                        if (experiment)
                        {
                            impl_->progress.total =
                                static_cast<unsigned>(experiment->trial_count());
                        }
                        if (impl_->progress.cancel)
                        {
                            throw std::runtime_error("cancelled");
                        }
                        if (!std::filesystem::create_directory(output))
                        {
                            throw std::runtime_error(
                                "Каталог результата уже существует; выберите новое имя");
                        }
                        if (document)
                        {
                            sv::research::ReportOutput snapshot(output / "scenario.json");
                            snapshot.write(*document);
                        }
                        const auto progress = [this](unsigned done, unsigned total)
                        {
                            impl_->progress.total = total;
                            impl_->progress.completed = done;
                        };
                        if (experiment)
                        {
                            experiment->reserve(output / "report.json");
                        }
                        const auto report =
                            verifiedBundle
                                ? verifiedBundle->execute(output / "report.json", connection,
                                                          &impl_->progress.cancel, progress)
                                : experiment->execute(connection, &impl_->progress.cancel,
                                                      progress);
                        path = QString::fromStdString((output / "report.json").string());
                        rows = researchResultRows(report);
                        code = sv::research::research_exit_code(report);
                        const auto restore =
                            report.at("restored").as_bool() ? "подтверждено" : "не подтверждено";
                        const auto *error = report.if_contains("error");
                        const QString reason =
                            error && error->is_string()
                                ? QString::fromStdString(std::string(error->as_string()))
                                : "Прогон не завершён";
                        qualityStatus =
                            QString::fromStdString(sv::research::quality_status(report));
                        if (const auto *reference = report.if_contains("quality_reference"))
                        {
                            const auto &identity = reference->as_object();
                            referenceInfo =
                                QString("%1 · dataset: %2 · schema %3")
                                    .arg(QString::fromStdString(
                                             std::string(identity.at("kind").as_string())),
                                         QString::fromStdString(
                                             std::string(identity.at("dataset_id").as_string())))
                                    .arg(identity.at("schema_version").to_number<unsigned>());
                            if (const auto *origin = identity.if_contains("criteria_source"))
                            {
                                referenceInfo +=
                                    " · criteria: " +
                                    QString::fromStdString(std::string(origin->as_string()));
                            }
                        }
                        else
                        {
                            referenceInfo = "Эталон не задан";
                        }
                        if (code == 0)
                        {
                            status =
                                qualityStatus == "passed"
                                    ? "Прогон завершён; заданные критерии пройдены."
                                : report.contains("quality_reference")
                                    ? "Прогон завершён; метрики рассчитаны, критерии не заданы."
                                    : "Готово. Качество не оценивалось: внешний эталон не задан.";
                        }
                        else if (code == 5)
                        {
                            status =
                                "Прогон и восстановление завершены; заданные критерии качества НЕ "
                                "пройдены.";
                        }
                        else
                        {
                            status =
                                QString("%1: %2. Восстановление: %3. Подробности в отчёте.")
                                    .arg(reason == "cancelled" ? "Отменено" : "Ошибка исследования",
                                         reason, restore);
                        }
                    }
                }
                catch (const std::exception &error)
                {
                    status = "Ошибка подготовки/исполнения: " + QString::fromUtf8(error.what());
                }
                QMetaObject::invokeMethod(
                    this,
                    [this, status, path, rows, batchRows, code, qualityStatus, referenceInfo,
                     bundleInfo]
                    {
                        impl_->worker.join();
                        impl_->timer.stop();
                        impl_->busy = false;
                        impl_->status = status;
                        impl_->report = path;
                        impl_->rows = rows;
                        impl_->batch_rows = batchRows;
                        impl_->exit_code = code;
                        impl_->quality_status = qualityStatus;
                        impl_->reference_info = referenceInfo;
                        impl_->bundle_info = bundleInfo;
                        emit changed();
                        emit finished();
                    },
                    Qt::QueuedConnection);
            });
        impl_->timer.start();
        emit changed();
        return true;
    }
    catch (const std::exception &error)
    {
        impl_->busy = false;
        return reject(QString::fromUtf8(error.what()));
    }
}

void ResearchController::cancel()
{
    if (impl_->busy)
    {
        impl_->progress.cancel = true;
        impl_->status = "Остановка: ожидается завершение текущей операции и восстановление…";
        emit changed();
    }
}
