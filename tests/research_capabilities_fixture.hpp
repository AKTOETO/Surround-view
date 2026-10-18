#pragma once
#include "sv/fusion_runtime.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

namespace capability_fixture
{
inline QVariantMap catalog()
{
    return QJsonDocument::fromJson(
               QByteArray::fromStdString(boost::json::serialize(sv::fusion_catalog())))
        .object()
        .toVariantMap();
}

inline QVariantMap form()
{
    return {{"modes", QStringList{"multi_band", "graph_cut_multi_band"}},
            {"frames", 2},
            {"warmup", 1},
            {"repeats", 2},
            {"seed", 17},
            {"boundary", "both"},
            {"pyramid_levels", 4},
            {"seam_solver", "binary_pairs"}};
}
} // namespace capability_fixture
