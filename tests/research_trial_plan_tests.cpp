#include "sv/json.hpp"
#include "trial_plan.hpp"
#include <algorithm>
#include <gtest/gtest.h>
#include <limits>

TEST(ResearchPlan, GoldenOrderDoesNotDependOnStdShuffleOrUniformDistribution)
{
    sv::research::Scenario scenario;
    scenario.variants.resize(3);
    scenario.seed = 5489;
    scenario.warmup = 1;
    scenario.repeats = 2;
    const auto plan = sv::research::make_trial_plan(scenario);
    const std::vector<unsigned> expected{1, 0, 2, 0, 1, 2, 0, 2, 1};
    ASSERT_EQ(plan.size(), expected.size());
    for (size_t i = 0; i < plan.size(); ++i)
    {
        EXPECT_EQ(plan[i].variant, expected[i]);
        EXPECT_EQ(plan[i].frame_index, 0U);
        EXPECT_EQ(plan[i].block, i / 3);
        EXPECT_EQ(plan[i].warmup, i < 3);
    }
    EXPECT_STREQ(sv::research::trial_order_algorithm, "mt19937_fisher_yates_reject_v1");
}

TEST(ResearchPlan, RepeatedSeedAndCompleteBlocksRemainStableAcrossFrames)
{
    sv::research::Scenario scenario;
    scenario.variants.resize(7);
    scenario.frames = 3;
    scenario.seed = 17;
    const auto first = sv::research::make_trial_plan(scenario);
    const auto wire = sv::research::trial_plan_json(first);
    EXPECT_EQ(wire, sv::research::trial_plan_json(sv::research::make_trial_plan(scenario)));
    ASSERT_EQ(first.size(), 105U);
    for (size_t begin = 0; begin < first.size(); begin += 7)
    {
        std::vector<unsigned> variants;
        for (size_t i = begin; i < begin + 7; ++i)
        {
            variants.push_back(first[i].variant);
            EXPECT_EQ(first[i].block, (begin / 7) % 5);
            EXPECT_EQ(first[i].frame_index, begin / 35);
            EXPECT_EQ(first[i].warmup, (begin / 7) % 5 < 2);
            EXPECT_EQ(wire[i].as_object().at("trial_index").to_number<size_t>(), i);
        }
        std::sort(variants.begin(), variants.end());
        EXPECT_EQ(variants, (std::vector<unsigned>{0, 1, 2, 3, 4, 5, 6}));
    }
    scenario.seed = 18;
    EXPECT_NE(wire, sv::research::trial_plan_json(sv::research::make_trial_plan(scenario)));
}

TEST(ResearchPlan, AggregateBoundsAreCheckedBeforeAllocation)
{
    sv::research::Scenario scenario;
    scenario.variants.resize(1);
    scenario.repeats = 1000;
    scenario.warmup = 0;
    scenario.frames = 10;
    EXPECT_EQ(sv::research::make_trial_plan(scenario).size(), 10000U);
    scenario.frames = 11;
    EXPECT_THROW(sv::research::make_trial_plan(scenario), std::invalid_argument);
    scenario.frames = 1;
    scenario.repeats = std::numeric_limits<unsigned>::max();
    EXPECT_THROW(sv::research::make_trial_plan(scenario), std::invalid_argument);
    scenario.repeats = 1;
    scenario.variants.clear();
    EXPECT_THROW(sv::research::make_trial_plan(scenario), std::invalid_argument);
}

TEST(ResearchMatrix, CartesianExpansionHasCanonicalAxisOrderAndExplicitValueOrder)
{
    auto input = sv::parse_json(
        R"({"schema_version":1,"matrix":{"base":{"mode":"multi_band"},"axes":{"pyramid_levels":[2,4],"pyramid_boundary":["zero","normalized"]}}})");
    const auto scenario = sv::research::parse_scenario(input);
    ASSERT_EQ(scenario.variants.size(), 4U);
    EXPECT_EQ(scenario.variants[0].fusion.pyramid_boundary, "zero");
    EXPECT_EQ(scenario.variants[1].fusion.pyramid_boundary, "zero");
    EXPECT_EQ(scenario.variants[2].fusion.pyramid_boundary, "normalized");
    EXPECT_EQ(scenario.variants[0].fusion.pyramid_levels, 2U);
    EXPECT_EQ(scenario.variants[1].fusion.pyramid_levels, 4U);
    auto &axes = input.at("matrix").as_object().at("axes");
    axes = sv::parse_json(R"({"pyramid_boundary":["zero","normalized"],"pyramid_levels":[2,4]})");
    const auto reordered = sv::research::parse_scenario(input);
    for (size_t i = 0; i < scenario.variants.size(); ++i)
    {
        EXPECT_EQ(scenario.variants[i].fusion.pyramid_boundary,
                  reordered.variants[i].fusion.pyramid_boundary);
        EXPECT_EQ(scenario.variants[i].fusion.pyramid_levels,
                  reordered.variants[i].fusion.pyramid_levels);
    }
}

TEST(ResearchMatrix, RejectsAmbiguousMalformedAndOversizedDefinitions)
{
    for (
        const auto *text :
        {R"({"schema_version":1})",
         R"({"schema_version":1,"variants":[],"matrix":{"axes":{"mode":["multi_band"]}}})",
         R"({"schema_version":1,"matrix":{"axes":{}}})",
         R"({"schema_version":1,"matrix":{"axes":{"mode":[]}}})",
         R"({"schema_version":1,"matrix":{"axes":{"mode":"multi_band"}}})",
         R"({"schema_version":1,"matrix":{"axes":{"mode":["unsupported"]}}})",
         R"({"schema_version":1,"matrix":{"base":{"mode":"multi_band"},"axes":{"shader":["code"]}}})",
         R"({"schema_version":1,"matrix":{"axes":{"mode":["multi_band"]},"unknown":true}})"})
    {
        EXPECT_ANY_THROW(sv::research::parse_scenario(sv::parse_json(text))) << text;
    }
    boost::json::array values;
    for (int i = 0; i < 6; ++i)
    {
        values.push_back(i + 1);
    }
    const boost::json::object matrix{
        {"base", boost::json::object{{"mode", "multi_band"}}},
        {"axes", boost::json::object{{"pyramid_levels", values}, {"edge_width_px", values}}}};
    EXPECT_THROW(sv::research::expand_variant_matrix(matrix), std::invalid_argument);
}
