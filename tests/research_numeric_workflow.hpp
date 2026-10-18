#pragma once
#include "connection.hpp"
#include "research_controller.hpp"
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QSet>
#include <gtest/gtest.h>

namespace numeric_workflow
{
inline void prepare(QObject *panel)
{
    panel->setProperty("selectedModes", QStringList{"edge_feather", "angular_feather",
                                                    "graph_cut_seam", "multi_band"});
    panel->findChild<QObject *>("researchFrames")->setProperty("value", 1);
    panel->findChild<QObject *>("researchWarmup")->setProperty("value", 0);
    panel->findChild<QObject *>("researchRepeats")->setProperty("value", 1);
    panel->findChild<QObject *>("researchShowNumericAxes")->setProperty("checked", true);
    for (const auto *parameter : {"edge_width_px", "angle_power", "smoothness_weight"})
    {
        panel->findChild<QObject *>(QString("researchUse_") + parameter)
            ->setProperty("checked", true);
    }
    panel->findChild<QObject *>("researchAxis_edge_width_px")
        ->setProperty("values", QVariantList{12.5, 24.});
    panel->findChild<QObject *>("researchAxis_angle_power")
        ->setProperty("values", QVariantList{1., 2.});
    panel->findChild<QObject *>("researchAxis_smoothness_weight")
        ->setProperty("values", QVariantList{0., .125});
}

inline void check(const ResearchController &controller, const sv::client::Options &options)
{
    QFile file(controller.reportPath());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    const auto report = QJsonDocument::fromJson(file.readAll()).object();
    const auto variants = report.value("scenario").toObject().value("variants").toArray();
    ASSERT_EQ(variants.size(), 10);
    ASSERT_EQ(report.value("samples").toArray().size(), 10);
    QSet<int> seen;
    for (const auto &entry : report.value("samples").toArray())
    {
        const auto sample = entry.toObject();
        const int index = sample.value("variant").toInt(-1);
        ASSERT_GE(index, 0);
        ASSERT_LT(index, variants.size());
        seen.insert(index);
        const auto variant = variants[index].toObject();
        const auto metadata = sample.value("metadata").toObject();
        for (const auto *key : {"edge_width_px", "angle_power", "smoothness_weight"})
        {
            if (variant.contains(key))
            {
                EXPECT_DOUBLE_EQ(metadata.value("fusion").toObject().value(key).toDouble(-1),
                                 variant.value(key).toDouble());
            }
        }
        EXPECT_EQ(metadata.value("inputs"), report.value("baseline").toObject().value("inputs"));
    }
    EXPECT_EQ(seen.size(), 10);
    EXPECT_TRUE(report.value("restored").toBool());
    EXPECT_FALSE(report.contains("quality_reference"));
    sv::research::Connection connection(options);
    const auto state = connection.request("state");
    const auto fusion = QJsonDocument::fromJson(
                            QByteArray::fromStdString(boost::json::serialize(state.at("fusion"))))
                            .object();
    EXPECT_EQ(fusion, report.value("initial_state").toObject().value("fusion").toObject());
}
} // namespace numeric_workflow
