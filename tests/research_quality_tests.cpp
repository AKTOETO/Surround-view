#include "file_experiment.hpp"
#include "quality_metrics.hpp"
#include "quality_reference.hpp"
#include "sv/frame.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <gtest/gtest.h>
#include <limits>

namespace
{
struct ReferenceFixture
{
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sv-quality-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    boost::json::object manifest;
    sv::Message frame;

    ReferenceFixture()
    {
        std::filesystem::create_directory(directory);
        sv::Image rgb{2, 2, 3, std::vector<unsigned char>(12, 10)};
        sv::Image mask{2, 2, 3, std::vector<unsigned char>(12, 255)};
        sv::write_ppm(directory / "rgb.ppm", rgb);
        sv::write_ppm(directory / "mask.ppm", mask);
        const boost::json::object view{{"azimuth_rad", 0.0},
                                       {"elevation_rad", 1.0},
                                       {"distance_m", 8.0},
                                       {"fov_y_rad", 1.0},
                                       {"clip_m", boost::json::array{0.1, 50.0}},
                                       {"target_m", boost::json::array{0.0, 0.0, 0.0}}};
        boost::json::array guards, inputs;
        for (unsigned i = 0; i < 4; ++i)
        {
            boost::json::object guard{{"camera_id", i},
                                      {"calibration_id", "cal-" + std::to_string(i)},
                                      {"source_timestamp_ns", "0"}};
            guards.push_back(guard);
            guard["used"] = true;
            inputs.push_back(guard);
        }
        manifest = {{"schema_version", 1},
                    {"kind", "regression"},
                    {"dataset_id", "test-fixture"},
                    {"width", 2},
                    {"height", 2},
                    {"view", view},
                    {"frames", boost::json::array{boost::json::object{
                                   {"rgb", "rgb.ppm"}, {"mask", "mask.ppm"}, {"inputs", guards}}}}};
        frame = {11,
                 {{"width", 2},
                  {"height", 2},
                  {"view", view},
                  {"stride_bytes", 8},
                  {"row_origin", "top_left"},
                  {"pixel_format", "RGBA8"},
                  {"health", "READY"},
                  {"source_type", "replay"},
                  {"output_product", "final_frame"},
                  {"diagnostic_view", "color"},
                  {"inputs", inputs}},
                 std::vector<unsigned char>(16, 10)};
        save();
    }

    void save() const
    {
        std::ofstream(directory / "reference.json") << boost::json::serialize(manifest);
    }

    ~ReferenceFixture()
    {
        std::error_code error;
        std::filesystem::remove_all(directory, error);
    }
};
} // namespace

TEST(ResearchQuality, KnownAnswerMaskAndPerfectMatch)
{
    // Only first pixel selected. Alpha and excluded pixels must not affect RGB metrics.
    const auto result = sv::research::masked_rgb_metrics(
        {13, 24, 30, 0, 255, 255, 255, 255}, {10, 20, 30, 0, 0, 0}, {255, 255, 255, 0, 0, 0});
    EXPECT_EQ(result.at("selected_pixels").to_number<unsigned>(), 1U);
    EXPECT_DOUBLE_EQ(result.at("mae_rgb8").as_double(), 7.0 / 3);
    EXPECT_DOUBLE_EQ(result.at("rmse_rgb8").as_double(), std::sqrt(25.0 / 3));
    EXPECT_EQ(result.at("max_absolute_rgb8").to_number<unsigned>(), 4U);
    EXPECT_NEAR(result.at("psnr_db").as_double(), 38.92261606915535, 1e-10);
    EXPECT_FALSE(result.at("exact_match").as_bool());
    const auto exact =
        sv::research::masked_rgb_metrics({10, 20, 30, 99}, {10, 20, 30}, {255, 255, 255});
    EXPECT_TRUE(exact.at("exact_match").as_bool());
    EXPECT_TRUE(exact.at("psnr_db").is_null());
}

