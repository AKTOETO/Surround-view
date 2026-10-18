#include "bundle_io.hpp"
#include "digest.hpp"
#include "regular_input.hpp"
#include "scenario_bundle.hpp"
#include "sv/frame.hpp"
#include "sv/json.hpp"
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>

namespace
{
void json(const std::filesystem::path &path, const boost::json::object &value)
{
    std::ofstream(path) << boost::json::serialize(value) << '\n';
}

boost::json::object document(const std::filesystem::path &path)
{
    return sv::parse_json(sv::research::read_regular_file(path, 1024 * 1024)).as_object();
}

struct BundleFixture : testing::Test
{
    sv::research::bundle_detail::TemporaryDirectory directory;
    std::filesystem::path source = directory.path / "original # %",
                          bundle = directory.path / "bundle";
    boost::json::object scenario, reference;

    void SetUp() override
    {
        std::filesystem::create_directory(source);
        const boost::json::object view{{"azimuth_rad", 0.},
                                       {"elevation_rad", 1.},
                                       {"distance_m", 8.},
                                       {"fov_y_rad", 1.},
                                       {"clip_m", boost::json::array{.1, 50.}},
                                       {"target_m", boost::json::array{0., 0., 0.}}};
        boost::json::array guards;
        for (unsigned i = 0; i < 4; ++i)
        {
            guards.push_back(boost::json::object{
                {"camera_id", i}, {"calibration_id", "fixture"}, {"source_timestamp_ns", "0"}});
        }
        reference = {
            {"schema_version", 1},
            {"kind", "regression"},
            {"dataset_id", "bundle-fixture"},
            {"width", 2},
            {"height", 2},
            {"view", view},
            {"criteria", boost::json::object{{"max_mae_rgb8", 1.}}},
            {"frames", boost::json::array{boost::json::object{
                           {"rgb", "rgb # %.ppm"}, {"mask", "mask.ppm"}, {"inputs", guards}}}}};
        scenario = {{"schema_version", 1},
                    {"frames", 1},
                    {"warmup", 0},
                    {"repeats", 1},
                    {"reset_replay", true},
                    {"variants", boost::json::array{boost::json::object{{"mode", "edge_feather"}}}},
                    {"quality_reference", "reference.json"},
                    {"quality_criteria", boost::json::object{{"max_mae_rgb8", .5}}}};
        sv::write_ppm(source / "rgb # %.ppm",
                      sv::Image{2, 2, 3, std::vector<unsigned char>(12, 10)});
        sv::write_ppm(source / "mask.ppm", sv::Image{2, 2, 3, std::vector<unsigned char>(12, 255)});
        json(source / "reference.json", reference);
        json(source / "scenario.json", scenario);
    }

    boost::json::object export_bundle()
    {
        return sv::research::export_scenario_bundle(source / "scenario.json", bundle);
    }

    // Updating a descriptor lets negative tests reach semantic/closure checks rather than only
    // hashes.
    void update_descriptor(boost::json::object &manifest, const std::string &name)
    {
        const auto bytes = sv::research::read_regular_file(bundle / name, 1024 * 1024);
        for (auto &entry : manifest.at("files").as_array())
        {
            if (std::string(entry.as_object().at("path").as_string()) == name)
            {
                entry.as_object()["bytes"] = bytes.size();
                entry.as_object()["sha256"] = sv::research::sha256(bytes.data(), bytes.size());
            }
        }
    }
};
} // namespace

TEST_F(BundleFixture, PortableSnapshotRetainsPixelsCriteriaAndSourceDigest)
{
    const auto original = sv::research::read_regular_file(source / "scenario.json", 1024 * 1024);
    const auto exported = export_bundle();
    const auto packed = document(bundle / "scenario.json");
    EXPECT_EQ(packed.at("quality_reference"), "reference.json");
    EXPECT_EQ(packed.at("quality_criteria"), scenario.at("quality_criteria"));
    EXPECT_EQ(std::string(exported.at("source_scenario_sha256").as_string()),
              sv::research::sha256(original.data(), original.size()));
    const auto moved = directory.path / "moved bundle # %";
    std::filesystem::rename(bundle, moved);
    sv::research::ScenarioBundle loaded(moved);
    EXPECT_EQ(loaded.summary().at("manifest_sha256"), exported.at("manifest_sha256"));
    EXPECT_EQ(loaded.summary().at("files").to_number<unsigned>(), 4U);
    EXPECT_EQ(loaded.summary().at("trials").to_number<unsigned>(), 1U);
    EXPECT_EQ(loaded.summary().at("reference_decoded_bytes").to_number<unsigned>(), 24U);
    std::filesystem::remove_all(source);
    std::filesystem::remove_all(moved);
    sv::client::Options options;
    options.endpoint.directory = (directory.path / "no-server").string();
    options.timeout_ms = 20;
    options.max_retries = 0;
    const auto report = loaded.execute(directory.path / "report.json", options);
    EXPECT_FALSE(
        report.at("success").as_bool()); // Deliberately no server; inputs were already frozen.
    EXPECT_EQ(report.at("input_bundle").as_object().at("manifest_sha256"),
              exported.at("manifest_sha256"));
    const auto &quality = report.at("quality_reference").as_object();
    EXPECT_EQ(quality.at("criteria_source"), "scenario_override_v1");
    EXPECT_EQ(quality.at("criteria").as_object().at("max_mae_rgb8").to_number<double>(), .5);
    EXPECT_EQ(
        quality.at("frames").as_array()[0].as_object().at("selected_pixels").to_number<unsigned>(),
        4U);
    EXPECT_ANY_THROW(loaded.execute(directory.path / "second.json", options));
    EXPECT_FALSE(std::filesystem::exists(directory.path / "second.json"));
}

