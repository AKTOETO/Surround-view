#pragma once
#include <QVariantMap>
#include <boost/json.hpp>

// Validated form values -> ordinary scenario document; no server/IO side effects.
boost::json::object researchDraft(const QVariantMap &, const QString &reference = {});