TEST(ResearchQuality, RejectsInvalidMasksAndBuffers)
{
    EXPECT_THROW(sv::research::masked_rgb_metrics({}, {}, {}), std::invalid_argument);
    EXPECT_THROW(sv::research::masked_rgb_metrics({1, 2, 3}, {1, 2, 3}, {255, 255, 255}),
                 std::invalid_argument);
    for (const auto &mask : {std::vector<unsigned char>{0, 0, 0}, {128, 128, 128}, {255, 0, 255}})
    {
        EXPECT_THROW(sv::research::masked_rgb_metrics({1, 2, 3, 4}, {1, 2, 3}, mask),
                     std::invalid_argument);
    }
}

TEST(ResearchQuality, ManifestIdentityAndGuardedEvaluation)
{
    ReferenceFixture fixture;
    // JSON integer syntax for an exactly equal view must not reject a FRAME double.
    fixture.manifest.at("view").as_object()["azimuth_rad"] = 0;
    fixture.manifest.at("view").as_object()["distance_m"] = 8;
    fixture.manifest.at("view").as_object().at("target_m").as_array()[0] = 0;
    fixture.save();
    sv::research::QualityReference reference(fixture.directory / "reference.json");
    sv::research::Scenario scenario;
    scenario.reset_replay = true;
    scenario.variants.resize(1);
    EXPECT_NO_THROW(reference.validate(scenario));
    const auto description = reference.description();
    EXPECT_EQ(description.at("kind"), "regression");
    EXPECT_EQ(description.at("manifest_sha256").as_string().size(), 64U);
    const auto &identity = description.at("frames").as_array()[0].as_object();
    EXPECT_EQ(identity.at("rgb_pixels_sha256").as_string().size(), 64U);
    EXPECT_EQ(identity.at("mask_pixels_sha256").as_string().size(), 64U);
    auto evaluate = [&]()
    { return reference.evaluate({0, 0, 0, false, std::make_shared<sv::Message>(fixture.frame)}); };
    EXPECT_TRUE(evaluate().at("exact_match").as_bool());
    for (const auto &field :
         {"view", "width", "height", "row_origin", "pixel_format", "stride_bytes", "health",
          "source_type", "output_product", "diagnostic_view", "inputs"})
    {
        const auto original = fixture.frame.header.at(field);
        fixture.frame.header[field] = nullptr;
        EXPECT_ANY_THROW(evaluate()) << field;
        fixture.frame.header[field] = original;
    }
    fixture.frame.header.at("inputs").as_array()[0].as_object()["source_timestamp_ns"] = "1";
    EXPECT_THROW(evaluate(), std::runtime_error);
    scenario.reset_replay = false;
    EXPECT_THROW(reference.validate(scenario), std::invalid_argument);
    scenario.reset_replay = true;
    scenario.frames = 2;
    EXPECT_THROW(reference.validate(scenario), std::invalid_argument);
    scenario.frames = 1;
    scenario.variants[0].fusion.diagnostic = "coverage";
    EXPECT_THROW(reference.validate(scenario), std::invalid_argument);
}

TEST(ResearchQuality, RejectsMalformedManifestAndEmptyMaskBeforeConnection)
{
    ReferenceFixture fixture;
    const auto good = fixture.manifest;
    for (const auto &patch : {boost::json::object{{"kind", "self_evident_truth"}},
                              {{"width", 1}},
                              {{"width", 2.5}},
                              {{"frames", boost::json::array{}}},
                              {{"unknown", true}}})
    {
        fixture.manifest = good;
        for (const auto &field : patch)
        {
            fixture.manifest[field.key()] = field.value();
        }
        fixture.save();
        EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
    }
    fixture.manifest = good;
    fixture.manifest.at("frames")
        .as_array()[0]
        .as_object()
        .at("inputs")
        .as_array()[0]
        .as_object()["source_timestamp_ns"] = "00";
    fixture.save();
    EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
    fixture.manifest = good;
    fixture.save();
    sv::write_ppm(fixture.directory / "mask.ppm", {2, 2, 3, std::vector<unsigned char>(12, 0)});
    EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
}

