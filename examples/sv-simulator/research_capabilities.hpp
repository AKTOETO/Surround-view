#pragma once
#include <QVariantMap>
#include <boost/json.hpp>

QVariantMap researchCapabilities(const QVariantMap &fusionCatalog);
void validateResearchCapabilities(const boost::json::object &scenario,
                                  const QVariantMap &fusionCatalog,
                                  const QVariantMap &surfaceCatalog = {});
