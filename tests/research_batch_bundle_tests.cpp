#include "batch_bundle.hpp"
#include "research_bundle_fixture.hpp"

namespace
{
struct BatchBundleFixture : bundle_fixture::BundleFixture
{
    boost::json::object plan;

    void SetUp() override
    {
        BundleFixture::SetUp();
        plan = {
            {"schema_version", 1},
            {"runs", boost::json::array{
                         boost::json::object{{"id", "first"}, {"scenario", "scenario.json"}},
                         boost::json::object{{"id", "second"}, {"scenario", "scenario.json"}}}}};
        save_plan();
    }

    void save_plan()
    {
        bundle_fixture::json(source / "plan # %.json", plan);
    }

    boost::json::object export_batch()
    {
        return sv::research::export_batch_bundle(source / "plan # %.json", bundle);
    }
};
} // namespace

TEST_F(BatchBundleFixture, RelocatesFreezesAllMembersAndReservesEveryReportBeforeExecution)
{
    const auto exported = export_batch();
    const auto moved = directory.path / "relocated # %";
    std::filesystem::rename(bundle, moved);
    sv::research::BatchBundle loaded(moved);
    EXPECT_EQ(loaded.summary().at("manifest_sha256"), exported.at("manifest_sha256"));
    EXPECT_EQ(loaded.summary().at("trials").to_number<unsigned>(), 2U);
    EXPECT_EQ(loaded.summary().at("reference_decoded_bytes").to_number<unsigned>(), 48U);
    EXPECT_EQ(loaded.summary().at("runs").as_array()[1].as_object().at("id"), "second");
    const auto original = sv::research::read_regular_file(source / "plan # %.json", 65536);
    EXPECT_EQ(std::string(exported.at("source_plan_sha256").as_string()),
              sv::research::sha256(original.data(), original.size()));
    std::filesystem::remove_all(source);
    std::filesystem::remove_all(moved);
    sv::client::Options options;
    options.endpoint.directory = (directory.path / "no-server").string();
    const auto output = directory.path / "results";
    unsigned callbacks = 0;
    const auto summary =
        loaded.execute(output, options, nullptr,
                       [&](unsigned member, unsigned done, unsigned total)
                       {
                           EXPECT_EQ(member, 0U);
                           EXPECT_EQ(done, 0U);
                           EXPECT_EQ(total, 1U);
                           EXPECT_TRUE(std::filesystem::exists(output / "first.json"));
                           EXPECT_TRUE(std::filesystem::exists(output / "second.json"));
                           ++callbacks;
                       });
    EXPECT_GT(callbacks, 0U);
    EXPECT_FALSE(summary.at("success").as_bool()); // deliberate missing endpoint
    EXPECT_EQ(summary.at("input_bundle"), loaded.summary());
    EXPECT_EQ(summary.at("runs").as_array()[1].as_object().at("status"), "not_started");
    const auto report = bundle_fixture::document(output / "first.json");
    EXPECT_EQ(report.at("scenario_input_kind"), "verified_bundle_snapshot_v1");
    EXPECT_TRUE(report.contains("quality_reference"));
    EXPECT_EQ(report.at("input_bundle").as_object().at("manifest_sha256"),
              loaded.summary().at("runs").as_array()[0].as_object().at("manifest_sha256"));
    EXPECT_ANY_THROW(loaded.execute(directory.path / "retry", options));
    EXPECT_FALSE(std::filesystem::exists(directory.path / "retry"));
}

TEST_F(BatchBundleFixture, LateInvalidInputAndAggregateTrialBudgetLeaveNoDestination)
{
    plan.at("runs").as_array()[1].as_object()["scenario"] = "missing.json";
    save_plan();
    EXPECT_ANY_THROW(export_batch());
    EXPECT_FALSE(std::filesystem::exists(bundle));
    plan.at("runs").as_array()[1].as_object()["scenario"] = "scenario.json";
    save_plan();
    scenario["reset_replay"] = false;
    bundle_fixture::json(source / "scenario.json", scenario);
    EXPECT_ANY_THROW(export_batch());
    EXPECT_FALSE(std::filesystem::exists(bundle));
    scenario["reset_replay"] = true;
    scenario.erase("quality_reference");
    scenario.erase("quality_criteria");
    scenario["frames"] = 8;
    scenario["warmup"] = 0;
    scenario["repeats"] = 1000; // individually valid 8000, aggregate invalid 16000
    bundle_fixture::json(source / "scenario.json", scenario);
    EXPECT_ANY_THROW(export_batch());
    EXPECT_FALSE(std::filesystem::exists(bundle));
}

