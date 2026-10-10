#include "ipc_discovery.hpp"
#include <QDir>
#include <QFileInfo>

QStringList discover_local_ipc_candidates(const QStringList &candidate_directories)
{
    QStringList result;
    for (const auto &candidate : candidate_directories)
    {
        const auto normalized = QDir::cleanPath(candidate);
        // Match sv-client-lib's Unix directory limit (UTF-8 bytes), otherwise
        // discovery can stop at a candidate the transport will never accept.
        if (!QDir::isAbsolutePath(normalized) || normalized.toStdString().size() > 80 ||
            result.contains(normalized))
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
