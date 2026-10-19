#include "research_batch_draft.hpp"
#include "batch_plan.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "scenario.hpp"
#include "sv/json.hpp"
#include <QDir>
#include <QJsonDocument>
#include <QVariantMap>
#include <algorithm>
#include <stdexcept>

QString researchLocalPath(const QUrl &url)
{
    if (!url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment() ||
        url.toLocalFile().isEmpty() || url.toLocalFile().contains(QChar(0)))
    {
        throw std::invalid_argument("Выберите локальный файл без query/fragment в URL");
    }
    // Lexically removing '..' across a symlink changes the selected source file.
    const auto path = url.toLocalFile();
    return QDir::isAbsolutePath(path) ? path : QDir::currentPath() + '/' + path;
}

boost::json::object researchBatchDraft(const QVariantList &members)
{
    if (members.empty() || members.size() > 8)
    {
        throw std::invalid_argument("Серия должна содержать от 1 до 8 сценариев");
    }
    boost::json::array runs;
    for (const auto &value : members)
    {
        if (value.userType() != QMetaType::QVariantMap)
        {
            throw std::invalid_argument("Участник должен содержать ID и путь сценария");
        }
        const auto row = value.toMap();
        const auto id = row.value("id"), source = row.value("scenario");
        if (row.size() != 2 || id.userType() != QMetaType::QString ||
            (source.userType() != QMetaType::QString && source.userType() != QMetaType::QUrl &&
             source.userType() != QMetaType::QVariantMap))
        {
            throw std::invalid_argument(
                "Участник должен содержать строковый ID и URL либо снимок сценария");
        }
        boost::json::value scenario;
        if (source.userType() == QMetaType::QVariantMap)
        {
            const auto bytes = QJsonDocument::fromVariant(source).toJson(QJsonDocument::Compact);
            scenario = sv::parse_json(std::string_view(bytes.constData(), size_t(bytes.size())));
            (void)sv::research::parse_scenario(scenario);
        }
        else
        {
            scenario = researchLocalPath(source.toUrl()).toStdString();
        }
        runs.push_back(boost::json::object{{"id", id.toString().toStdString()},
                                           {"scenario", std::move(scenario)}});
    }
    // Shared run_batch preflight enforces IDs, duplicates, references and total budgets.
    const bool inlined = std::any_of(runs.begin(), runs.end(), [](const auto &row)
                                     { return row.as_object().at("scenario").is_object(); });
    return {{"schema_version", inlined ? 2 : 1}, {"runs", std::move(runs)}};
}

QVariantList researchImportBatch(const QUrl &file)
{
    const std::filesystem::path path(researchLocalPath(file).toStdString());
    const auto plan = sv::research::parse_batch_plan(sv::research::read_batch_plan(path));
    QVariantList members;
    for (const auto &value : plan.at("runs").as_array())
    {
        const auto &row = value.as_object();
        QVariant source;
        if (row.at("scenario").is_object())
        {
            auto scenario = row.at("scenario").as_object();
            if (auto *reference = scenario.if_contains("quality_reference"))
            {
                *reference = (path.parent_path() / std::string(reference->as_string())).string();
            }
            (void)sv::research::parse_scenario(scenario);
            source =
                QJsonDocument::fromJson(QByteArray::fromStdString(boost::json::serialize(scenario)))
                    .toVariant()
                    .toMap();
        }
        else
        {
            const auto file = path.parent_path() / std::string(row.at("scenario").as_string());
            source = QUrl::fromLocalFile(QString::fromStdString(file.string()));
        }
        members.push_back(
            QVariantMap{{"id", QString::fromStdString(std::string(row.at("id").as_string()))},
                        {"scenario", source}});
    }
    return members;
}

QVariantMap ResearchController::batchScenario(const QVariantMap &values,
                                              const QUrl &reference) const
{
    if (busy())
    {
        return {{"success", false}, {"message", "Дождитесь завершения текущего исследования"}};
    }
    try
    {
        const auto scenario =
            researchDraft(values, reference.isEmpty() ? QString{} : researchLocalPath(reference));
        const auto data = QByteArray::fromStdString(boost::json::serialize(scenario));
        return {{"success", true},
                {"scenario", QJsonDocument::fromJson(data).toVariant().toMap()},
                {"message", "Снимок формы добавлен в состав; эталон будет проверен при запуске"}};
    }
    catch (const std::exception &error)
    {
        return {{"success", false}, {"message", QString::fromUtf8(error.what())}};
    }
}
