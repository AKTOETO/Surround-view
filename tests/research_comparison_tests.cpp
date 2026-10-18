#include "regular_input.hpp"
#include "report_comparison.hpp"
#include "research_comparison_fixture.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>

namespace
{
const boost::json::object &first(const boost::json::object &r)
{
    return r.at("rows").as_array()[0].as_object();
}

void reorder(boost::json::object &r)
{
    auto &variants = r.at("scenario").as_object().at("variants").as_array();
    std::swap(variants[0], variants[1]);
    for (auto *key : {"samples", "summaries", "quality_summaries"})
    {
        for (auto &v : r.at(key).as_array())
        {
            v.as_object()["variant"] = 1 - v.as_object().at("variant").to_number<unsigned>();
        }
    }
}
} // namespace

TEST(ResearchComparison, MatchesFullSettingsAcrossReorderedVariantsAndNumericViewTypes)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    reorder(b);
    for (auto &s : b.at("samples").as_array())
    {
        s.as_object().at("metadata").as_object().at("view").as_object()["distance_m"] = 8;
    }
    const auto result = sv::research::compare_reports(a, b);
    EXPECT_EQ(result.at("inputs"), "matching_claims");
    EXPECT_EQ(result.at("timing_gate"), "matching_workload_claims");
    EXPECT_EQ(result.at("quality_gate"), "matching_reference_claims");
    EXPECT_EQ(first(result).at("right_index").to_number<unsigned>(), 1U);
    EXPECT_EQ(first(result).at("timing_delta_ms").to_number<double>(), 0);
    EXPECT_EQ(first(result).at("mae_delta").to_number<double>(), 0);
}

TEST(ResearchComparison, ChangedContentWithSameIdsAndViewDisablesAllDeltas)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    for (auto &s : b.at("samples").as_array())
    {
        s.as_object()
            .at("metadata")
            .as_object()
            .at("inputs")
            .as_array()[0]
            .as_object()
            .at("image_identity")
            .as_object()["sha256"] = std::string(64, 'e');
    }
    b.at("frame_baselines")
        .as_array()[0]
        .as_object()
        .at("metadata")
        .as_object()
        .at("inputs")
        .as_array()[0]
        .as_object()
        .at("image_identity")
        .as_object()["sha256"] = std::string(64, 'e');
    const auto result = sv::research::compare_reports(a, b);
    EXPECT_EQ(result.at("inputs"), "different_or_unverified");
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_TRUE(first(result).at("mae_delta").is_null());
}

TEST(ResearchComparison, MaskMismatchAndIncompleteRunsRemainVisibleWithoutAcceptance)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("quality_reference")
        .as_object()
        .at("frames")
        .as_array()[0]
        .as_object()["mask_pixels_sha256"] = std::string(64, 'e');
    auto result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("mae_delta").is_null());
    EXPECT_FALSE(first(result).at("timing_delta_ms").is_null());
    b["success"] = false;
    result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_FALSE(result.at("right").as_object().at("success").as_bool());
}

TEST(ResearchComparison, DifferentOrDuplicateProfilesAreNeverGreedilyMatched)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    for (auto &v : b.at("samples").as_array())
    {
        v.as_object().at("metadata").as_object().at("surface").as_object()["dome_radius_m"] = 15;
    }
    auto result = sv::research::compare_reports(a, b);
    EXPECT_EQ(result.at("rows").as_array().size(), 4U);
    EXPECT_EQ(first(result).at("pairing"), "left_only");
    b = a;
    b.at("scenario").as_object().at("variants").as_array()[1] =
        b.at("scenario").as_object().at("variants").as_array()[0];
    b.at("samples").as_array()[1].as_object().at("metadata").as_object()["fusion"] =
        b.at("samples").as_array()[0].as_object().at("metadata").as_object().at("fusion");
    result = sv::research::compare_reports(a, b);
    EXPECT_EQ(first(result).at("pairing"), "ambiguous");
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
}

TEST(ResearchComparison, MalformedCountersMetricsAndBatchInputFailInsteadOfShowingZeros)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("samples").as_array().push_back(b.at("samples").as_array()[0]);
    EXPECT_ANY_THROW(sv::research::compare_reports(a, b));
    b = a;
    b.at("summaries")
        .as_array()[0]
        .as_object()
        .at("timings")
        .as_object()
        .at("render_wall")
        .as_object()["p50_ms"] = -1;
    EXPECT_ANY_THROW(sv::research::compare_reports(a, b));
    EXPECT_ANY_THROW(sv::research::compare_reports(
        a, boost::json::object{{"schema_version", 1}, {"runs", boost::json::array{}}}));
    std::atomic_bool cancelled{true};
    EXPECT_ANY_THROW(sv::research::compare_reports(a, a, &cancelled));
}