TEST(ResearchQuality, SummariesCountFramesRatherThanPausedRepeats)
{
    const auto a = sv::research::masked_rgb_metrics({10, 20, 30, 0}, {0, 0, 0}, {255, 255, 255});
    const auto b = sv::research::masked_rgb_metrics({20, 20, 20, 0}, {0, 0, 0}, {255, 255, 255});
    boost::json::array samples;
    for (unsigned repeat = 0; repeat < 5; ++repeat)
    {
        samples.push_back(boost::json::object{
            {"frame_index", 0}, {"variant", 0}, {"warmup", false}, {"quality", a}});
    }
    samples.push_back(
        boost::json::object{{"frame_index", 1}, {"variant", 0}, {"warmup", false}, {"quality", b}});
    samples.push_back(
        boost::json::object{{"frame_index", 2}, {"variant", 0}, {"warmup", true}, {"quality", b}});
    const auto summaries = sv::research::quality_summaries(samples, 2);
    EXPECT_EQ(summaries[0].as_object().at("frames").to_number<unsigned>(), 2U);
    EXPECT_EQ(summaries[0].as_object().at("selected_pixels").to_number<unsigned>(), 2U);
    EXPECT_DOUBLE_EQ(summaries[0].as_object().at("pixel_weighted_mae_rgb8").as_double(), 20);
    EXPECT_EQ(summaries[1].as_object().at("frames").to_number<unsigned>(), 0U);
    samples.push_back(
        boost::json::object{{"frame_index", 0}, {"variant", 0}, {"warmup", false}, {"quality", b}});
    EXPECT_THROW(sv::research::quality_summaries(samples, 1), std::runtime_error);
}

TEST(ResearchQuality, WeightedPsnrUsesUniqueFrameMseAndRepresentsExactMatch)
{
    const auto a = sv::research::masked_rgb_metrics({1, 1, 1, 0}, {0, 0, 0}, {255, 255, 255});
    const auto b = sv::research::masked_rgb_metrics({3, 3, 3, 0, 3, 3, 3, 0, 3, 3, 3, 0},
                                                    std::vector<unsigned char>(9, 0),
                                                    std::vector<unsigned char>(9, 255));
    const auto zero = sv::research::masked_rgb_metrics({0, 0, 0, 0}, {0, 0, 0}, {255, 255, 255});
    boost::json::array samples;
    for (unsigned repeat = 0; repeat < 5; ++repeat)
    {
        samples.push_back(boost::json::object{
            {"frame_index", 0}, {"variant", 0}, {"warmup", false}, {"quality", a}});
    }
    samples.push_back(
        boost::json::object{{"frame_index", 1}, {"variant", 0}, {"warmup", false}, {"quality", b}});
    samples.push_back(boost::json::object{
        {"frame_index", 0}, {"variant", 1}, {"warmup", false}, {"quality", zero}});
    const auto summaries = sv::research::quality_summaries(samples, 3);
    EXPECT_NEAR(summaries[0].as_object().at("pixel_weighted_psnr_db").as_double(),
                39.67982320853653, 1e-12);
    EXPECT_FALSE(summaries[0].as_object().at("exact_match").as_bool());
    EXPECT_TRUE(summaries[1].as_object().at("exact_match").as_bool());
    EXPECT_TRUE(summaries[1].as_object().at("pixel_weighted_psnr_db").is_null());
    EXPECT_FALSE(summaries[2].as_object().contains("pixel_weighted_psnr_db"));
}

TEST(ResearchQuality, ProjectionAndMemoryBoundsAreValidatedBeforeLoadingImages)
{
    ReferenceFixture fixture;
    const auto good = fixture.manifest;
    for (const auto &patch : {boost::json::object{{"fov_y_rad", 0}},
                              {{"clip_m", boost::json::array{1.0, 0.1}}},
                              {{"target_m", boost::json::array{0.0, 0.0}}}})
    {
        fixture.manifest = good;
        for (const auto &field : patch)
        {
            fixture.manifest.at("view").as_object()[field.key()] = field.value();
        }
        fixture.save();
        EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
    }
    fixture.manifest = good;
    fixture.manifest["width"] = 4096;
    fixture.manifest["height"] = 2160;
    auto &frames = fixture.manifest.at("frames").as_array();
    const auto frame = frames[0];
    frames.push_back(frame);
    frames.push_back(frame); // 3 x RGB+mask at this resolution exceeds 128 MiB.
    fixture.save();
    EXPECT_THROW(sv::research::QualityReference{fixture.directory / "reference.json"},
                 std::invalid_argument);
}

