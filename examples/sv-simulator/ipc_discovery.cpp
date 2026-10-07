#include "ipc_discovery.hpp"
#include <QDir>
#include <QFileInfo>

QStringList discover_local_ipc_candidates(const QStringList &candidate_directories)
{
    QStringList result;
    for (const auto &candidate : candidate_directories)
    {
        const auto normalized = QDir::cleanPath(candidate);
        if (!QDir::isAbsolutePath(normalized) || result.contains(normalized))
        {
            continue;
        }
        const QDir directory(normalized);
        if (QFileInfo::exists(directory.filePath("control.sock")) &&
            QFileInfo::exists(directory.filePath("data.sock")))
        {
            result.push_back(normalized);
        }
    }
    return result;
}