TEST_F(BatchBundleFixture, RefusesOverwriteAndHonorsPrecancelBeforeOutput)
{
    std::atomic_bool cancel{true};
    EXPECT_ANY_THROW(sv::research::export_batch_bundle(source / "plan # %.json", bundle, &cancel));
    EXPECT_FALSE(std::filesystem::exists(bundle));
    export_batch();
    const auto original = sv::research::read_regular_file(bundle / "bundle.json", 65536);
    EXPECT_ANY_THROW(export_batch());
    EXPECT_EQ(sv::research::read_regular_file(bundle / "bundle.json", 65536), original);
    EXPECT_ANY_THROW(sv::research::BatchBundle(bundle, &cancel));
    sv::research::BatchBundle loaded(bundle);
    EXPECT_ANY_THROW(loaded.execute(directory.path / "cancel-output", {}, &cancel));
    EXPECT_FALSE(std::filesystem::exists(directory.path / "cancel-output"));
    for (const auto &path :
         {std::string{}, (directory.path / "truncated").string() + '\0' + "suffix"})
    {
        sv::research::BatchBundle invalid(bundle);
        EXPECT_ANY_THROW(invalid.execute(path, {}));
    }
    EXPECT_FALSE(std::filesystem::exists(directory.path / "truncated"));
}

TEST_F(BatchBundleFixture, RejectsTamperedLateChildMissingFilesAndNoFollowViolations)
{
    export_batch();
    const auto original =
        sv::research::read_regular_file(bundle / "run-1" / "bundle.json", 1024 * 1024);
    std::ofstream(bundle / "run-1" / "bundle.json", std::ios::app) << ' ';
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    std::ofstream(bundle / "run-1" / "bundle.json") << original;
    std::filesystem::rename(bundle / "run-1", directory.path / "child");
    std::filesystem::create_directory_symlink(directory.path / "child", bundle / "run-1");
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    std::filesystem::remove(bundle / "run-1");
    std::filesystem::rename(directory.path / "child", bundle / "run-1");
    const auto asset = bundle / "run-1" / "assets" / "frame-0-rgb.ppm";
    std::filesystem::rename(asset, directory.path / "rgb.ppm");
    std::filesystem::create_symlink(directory.path / "rgb.ppm", asset);
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    std::filesystem::remove(asset);
    ASSERT_EQ(::mkfifo(asset.c_str(), 0600), 0);
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    std::filesystem::remove(asset);
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    std::filesystem::rename(directory.path / "rgb.ppm", asset);
    const auto root_link = directory.path / "root-link";
    std::filesystem::create_directory_symlink(bundle, root_link);
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(root_link / ""));
    EXPECT_NO_THROW(sv::research::BatchBundle loaded(bundle));
}

TEST_F(BatchBundleFixture, StrictManifestRejectsVersionsUnknownFieldsIdsPathsAndHashes)
{
    export_batch();
    const auto manifest = bundle_fixture::document(bundle / "bundle.json");
    auto reject = [&](boost::json::object value)
    {
        bundle_fixture::json(bundle / "bundle.json", value);
        EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
    };
    auto value = manifest;
    value["schema_version"] = 2;
    reject(value);
    value = manifest;
    value["schema_version"] = -1;
    reject(value);
    value = manifest;
    value["extra"] = true;
    reject(value);
    value = manifest;
    value["runs"] = boost::json::array{};
    reject(value);
    value = manifest;
    value.at("runs").as_array()[1].as_object()["id"] = "first";
    reject(value);
    value = manifest;
    value.at("runs").as_array()[1].as_object()["directory"] = "../escape";
    reject(value);
    value = manifest;
    value.at("runs").as_array()[0].as_object()["manifest_sha256"] = std::string(64, '0');
    reject(value);
    value = manifest;
    value.at("runs").as_array()[0].as_object()["extra"] = true;
    reject(value);
    value = manifest;
    value.at("runs").as_array()[0].as_object()["id"] = "batch";
    reject(value);
    value = manifest;
    value["source_plan_sha256"] = "bad";
    reject(value);
    bundle_fixture::json(bundle / "bundle.json", manifest);
    EXPECT_NO_THROW(sv::research::BatchBundle loaded(bundle));
}

