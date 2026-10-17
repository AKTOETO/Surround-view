#include "research_batch_draft.hpp"
#include <QFileInfo>
#include <QVariantMap>
#include <stdexcept>

QString researchLocalPath(const QUrl &url)
{
    if (!url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment() ||
        url.toLocalFile().isEmpty() || url.toLocalFile().contains(QChar(0)))
    {
        throw std::invalid_argument("Выберите локальный файл без query/fragment в URL");
    }
    return QFileInfo(url.toLocalFile()).absoluteFilePath();
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
