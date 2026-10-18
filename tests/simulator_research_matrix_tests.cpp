#include "research_capabilities_fixture.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "research_results.hpp"
#include "scenario.hpp"
#include <QSet>
#include <QTemporaryDir>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

namespace
{
QVariantMap matrixForm()
{
    auto values = capability_fixture::form();
    values["pyramid_levels_range"] = QVariantMap{{"from", 2}, {"to", 5}, {"step", 2}};
    values["seam_solvers"] = QStringList{"binary_pairs", "alpha_expansion"};
    return values;
}
} // namespace

TEST(ResearchGuiMatrix, ExpandsOnlyApplicableAxesWithStableCommonOrder)
{
    auto form = matrixForm();
    form["modes"] =
        QStringList{"edge_feather", "graph_cut_seam", "multi_band", "graph_cut_multi_band"};
    const auto result = researchDraft(form);
    const auto &rows = result.at("variants").as_array();
    ASSERT_EQ(rows.size(), 15U); // 1 + 2 + (2*2) + (2*2*2)
    EXPECT_FALSE(rows[0].as_object().contains("pyramid_levels"));
    EXPECT_FALSE(rows[0].as_object().contains("seam_solver"));
    EXPECT_EQ(rows[1].as_object().at("seam_solver"), "binary_pairs");
    EXPECT_EQ(rows[2].as_object().at("seam_solver"), "alpha_expansion");
    EXPECT_FALSE(rows[1].as_object().contains("pyramid_boundary"));
    EXPECT_EQ(rows[3].as_object().at("pyramid_boundary"), "zero");
    EXPECT_EQ(rows[3].as_object().at("pyramid_levels").to_number<unsigned>(), 2U);
    EXPECT_EQ(rows[4].as_object().at("pyramid_levels").to_number<unsigned>(), 4U);
    EXPECT_EQ(rows[5].as_object().at("pyramid_boundary"), "normalized");
    EXPECT_EQ(rows[7].as_object().at("mode"), "graph_cut_multi_band");
    EXPECT_EQ(rows[7].as_object().at("seam_solver"), "binary_pairs");
    EXPECT_EQ(rows[8].as_object().at("seam_solver"), "alpha_expansion");
    EXPECT_EQ(result, researchDraft(form));
    ResearchController controller;
    const auto summary = controller.draftSummary(form);
    EXPECT_TRUE(summary.value("valid").toBool());
    EXPECT_EQ(summary.value("variants").toUInt(), 15U);
    EXPECT_EQ(summary.value("trials").toUInt(), 90U);
}

TEST(ResearchGuiMatrix, IrrelevantAxesNeverMultiplyBasicOrNonGraphMethods)
{
    auto form = matrixForm();
    form["modes"] = QStringList{"edge_feather", "hard_best_angle"};
    EXPECT_EQ(researchDraft(form).at("variants").as_array().size(), 2U);
    form["modes"] = QStringList{"multi_band"};
    form["boundary"] = "zero";
    EXPECT_EQ(researchDraft(form).at("variants").as_array().size(), 2U);
    form["modes"] = QStringList{"graph_cut_seam"};
    EXPECT_EQ(researchDraft(form).at("variants").as_array().size(), 2U);
}

TEST(ResearchGuiMatrix, RejectsMalformedRangesAndDuplicateOrUnknownSolvers)
{
    for (const auto &range :
         std::vector<QVariantMap>{{{"from", 0}, {"to", 3}, {"step", 1}},
                                  {{"from", 4}, {"to", 3}, {"step", 1}},
                                  {{"from", 1}, {"to", 9}, {"step", 1}},
                                  {{"from", 1}, {"to", 3}, {"step", 0}},
                                  {{"from", true}, {"to", 3}, {"step", 1}},
                                  {{"from", 1.5}, {"to", 3}, {"step", 1}},
                                  {{"from", 1}, {"to", 3}},
                                  {{"from", 1}, {"to", 3}, {"step", 1}, {"extra", 1}}})
    {
        auto form = matrixForm();
        form["pyramid_levels_range"] = range;
        EXPECT_ANY_THROW(researchDraft(form));
    }
    for (const auto &axis :
         std::vector<QStringList>{{}, {"binary_pairs", "binary_pairs"}, {"unknown"}})
    {
        auto form = matrixForm();
        form["seam_solvers"] = axis;
        EXPECT_ANY_THROW(researchDraft(form));
    }
}