TEST_F(BatchBundleFixture, AggregateRawBudgetRejectsEvenWhenEveryChildIsIndividuallyValid)
{
    reference["width"] = 2048;
    reference["height"] = 1024;
    sv::write_ppm(source / "rgb # %.ppm",
                  sv::Image{2048, 1024, 3, std::vector<unsigned char>(2048 * 1024 * 3, 10)});
    sv::write_ppm(source / "mask.ppm",
                  sv::Image{2048, 1024, 3, std::vector<unsigned char>(2048 * 1024 * 3, 255)});
    bundle_fixture::json(source / "reference.json", reference);
    auto &runs = plan.at("runs").as_array();
    for (unsigned index = 2; index < 6; ++index)
    {
        runs.push_back(boost::json::object{{"id", "case-" + std::to_string(index)},
                                           {"scenario", "scenario.json"}});
    }
    save_plan();
    const auto single = directory.path / "single";
    const auto child = sv::research::export_scenario_bundle(source / "scenario.json", single);
    EXPECT_NO_THROW(sv::research::ScenarioBundle loaded(single));
    EXPECT_ANY_THROW(export_batch()); // six ~12MiB children exceed aggregate 64MiB
    EXPECT_FALSE(std::filesystem::exists(bundle));
    // Build a syntactically valid oversized input too; verify must apply its own budget.
    std::filesystem::create_directory(bundle);
    boost::json::array descriptors;
    const auto manifest = bundle_fixture::document(single / "bundle.json");
    for (unsigned index = 0; index < 6; ++index)
    {
        const auto name = "run-" + std::to_string(index);
        const auto run = bundle / name;
        std::filesystem::create_directory(run);
        std::filesystem::create_directory(run / "assets");
        std::filesystem::create_hard_link(single / "bundle.json", run / "bundle.json");
        for (const auto &file : manifest.at("files").as_array())
        {
            const std::string path(file.as_object().at("path").as_string());
            std::filesystem::create_hard_link(single / path, run / path);
        }
        descriptors.push_back(boost::json::object{
            {"id", name}, {"directory", name}, {"manifest_sha256", child.at("manifest_sha256")}});
    }
    bundle_fixture::json(bundle / "bundle.json", {{"schema_version", 1},
                                                  {"kind", "batch_bundle_v1"},
                                                  {"scope", "client_scenarios_and_references_only"},
                                                  {"source_plan_sha256", std::string(64, 'a')},
                                                  {"runs", descriptors}});
    EXPECT_ANY_THROW(sv::research::BatchBundle loaded(bundle));
}

TEST_F(BatchBundleFixture, UnsavedPlanExportUsesExactBytesBaseAndOrderWithoutAPlanFile)
{
    std::swap(plan.at("runs").as_array()[0], plan.at("runs").as_array()[1]);
    const auto text = "\n " + boost::json::serialize(plan) + "\n";
    std::filesystem::remove(source / "plan # %.json");
    const auto receipt = sv::research::export_batch_bundle(text, source, bundle);
    EXPECT_EQ(std::string(receipt.at("source_plan_sha256").as_string()),
              sv::research::sha256(text.data(), text.size()));
    EXPECT_EQ(receipt.at("runs").as_array()[0].as_object().at("id"), "second");
    EXPECT_FALSE(std::filesystem::exists(source / "plan # %.json"));
    std::filesystem::remove_all(source);
    EXPECT_NO_THROW(sv::research::BatchBundle loaded(bundle));
}

TEST_F(BatchBundleFixture, MemoryPlanExportRejectsBadBaseOversizeAndLateMemberBeforeDestination)
{
    const auto text = boost::json::serialize(plan);
    EXPECT_ANY_THROW(sv::research::export_batch_bundle(text, {}, bundle));
    EXPECT_ANY_THROW(
        sv::research::export_batch_bundle(text, source.string() + '\0' + "bad", bundle));
    EXPECT_ANY_THROW(
        sv::research::export_batch_bundle(text, source, bundle.string() + '\0' + "bad"));
    EXPECT_ANY_THROW(sv::research::export_batch_bundle(std::string(65537, ' '), source, bundle));
    plan.at("runs").as_array()[1].as_object()["scenario"] = "missing.json";
    EXPECT_ANY_THROW(
        sv::research::export_batch_bundle(boost::json::serialize(plan), source, bundle));
    EXPECT_FALSE(std::filesystem::exists(bundle));
}
