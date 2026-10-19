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
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
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

TEST(ResearchComparison, CaptureAndProgressFlagsAreRequiredEvenForMatchingRenderSamples)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("scenario").as_object()["capture_frames"] = true;
    auto &policy = b.at("observation_policy").as_object();
    policy["capture_frames"] = true;
    policy["capture_consumer"] = "rgba8_files_v1";
    auto result = sv::research::compare_reports(a, b);
    EXPECT_EQ(result.at("observation_gate"), "different_or_unverified_observation");
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_FALSE(first(result).at("mae_delta").is_null());
    EXPECT_FALSE(first(sv::research::compare_reports(b, b)).at("timing_delta_ms").is_null());
    policy["capture_consumer"] = "callback_unverified";
    EXPECT_TRUE(first(sv::research::compare_reports(b, b)).at("timing_delta_ms").is_null());
    b = a;
    b.at("observation_policy").as_object()["progress_callback"] = true;
    EXPECT_TRUE(first(sv::research::compare_reports(a, b)).at("timing_delta_ms").is_null());
}

TEST(ResearchComparison, LegacyUnknownMalformedAndUnstableObservationClaimsDisableOnlyTiming)
{
    const auto a = comparison_fixture::report();
    for (unsigned variation = 0; variation < 8; ++variation)
    {
        auto b = a;
        auto &p = b.at("observation_policy").as_object();
        switch (variation)
        {
        case 0:
            b.erase("observation_policy");
            break;
        case 1:
            p["policy"] = "future_observation_v2";
            break;
        case 2:
            p["telemetry_stable"] = false;
            break;
        case 3:
            p["telemetry_before"] = nullptr;
            break;
        case 4:
            p.at("telemetry_after").as_object()["period_ms"] = 200;
            break;
        case 5:
            p["extra"] = 1;
            break;
        case 6:
            b.at("scenario").as_object()["capture_frames"] = "false";
            break;
        case 7:
            p["frame_digest"] = "none";
            break;
        }
        // Invalid scenario field types are rejected by the existing scenario parser.
        if (variation == 6)
        {
            EXPECT_ANY_THROW(sv::research::compare_reports(a, b));
            continue;
        }
        const auto result = sv::research::compare_reports(a, b);
        EXPECT_TRUE(first(result).at("timing_delta_ms").is_null()) << variation;
        EXPECT_FALSE(first(result).at("mae_delta").is_null()) << variation;
        EXPECT_TRUE(result.at("right").as_object().at("observation_policy").is_null());
    }
}

TEST(ResearchComparison, EvaluatorReferenceAndCriteriaAffectTimingButNotEquivalentRawQuality)
{
    const auto a = comparison_fixture::report();
    auto b = a;
    b.at("quality_reference").as_object()["criteria"] = boost::json::object{{"max_mae_rgb8", 2}};
    auto result = sv::research::compare_reports(a, b);
    EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
    EXPECT_FALSE(first(result).at("mae_delta").is_null());
    b = a;
    b.erase("quality_reference");
    b.at("observation_policy").as_object()["quality_evaluation"] = false;
    b.at("observation_policy").as_object()["quality_algorithm"] = "none";
    EXPECT_TRUE(first(sv::research::compare_reports(a, b)).at("timing_delta_ms").is_null());
    result = sv::research::compare_reports(b, b);
    EXPECT_FALSE(first(result).at("timing_delta_ms").is_null());
    EXPECT_TRUE(first(result).at("mae_delta").is_null());
}

TEST(ResearchComparison, DifferentServerTelemetrySettingsDisableTimingWithoutHidingMetrics)
{
    const auto a = comparison_fixture::report();
    for (const auto *field : {"enabled", "period_ms", "history_capacity"})
    {
        auto b = a;
        auto &policy = b.at("observation_policy").as_object();
        for (const auto *when : {"telemetry_before", "telemetry_after"})
        {
            auto &telemetry = policy.at(when).as_object();
            telemetry[field] = std::string_view(field) == "enabled" ? boost::json::value(true)
                                                                    : boost::json::value(200);
        }
        const auto result = sv::research::compare_reports(a, b);
        EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
        EXPECT_FALSE(first(result).at("left").as_object().at("render_p50_ms").is_null());
        EXPECT_FALSE(first(result).at("mae_delta").is_null());
        EXPECT_FALSE(first(sv::research::compare_reports(b, b)).at("timing_delta_ms").is_null());
    }
}

TEST(ResearchComparison, MissingTamperedOrDifferentConfiguredBuildBlocksOnlyTiming)
{
    const auto a = comparison_fixture::report();
    for (int kind = 0; kind < 5; ++kind)
    {
        auto b = a;
        auto &catalog = b.at("catalog").as_object();
        if (kind == 0)
        {
            catalog.erase("build_passport");
        }
        else if (kind == 1)
        {
            catalog.at("build_passport").as_object()["sha256"] = std::string(64, '0');
        }
        else if (kind == 2)
        {
            catalog.at("build_passport").as_object()["policy"] = "future";
        }
        else
        {
            auto descriptor = catalog.at("build_passport").as_object().at("descriptor").as_object();
            descriptor.at("configuration").as_object()[kind == 3 ? "name" : "cxx_flags"] =
                "changed";
            catalog["build_passport"] = sv::make_build_passport(descriptor);
        }
        const auto result = sv::research::compare_reports(a, b);
        EXPECT_EQ(result.at("policy"), "report_claims_comparison_v3");
        EXPECT_EQ(result.at("build_gate"), "different_or_unverified_configured_build");
        EXPECT_TRUE(first(result).at("timing_delta_ms").is_null());
        EXPECT_EQ(first(result).at("mae_delta").to_number<double>(), 0);
        EXPECT_FALSE(first(result).at("right").is_null());
    }
}