TEST(ResearchGuiMatrix, EnforcesAggregateVariantAndTrialLimitsBeforeWorkerOrOutput)
{
    auto form = matrixForm();
    form["modes"] = QStringList{"graph_cut_multi_band"};
    form["pyramid_levels_range"] = QVariantMap{{"from", 1}, {"to", 8}, {"step", 1}};
    EXPECT_EQ(researchDraft(form).at("variants").as_array().size(), 32U);
    form["modes"] = QStringList{"graph_cut_multi_band", "edge_feather"};
    EXPECT_ANY_THROW(researchDraft(form));
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/result";
    EXPECT_FALSE(controller.startCatalogDraft(form, output, {}, capability_fixture::catalog()));
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    const auto summary = controller.draftSummary(form);
    EXPECT_FALSE(summary.value("valid").toBool());
    EXPECT_FALSE(summary.contains("variants")); // no misleading zero count
    form["modes"] = QStringList{"graph_cut_multi_band"};
    form["frames"] = 256;
    form["repeats"] = 1000;
    EXPECT_FALSE(controller.draftSummary(form).value("valid").toBool());
}

TEST(ResearchGuiMatrix, CatalogGuardChecksEveryExpandedVariantAndKeepsCriteria)
{
    auto form = matrixForm();
    auto catalog = capability_fixture::catalog();
    catalog["pyramid_levels"] = QVariantMap{{"min", 3}, {"max", 4}};
    ResearchController controller;
    EXPECT_FALSE(controller.draftCapabilityError(form, catalog).isEmpty());
    form["pyramid_levels_range"] = QVariantMap{{"from", 3}, {"to", 4}, {"step", 1}};
    EXPECT_TRUE(controller.draftCapabilityError(form, catalog).isEmpty());
    catalog["seam_solver"] = QStringList{"binary_pairs"};
    EXPECT_FALSE(controller.draftCapabilityError(form, catalog).isEmpty());
    catalog = capability_fixture::catalog();
    form["quality_criteria"] = QVariantMap{{"max_mae_rgb8", .125}};
    const auto reference = QUrl::fromLocalFile("/absent reference # %.json");
    EXPECT_TRUE(controller.draftCapabilityError(form, catalog, reference).isEmpty());
    const auto document = researchDraft(form, reference.toLocalFile());
    EXPECT_DOUBLE_EQ(
        document.at("quality_criteria").as_object().at("max_mae_rgb8").to_number<double>(), .125);
    EXPECT_EQ(document.at("variants").as_array().size(), 12U);
}

TEST(ResearchGuiMatrix, ResultLabelsIdentifyTheSweptLevelsAndSolvers)
{
    auto form = matrixForm();
    form["modes"] = QStringList{"graph_cut_multi_band"};
    form["boundary"] = "zero";
    form["pyramid_levels_range"] = QVariantMap{{"from", 2}, {"to", 3}, {"step", 1}};
    const auto rows = researchResultRows(
        {{"scenario", researchDraft(form)}, {"summaries", boost::json::array{}}});
    ASSERT_EQ(rows.size(), 4);
    QSet<QString> labels;
    for (int i = 0; i < rows.size(); ++i)
    {
        const auto row = rows[i].toMap();
        EXPECT_EQ(row.value("variantIndex").toInt(), i);
        EXPECT_EQ(row.value("pyramidLevels").toString(), i < 2 ? "2" : "3");
        EXPECT_EQ(row.value("solver").toString(), i % 2 == 0 ? "binary_pairs" : "alpha_expansion");
        EXPECT_TRUE(row.value("profile").toString().contains(
            "levels=" + row.value("pyramidLevels").toString()));
        EXPECT_TRUE(
            row.value("profile").toString().contains("solver=" + row.value("solver").toString()));
        labels.insert(row.value("profile").toString());
    }
    EXPECT_EQ(labels.size(), 4);
}
