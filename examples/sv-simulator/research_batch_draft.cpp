#include "research_batch_draft.hpp"
#include "batch_plan.hpp"
#include <QDir>
#include <QVariantMap>
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
            (source.userType() != QMetaType::QString && source.userType() != QMetaType::QUrl))
        {
            throw std::invalid_argument(
                "Участник должен содержать только строковый ID и URL сценария");
        }
        runs.push_back(
            boost::json::object{{"id", id.toString().toStdString()},
                                {"scenario", researchLocalPath(source.toUrl()).toStdString()}});
    }
    // Shared run_batch preflight enforces IDs, duplicates, references and total budgets.
    return {{"schema_version", 1}, {"runs", std::move(runs)}};
}

QVariantList researchImportBatch(const QUrl &file)
{
    const std::filesystem::path path(researchLocalPath(file).toStdString());
    const auto plan = sv::research::parse_batch_plan(sv::research::read_batch_plan(path));
    QVariantList members;
    for (const auto &value : plan.at("runs").as_array())
    {
        const auto &row = value.as_object();
        const auto source = path.parent_path() / std::string(row.at("scenario").as_string());
        members.push_back(QVariantMap{
            {"id", QString::fromStdString(std::string(row.at("id").as_string()))},
            {"scenario", QUrl::fromLocalFile(QString::fromStdString(source.string()))}});
    }
    return members;
}
