#pragma once
#include <QUrl>
#include <QVariantList>
#include <boost/json.hpp>

QString researchLocalPath(const QUrl &);
// Freeze typed composition into an ordinary batch document; no disk/network side effects.
boost::json::object researchBatchDraft(const QVariantList &);
