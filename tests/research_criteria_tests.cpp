#include "quality_criteria.hpp"
#include "quality_metrics.hpp"
#include <gtest/gtest.h>

TEST(ResearchCriteria, BoundsAndUnknownFieldsAreRejectedBeforeConnection)
{
    for (const auto &object : {boost::json::object{},
                               {{"unknown", 1}},
                               {{"max_mae_rgb8", -1}},
                               {{"max_rmse_rgb8", 256}},
                               {{"min_psnr_db", 201}},
                               {{"max_mae_rgb8", "1"}},
                               {{"max_mae_rgb8", true}}})
    {
        EXPECT_ANY_THROW(sv::research::parse_quality_criteria(object));
    }
    const boost::json::object values{
        {"max_mae_rgb8", 0}, {"max_rmse_rgb8", 255}, {"min_psnr_db", 200}};
    const auto normalized =
        sv::research::quality_criteria_json(sv::research::parse_quality_criteria(values));
    EXPECT_EQ(normalized.size(), values.size());
    for (const auto &field : values)
    {
        EXPECT_DOUBLE_EQ(normalized.at(field.key()).to_number<double>(),
                         field.value().to_number<double>());
    }
}

TEST(ResearchCriteria, InclusiveThresholdsAndPerfectMatchPsnr)
{
    const auto exact = sv::research::masked_rgb_metrics({1, 2, 3, 0}, {1, 2, 3}, {255, 255, 255});
    const auto criteria = sv::research::parse_quality_criteria(
        {{"max_mae_rgb8", 0}, {"max_rmse_rgb8", 0}, {"min_psnr_db", 200}});
    EXPECT_TRUE(sv::research::check_quality_criteria(exact, criteria).at("passed").as_bool());
    const auto different =
        sv::research::masked_rgb_metrics({1, 2, 3, 0}, {0, 0, 0}, {255, 255, 255});
    const auto failed = sv::research::check_quality_criteria(different, criteria);
    EXPECT_FALSE(failed.at("passed").as_bool());
    EXPECT_EQ(failed.at("failed_criteria").as_array().size(), 3U);
    const auto inclusive =
        sv::research::parse_quality_criteria({{"max_mae_rgb8", different.at("mae_rgb8")},
                                              {"max_rmse_rgb8", different.at("rmse_rgb8")},
                                              {"min_psnr_db", different.at("psnr_db")}});
    EXPECT_TRUE(sv::research::check_quality_criteria(different, inclusive).at("passed").as_bool());
}

TEST(ResearchCriteria, PartialOrUnrestoredExperimentsCannotPass)
{
    const auto criteria = sv::research::parse_quality_criteria({{"max_mae_rgb8", 0}});
    const auto exact = sv::research::masked_rgb_metrics({0, 0, 0, 0}, {0, 0, 0}, {255, 255, 255});
    const auto different =
        sv::research::masked_rgb_metrics({1, 1, 1, 0}, {0, 0, 0}, {255, 255, 255});
    boost::json::array samples{
        boost::json::object{
            {"frame_index", 0}, {"variant", 0}, {"warmup", true}, {"quality", different}},
        boost::json::object{
            {"frame_index", 0}, {"variant", 0}, {"warmup", false}, {"quality", exact}}};
    EXPECT_EQ(sv::research::quality_acceptance(samples, {}, 1, true).at("status"), "not_requested");
    EXPECT_EQ(sv::research::quality_acceptance(samples, criteria, 1, true).at("status"), "passed");
    EXPECT_EQ(sv::research::quality_acceptance(samples, criteria, 1, false).at("status"),
              "incomplete");
    EXPECT_EQ(sv::research::quality_acceptance(samples, criteria, 2, true).at("status"),
              "incomplete");
    EXPECT_EQ(sv::research::quality_acceptance({}, criteria, 0, true).at("status"), "incomplete");
    samples.push_back(boost::json::object{
        {"frame_index", 1}, {"variant", 0}, {"warmup", false}, {"quality", different}});
    const auto failed = sv::research::quality_acceptance(samples, criteria, 2, true);
    EXPECT_EQ(failed.at("status"), "failed");
    EXPECT_EQ(failed.at("evaluated_frame_variants").to_number<unsigned>(), 2U);
    EXPECT_EQ(failed.at("failed_samples").to_number<unsigned>(), 1U);
    EXPECT_EQ(failed.at("failed_frame_variants").to_number<unsigned>(), 1U);
}