TEST(ResearchComparison, MissingMetricsAndBuildEvidenceNeverBecomeZeroDeltas)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.erase("catalog");
    b.erase("quality_reference");
    b.erase("summaries");
    b.erase("quality_summaries");
    const auto result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("right").as_object().at("render_p50_ms").is_null());
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_TRUE(first(result).at("mae_delta").is_null());
}

TEST(ResearchComparison, BoundedReaderRejectsSpecialFilesAndHonorsCancellation)
{
    const auto root = std::filesystem::temp_directory_path() /
                      ("sv-compare-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    ASSERT_EQ(::mkfifo((root / "pipe").c_str(), 0600), 0);
    EXPECT_ANY_THROW(sv::research::read_regular_file(root / "pipe", 64));
    std::ofstream(root / "large") << std::string(65, 'x');
    EXPECT_ANY_THROW(sv::research::read_regular_file(root / "large", 64));
    std::atomic_bool cancel{true};
    EXPECT_ANY_THROW(sv::research::read_regular_file(root / "large", 64, &cancel));
    std::filesystem::remove_all(root);
}

TEST(ResearchComparison, BuildAndPercentileChangesOnlyDisableTimingDeltas)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("catalog").as_object()["source_fingerprint"] = std::string(64, 'd');
    auto result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_FALSE(first(result).at("mae_delta").is_null());
    b = a;
    b.erase("percentile_definition");
    result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_FALSE(first(result).at("mae_delta").is_null());
}

TEST(ResearchComparison, UnknownTruthAndIncorrectQualityCountsNeverProduceQualityDeltas)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("quality_reference").as_object()["kind"] = "unknown";
    EXPECT_TRUE(first(sv::research::compare_reports(b, b)).at("mae_delta").is_null());
    b = a;
    b.at("quality_summaries").as_array()[0].as_object()["selected_pixels"] = 2;
    EXPECT_TRUE(first(sv::research::compare_reports(b, b)).at("mae_delta").is_null());
    b = a;
    b.at("samples")
        .as_array()[0]
        .as_object()
        .at("metadata")
        .as_object()
        .at("inputs")
        .as_array()[0]
        .as_object()["camera_model_sha256"] = std::string(64, 'd');
    EXPECT_EQ(sv::research::compare_reports(a, b).at("inputs"), "different_or_unverified");
}

TEST(ResearchComparison, ExactMatchKeepsInfinitePsnrDistinctFromMissingLegacyMetrics)
{
    auto a = comparison_fixture::report();
    for (auto &q : a.at("quality_summaries").as_array())
    {
        auto &o = q.as_object();
        o["exact_match"] = true;
        o["pixel_weighted_mae_rgb8"] = 0;
        o["pixel_weighted_rmse_rgb8"] = 0;
        o["pixel_weighted_psnr_db"] = nullptr;
    }
    auto b = a;
    b.at("quality_summaries").as_array()[0].as_object().erase("pixel_weighted_psnr_db");
    b.at("quality_summaries").as_array()[0].as_object().erase("exact_match");
    const auto result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("left").as_object().at("exact_match").as_bool());
    EXPECT_FALSE(first(result).at("right").as_object().at("exact_match").as_bool());
    EXPECT_TRUE(first(result).at("right").as_object().at("psnr").is_null());
    EXPECT_EQ(first(result).at("mae_delta").to_number<double>(), 0);
}

TEST(ResearchComparison, InvalidInputShapeAndTrialOrderCannotPassGuards)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("frame_baselines")
        .as_array()[0]
        .as_object()
        .at("metadata")
        .as_object()
        .at("inputs")
        .as_array()[0]
        .as_object()
        .at("image_identity")
        .as_object()["width"] = 0;
    EXPECT_EQ(sv::research::compare_reports(a, b).at("inputs"), "different_or_unverified");
    b = a;
    b.at("samples").as_array()[0].as_object()["trial_index"] = 999;
    EXPECT_ANY_THROW(sv::research::compare_reports(a, b));
}

TEST(ResearchComparison, DifferentAcceptancePoliciesAreVisibleWithoutChangingRawMetricDeltas)
{
    auto a = comparison_fixture::report();
    a.at("quality_reference").as_object()["criteria"] = boost::json::object{{"max_mae_rgb8", 2}};
    a.at("quality_reference").as_object()["criteria_source"] = "manifest_v1";
    auto b = a;
    b.at("quality_reference").as_object()["criteria"] = boost::json::object{{"max_mae_rgb8", 3}};
    b.at("quality_reference").as_object()["criteria_source"] = "scenario_override_v1";
    const auto result = sv::research::compare_reports(a, b);
    EXPECT_EQ(result.at("quality_gate"), "matching_reference_claims");
    EXPECT_FALSE(first(result).at("mae_delta").is_null());
    EXPECT_EQ(result.at("reasons").as_array()[0], "acceptance_policy_differs");
    EXPECT_NE(result.at("left").as_object().at("criteria"),
              result.at("right").as_object().at("criteria"));
}
