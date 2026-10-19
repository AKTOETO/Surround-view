#include "digest.hpp"
#include "report_series.hpp"
#include "research_comparison_fixture.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>

namespace
{
boost::json::object report(double latency)
{
    auto result = comparison_fixture::report();
    for (size_t index = 0; index < 2; ++index)
    {
        auto &timing = result.at("summaries")
                           .as_array()[index]
                           .as_object()
                           .at("timings")
                           .as_object()
                           .at("render_wall")
                           .as_object();
        timing["p50_ms"] = latency + index;
        timing["p95_ms"] = latency + index + 1;
    }
    return result;
}

const boost::json::object &metric(const boost::json::object &result, const char *name,
                                  unsigned index = 0)
{
    return result.at("rows").as_array()[index].as_object().at(name).as_object();
}

struct SeriesFiles : ::testing::Test
{
    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("sv-report-series-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

    void SetUp() override
    {
        std::filesystem::create_directory(root);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(root);
    }

    void write(const char *name, const std::string &text)
    {
        std::ofstream(root / name) << text;
    }
};
} // namespace

TEST(ResearchReportSeries, ReportsMedianIsNotPooledFramesAndInputOrderControlsBaseline)
{
    const auto result = sv::research::compare_report_series({report(8), report(1), report(4)});
    EXPECT_EQ(result.at("policy"), "report_series_comparison_v1");
    EXPECT_EQ(result.at("pair_policy"), "report_claims_comparison_v4");
    EXPECT_EQ(result.at("report_count").to_number<unsigned>(), 3U);
    const auto &times = metric(result, "render_p50_ms");
    EXPECT_EQ(times.at("status"), "all_reports_comparable");
    EXPECT_EQ(times.at("eligible_reports").to_number<unsigned>(), 3U);
    EXPECT_DOUBLE_EQ(times.at("minimum").to_number<double>(), 1);
    EXPECT_DOUBLE_EQ(times.at("median").to_number<double>(), 4);
    EXPECT_DOUBLE_EQ(times.at("maximum").to_number<double>(), 8);
    const auto &first =
        result.at("comparisons").as_array()[0].as_object().at("comparison").as_object();
    EXPECT_DOUBLE_EQ(
        first.at("rows").as_array()[0].as_object().at("timing_delta_ms").to_number<double>(), -7);
    const auto even =
        sv::research::compare_report_series({report(1), report(2), report(4), report(8)});
    EXPECT_DOUBLE_EQ(metric(even, "render_p50_ms").at("median").to_number<double>(), 3);
}

TEST(ResearchReportSeries, FailedGuardNeverSilentlyDropsReportOrBlocksIndependentQuality)
{
    auto changed = report(4);
    changed.erase("client_build_passport");
    auto result = sv::research::compare_report_series({report(1), changed, report(8)});
    const auto &times = metric(result, "render_p50_ms");
    EXPECT_EQ(times.at("status"), "not_all_reports_comparable");
    EXPECT_EQ(times.at("eligible_reports").to_number<unsigned>(), 2U);
    EXPECT_TRUE(times.at("median").is_null());
    EXPECT_FALSE(metric(result, "mae_rgb8").at("median").is_null());
    changed = report(4);
    changed["success"] = false;
    result = sv::research::compare_report_series({report(1), changed, report(8)});
    EXPECT_TRUE(metric(result, "mae_rgb8").at("median").is_null());
    EXPECT_TRUE(metric(result, "render_p50_ms").at("median").is_null());
    EXPECT_FALSE(result.at("comparisons")
                     .as_array()[0]
                     .as_object()
                     .at("comparison")
                     .as_object()
                     .at("rows")
                     .as_array()[0]
                     .as_object()
                     .at("right")
                     .is_null());
}

TEST(ResearchReportSeries, MissingProfileIsVisibleAndOnlyAffectedAggregateIsUnavailable)
{
    auto reduced = report(4);
    reduced.at("scenario").as_object().at("variants").as_array().pop_back();
    for (const auto *key : {"samples", "summaries", "quality_summaries"})
    {
        reduced.at(key).as_array().pop_back();
    }
    const auto result = sv::research::compare_report_series({report(1), reduced, report(8)});
    // Different trial plans prohibit time aggregation for both profiles.
    EXPECT_TRUE(metric(result, "render_p50_ms").at("median").is_null());
    EXPECT_FALSE(metric(result, "mae_rgb8").at("median").is_null());
    EXPECT_TRUE(metric(result, "mae_rgb8", 1).at("median").is_null());
    EXPECT_EQ(metric(result, "mae_rgb8", 1).at("eligible_reports").to_number<unsigned>(), 2U);
}

TEST(ResearchReportSeries, DuplicateDocumentsCountsCancellationAndMalformedInputsFail)
{
    const auto a = report(1), b = report(2);
    EXPECT_ANY_THROW(sv::research::compare_report_series({a}));
    EXPECT_ANY_THROW(sv::research::compare_report_series(std::vector<boost::json::object>(9, a)));
    auto reordered = a;
    reordered.erase("success");
    reordered["success"] = true;
    EXPECT_ANY_THROW(sv::research::compare_report_series({a, reordered}));
    auto bad = b;
    bad["schema_version"] = 2;
    EXPECT_ANY_THROW(sv::research::compare_report_series({a, bad}));
    std::atomic_bool cancel{true};
    EXPECT_ANY_THROW(sv::research::compare_report_series({a, b}, &cancel));
}

TEST_F(SeriesFiles, ExactRawInputHashesRejectAliasesSpecialFilesAndAllowLiteralPaths)
{
    const auto bytes = boost::json::serialize(report(1)) + "\n  ";
    write("a # %.json", bytes);
    write("b.json", boost::json::serialize(report(2)));
    const auto result =
        sv::research::compare_report_series_files({root / "a # %.json", root / "b.json"});
    const auto &input = result.at("inputs").as_array()[0].as_object();
    EXPECT_EQ(std::string(input.at("sha256").as_string()),
              sv::research::sha256(bytes.data(), bytes.size()));
    std::filesystem::create_hard_link(root / "b.json", root / "alias.json");
    EXPECT_ANY_THROW(
        sv::research::compare_report_series_files({root / "b.json", root / "alias.json"}));
    ASSERT_EQ(::mkfifo((root / "pipe").c_str(), 0600), 0);
    EXPECT_ANY_THROW(
        sv::research::compare_report_series_files({root / "a # %.json", root / "pipe"}));
    EXPECT_ANY_THROW(sv::research::compare_report_series_files({root / "a # %.json", root}));
    EXPECT_ANY_THROW(sv::research::compare_report_series_files(
        {root / "a # %.json", std::filesystem::path(std::string("bad\0path", 8))}));
}

TEST_F(SeriesFiles, EnforcesPerFileAndAggregateByteBudgets)
{
    constexpr size_t mb = 1024 * 1024;
    auto value = report(1);
    auto text = boost::json::serialize(value);
    text.resize(16 * mb + 1, ' ');
    write("too-large", text);
    write("small", boost::json::serialize(report(2)));
    EXPECT_ANY_THROW(
        sv::research::compare_report_series_files({root / "small", root / "too-large"}));
    std::vector<std::filesystem::path> paths;
    for (unsigned index = 0; index < 5; ++index)
    {
        auto padded = boost::json::serialize(report(index + 1));
        padded.resize(13 * mb, ' ');
        const auto name = std::to_string(index);
        write(name.c_str(), padded);
        paths.push_back(root / name);
    }
    EXPECT_ANY_THROW(sv::research::compare_report_series_files(paths));
}