TEST(ResearchQuality, MissingEvaluatorIsRejectedBeforeConnection)
{
    auto scenario = sv::research::parse_scenario(sv::parse_json(
        R"({"schema_version":1,"reset_replay":true,"quality_reference":"quality/reference.json","variants":[{"mode":"edge_feather"}]})"));
    const auto report = sv::research::run(scenario, {});
    EXPECT_FALSE(report.at("success").as_bool());
    EXPECT_FALSE(report.at("restore_required").as_bool());
    EXPECT_EQ(report.at("error"), "quality_reference requires a loaded evaluator");
    for (const auto &path : {boost::json::value(""), boost::json::value(42),
                             boost::json::value(std::string(4097, 'x'))})
    {
        auto document = report.at("scenario").as_object();
        document["quality_reference"] = path;
        EXPECT_ANY_THROW(sv::research::parse_scenario(document));
    }
}

TEST(ResearchQuality, SchemaTwoRequiresImageAndNumericCameraGuards)
{
    ReferenceFixture fixture;
    fixture.manifest["schema_version"] = 2;
    fixture.save();
    EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
    for (unsigned camera = 0; camera < 4; ++camera)
    {
        auto &guard = fixture.manifest.at("frames")
                          .as_array()[0]
                          .as_object()
                          .at("inputs")
                          .as_array()[camera]
                          .as_object();
        guard["image_identity"] = boost::json::object{{"algorithm", "packed_rgb8_sha256_v1"},
                                                      {"sha256", std::string(64, 'a')},
                                                      {"width", 2},
                                                      {"height", 2},
                                                      {"pixel_format", "RGB8"},
                                                      {"row_origin", "top_left"}};
        guard["camera_model_sha256"] = std::string(64, 'b');
        auto &actual = fixture.frame.header.at("inputs").as_array()[camera].as_object();
        actual["image_identity"] = guard.at("image_identity");
        actual["camera_model_sha256"] = guard.at("camera_model_sha256");
    }
    fixture.save();
    sv::research::QualityReference reference(fixture.directory / "reference.json");
    EXPECT_TRUE(reference.requires_content_identity());
    auto evaluate = [&]()
    { return reference.evaluate({0, 0, 0, false, std::make_shared<sv::Message>(fixture.frame)}); };
    EXPECT_TRUE(evaluate().at("exact_match").as_bool());
    auto &first = fixture.frame.header.at("inputs").as_array()[0].as_object();
    first.at("image_identity").as_object()["sha256"] = std::string(64, 'c');
    EXPECT_THROW(evaluate(), std::runtime_error);
    first.at("image_identity").as_object()["sha256"] = std::string(64, 'a');
    first["camera_model_sha256"] = std::string(64, 'c');
    EXPECT_THROW(evaluate(), std::runtime_error);
    fixture.manifest.at("frames")
        .as_array()[0]
        .as_object()
        .at("inputs")
        .as_array()[0]
        .as_object()["camera_model_sha256"] = "not-a-digest";
    fixture.save();
    EXPECT_ANY_THROW(sv::research::QualityReference{fixture.directory / "reference.json"});
}

TEST(ResearchQuality, ScenarioCriteriaReplaceManifestWithoutMutatingOrMergingIt)
{
    ReferenceFixture fixture;
    fixture.manifest["criteria"] = boost::json::object{{"max_mae_rgb8", 0}, {"min_psnr_db", 200}};
    fixture.save();
    const auto original = boost::json::serialize(fixture.manifest);
    fixture.frame.payload[0] = 11;
    const sv::research::Sample sample{0, 0, 0, false, std::make_shared<sv::Message>(fixture.frame)};
    sv::research::QualityReference inherited(fixture.directory / "reference.json");
    EXPECT_FALSE(inherited.evaluate(sample).at("acceptance").as_object().at("passed").as_bool());
    const auto overrideCriteria = sv::research::parse_quality_criteria({{"max_mae_rgb8", 2}});
    sv::research::QualityReference selected(fixture.directory / "reference.json", overrideCriteria);
    const auto description = selected.description();
    EXPECT_EQ(description.at("criteria_source"), "scenario_override_v1");
    EXPECT_EQ(description.at("manifest_criteria"),
              sv::research::quality_criteria_json(sv::research::parse_quality_criteria(
                  fixture.manifest.at("criteria").as_object())));
    EXPECT_EQ(description.at("manifest_sha256"), inherited.description().at("manifest_sha256"));
    EXPECT_FALSE(description.at("criteria").as_object().contains("min_psnr_db"));
    EXPECT_TRUE(selected.evaluate(sample).at("acceptance").as_object().at("passed").as_bool());
    std::ifstream input(fixture.directory / "reference.json");
    EXPECT_EQ(std::string(std::istreambuf_iterator<char>(input), {}), original);
    // Later disk edits cannot change the evaluator's already prepared criteria or pixels.
    fixture.manifest["criteria"] = boost::json::object{{"max_mae_rgb8", 255}};
    fixture.save();
    EXPECT_EQ(selected.description(), description);
    EXPECT_TRUE(selected.evaluate(sample).at("acceptance").as_object().at("passed").as_bool());
}