TEST_F(BundleFixture, NoReferenceUsesOnlyScenarioAndNeverOverwritesExistingOutput)
{
    scenario.erase("quality_reference");
    scenario.erase("quality_criteria");
    json(source / "scenario.json", scenario);
    const auto exported = export_bundle();
    EXPECT_EQ(exported.at("files").to_number<unsigned>(), 1U);
    sv::research::ScenarioBundle loaded(bundle);
    EXPECT_EQ(loaded.summary().at("reference_decoded_bytes").to_number<unsigned>(), 0U);
    const auto before = sv::research::read_regular_file(bundle / "bundle.json", 1024 * 1024);
    EXPECT_ANY_THROW(export_bundle());
    EXPECT_EQ(sv::research::read_regular_file(bundle / "bundle.json", 1024 * 1024), before);
}

TEST_F(BundleFixture, InvalidReferenceAndCancellationDoNotCreateDestination)
{
    std::atomic_bool cancel{true};
    EXPECT_ANY_THROW(
        sv::research::export_scenario_bundle(source / "scenario.json", bundle, &cancel));
    EXPECT_FALSE(std::filesystem::exists(bundle));
    sv::write_ppm(source / "mask.ppm", sv::Image{2, 2, 3, std::vector<unsigned char>(12, 0)});
    EXPECT_ANY_THROW(export_bundle());
    EXPECT_FALSE(std::filesystem::exists(bundle));
}

TEST_F(BundleFixture, ChangedMissingAndSymlinkedMembersNeverVerify)
{
    export_bundle();
    const auto rgb = bundle / "assets/frame-0-rgb.ppm";
    const auto original = sv::research::read_regular_file(rgb, 1024);
    std::ofstream(rgb, std::ios::binary | std::ios::app) << 'x';
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    std::filesystem::remove(rgb);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    std::filesystem::create_symlink(source / "rgb # %.ppm", rgb);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    std::filesystem::remove(rgb);
    ASSERT_EQ(::mkfifo(rgb.c_str(), 0600), 0);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    std::filesystem::remove(rgb);
    sv::research::bundle_detail::write_new(rgb, original, nullptr);
    const auto link = directory.path / "directory-link";
    std::filesystem::create_directory_symlink(bundle, link);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{link});
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{std::filesystem::path(link.string() + "/")});
    const auto assets = bundle / "assets";
    std::filesystem::rename(assets, bundle / "real-assets");
    std::filesystem::create_directory_symlink(bundle / "real-assets", assets);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
}

TEST_F(BundleFixture, UnsafePathsAndOpenReferencesFailEvenWithUpdatedHashes)
{
    export_bundle();
    auto manifest = document(bundle / "bundle.json");
    auto altered = document(bundle / "reference.json");
    altered.at("frames").as_array()[0].as_object()["rgb"] = "../outside.ppm";
    json(bundle / "reference.json", altered);
    update_descriptor(manifest, "reference.json");
    json(bundle / "bundle.json", manifest);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    altered.at("frames").as_array()[0].as_object()["rgb"] = "assets/frame-1-rgb.ppm";
    json(bundle / "reference.json", altered);
    update_descriptor(manifest, "reference.json");
    json(bundle / "bundle.json", manifest);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
    manifest.at("files").as_array()[0].as_object()["path"] = "/etc/passwd";
    json(bundle / "bundle.json", manifest);
    EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle});
}

TEST_F(BundleFixture, ManifestVersionsLimitsAndFrozenDescriptionAreStrict)
{
    export_bundle();
    const auto original = document(bundle / "bundle.json");
    for (unsigned variation = 0; variation < 6; ++variation)
    {
        auto manifest = original;
        switch (variation)
        {
        case 0:
            manifest["schema_version"] = -4294967295LL;
            break;
        case 1:
            manifest["extra"] = true;
            break;
        case 2:
            manifest.at("files").as_array().push_back(manifest.at("files").as_array().front());
            break;
        case 3:
            manifest.at("files").as_array()[0].as_object()["bytes"] = 16 * 1024 * 1024 + 1;
            break;
        case 4:
            manifest["reference_description"] = nullptr;
            break;
        case 5:
            manifest.at("files").as_array()[0].as_object()["sha256"] = std::string(64, 'z');
            break;
        }
        json(bundle / "bundle.json", manifest);
        EXPECT_ANY_THROW(sv::research::ScenarioBundle{bundle}) << variation;
    }
}