TEST(ResearchQuality, CriteriaOverrideRequiresMatchingScenarioAndRejectsInvalidTypedValues)
{
    ReferenceFixture fixture;
    const auto overrideCriteria = sv::research::parse_quality_criteria({{"max_mae_rgb8", 0}});
    sv::research::QualityReference selected(fixture.directory / "reference.json", overrideCriteria);
    sv::research::Scenario scenario;
    scenario.frames = 1;
    scenario.reset_replay = true;
    scenario.variants.push_back({sv::client::FusionSettings{}, {}});
    EXPECT_ANY_THROW(selected.validate(scenario));
    scenario.quality_criteria = overrideCriteria;
    EXPECT_NO_THROW(selected.validate(scenario));
    scenario.quality_criteria->max_mae_rgb8 = 1;
    EXPECT_ANY_THROW(selected.validate(scenario));
    EXPECT_ANY_THROW(sv::research::QualityReference(fixture.directory / "reference.json",
                                                    sv::research::QualityCriteria{}));
    auto invalid = overrideCriteria;
    invalid.max_mae_rgb8 = std::numeric_limits<double>::quiet_NaN();
    EXPECT_ANY_THROW(sv::research::QualityReference(fixture.directory / "reference.json", invalid));
    sv::research::QualityReference noCriteria(fixture.directory / "reference.json");
    EXPECT_EQ(noCriteria.description().at("criteria_source"), "not_requested");
    fixture.manifest["criteria"] = boost::json::object{{"max_mae_rgb8", 1}};
    fixture.save();
    sv::research::QualityReference inherited(fixture.directory / "reference.json");
    EXPECT_EQ(inherited.description().at("criteria_source"), "manifest_v1");
}

TEST(ResearchQuality, ScenarioOverridePreflightAndCancellationReportRetainEffectivePolicy)
{
    ReferenceFixture fixture;
    fixture.manifest["criteria"] = boost::json::object{{"max_mae_rgb8", 5}};
    fixture.save();
    boost::json::object document{
        {"schema_version", 1},
        {"reset_replay", true},
        {"warmup", 0},
        {"repeats", 1},
        {"quality_reference", "reference.json"},
        {"quality_criteria", boost::json::object{{"max_rmse_rgb8", .000001}}},
        {"variants", boost::json::array{boost::json::object{{"mode", "edge_feather"}}}}};
    const auto text = boost::json::serialize(document);
    sv::research::FileExperiment prepared(text, fixture.directory);
    document.erase("quality_reference");
    EXPECT_ANY_THROW(sv::research::parse_scenario(document));
    prepared.reserve(fixture.directory / "cancelled.json");
    std::atomic_bool cancel{true};
    const auto report = prepared.execute(sv::client::Options{}, &cancel);
    EXPECT_FALSE(report.at("success").as_bool());
    EXPECT_EQ(report.at("scenario").as_object().at("quality_criteria"),
              document.at("quality_criteria"));
    const auto &reference = report.at("quality_reference").as_object();
    EXPECT_EQ(reference.at("criteria_source"), "scenario_override_v1");
    EXPECT_EQ(reference.at("manifest_criteria"),
              sv::research::quality_criteria_json(sv::research::parse_quality_criteria(
                  fixture.manifest.at("criteria").as_object())));
    EXPECT_FALSE(reference.at("criteria").as_object().contains("max_mae_rgb8"));
}
