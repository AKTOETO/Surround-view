#include "batch.hpp"
#include "digest.hpp"
#include "file_experiment.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
class ResearchBatch : public ::testing::Test
{
  protected:

    std::filesystem::path root =
        std::filesystem::temp_directory_path() /
        ("sv-batch-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::atomic_bool cancel{true};

    void SetUp() override
    {
        std::filesystem::create_directory(root);
        write(
            "scenario.json",
            R"({"schema_version":1,"reset_replay":true,"warmup":0,"repeats":1,"variants":[{"mode":"multi_band"}]})");
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    void write(const std::string &name, const std::string &text)
    {
        std::ofstream(root / name) << text;
    }

    boost::json::object plan()
    {
        return {
            {"schema_version", 1},
            {"runs", boost::json::array{
                         boost::json::object{{"id", "first"}, {"scenario", "scenario.json"}},
                         boost::json::object{{"id", "second"}, {"scenario", "scenario.json"}}}}};
    }

    boost::json::object execute(const boost::json::object &document)
    {
        write("batch-input.json", boost::json::serialize(document));
        return sv::research::run_batch(root / "batch-input.json", root / "output", {}, &cancel);
    }
};
} // namespace

TEST_F(ResearchBatch, CancelBeforeFirstRunDoesNotConnectAndPreservesReservedOutputs)
{
    const auto summary = execute(plan());
    EXPECT_EQ(summary.at("status"), "cancelled");
    EXPECT_FALSE(summary.at("success").as_bool());
    EXPECT_TRUE(summary.at("restored").as_bool()); // no server mutation attempted
    EXPECT_EQ(summary.at("completed").to_number<unsigned>(), 0U);
    EXPECT_EQ(summary.at("reported").to_number<unsigned>(), 0U);
    EXPECT_EQ(sv::research::quality_status(summary), "incomplete");
    EXPECT_EQ(sv::research::research_exit_code(summary), 4);
    EXPECT_EQ(std::filesystem::file_size(root / "output" / "first.json"), 0U);
    EXPECT_EQ(std::filesystem::file_size(root / "output" / "second.json"), 0U);
    std::ifstream report(root / "output" / "batch.json");
    EXPECT_EQ(boost::json::parse(report), summary);
    EXPECT_THROW(execute(plan()), std::runtime_error);
}

TEST_F(ResearchBatch, EveryMemberIsPreflightedBeforeCreatingOutput)
{
    auto document = plan();
    document.at("runs").as_array()[1].as_object()["scenario"] = "absent.json";
    EXPECT_ANY_THROW(execute(document));
    EXPECT_FALSE(std::filesystem::exists(root / "output"));
    write(
        "bad-reference.json",
        R"({"schema_version":1,"reset_replay":true,"quality_reference":"missing.json","variants":[{"mode":"multi_band"}]})");
    document.at("runs").as_array()[1].as_object()["scenario"] = "bad-reference.json";
    EXPECT_ANY_THROW(execute(document));
    EXPECT_FALSE(std::filesystem::exists(root / "output"));
}

TEST_F(ResearchBatch, RejectsAmbiguousOrUnboundedPlansBeforeOutput)
{
    for (const auto *id : {"../escape", ".", "a/b", "first", "", "a.json", "batch"})
    {
        auto document = plan();
        document.at("runs").as_array()[1].as_object()["id"] = id;
        EXPECT_ANY_THROW(execute(document)) << id;
        EXPECT_FALSE(std::filesystem::exists(root / "output"));
    }
    for (const auto &version :
         {boost::json::value(true), boost::json::value(1.0), boost::json::value(2)})
    {
        auto document = plan();
        document["schema_version"] = version;
        EXPECT_ANY_THROW(execute(document));
    }
    auto document = plan();
    document["unknown"] = true;
    EXPECT_ANY_THROW(execute(document));
    document = plan();
    document["runs"] = boost::json::array{};
    EXPECT_ANY_THROW(execute(document));
    for (unsigned i = 0; i < 9; ++i)
    {
        document.at("runs").as_array().push_back(
            boost::json::object{{"id", "run" + std::to_string(i)}, {"scenario", "scenario.json"}});
    }
    EXPECT_ANY_THROW(execute(document));
    EXPECT_FALSE(std::filesystem::exists(root / "output"));
}

TEST_F(ResearchBatch, RequiresResetAndBoundsAggregateTrials)
{
    write("scenario.json", R"({"schema_version":1,"variants":[{"mode":"multi_band"}]})");
    EXPECT_ANY_THROW(execute(plan()));
    write(
        "scenario.json",
        R"({"schema_version":1,"reset_replay":true,"warmup":0,"frames":6,"repeats":1000,"variants":[{"mode":"multi_band"}]})");
    EXPECT_ANY_THROW(execute(plan()));
    EXPECT_FALSE(std::filesystem::exists(root / "output"));
}

TEST_F(ResearchBatch, PreparedScenarioCannotBeChangedAndCanOnlyExecuteOnce)
{
    sv::research::FileExperiment experiment(root / "scenario.json");
    const auto hash = experiment.input_sha256();
    EXPECT_EQ(experiment.trial_count(), 1U);
    EXPECT_EQ(experiment.reference_bytes(), 0U);
    write("scenario.json", "invalid replacement");
    EXPECT_ANY_THROW(experiment.execute({}, &cancel));
    experiment.reserve(root / "single.json");
    const auto report = experiment.execute({}, &cancel);
    EXPECT_EQ(std::string(report.at("scenario_input_sha256").as_string()), hash);
    EXPECT_TRUE(report.at("scenario").as_object().at("reset_replay").as_bool());
    EXPECT_EQ(report.at("error"), "cancelled");
    EXPECT_ANY_THROW(experiment.execute({}, &cancel));
    EXPECT_ANY_THROW(experiment.reserve(root / "again.json"));
}

TEST_F(ResearchBatch, ReportAliasAndDanglingSymlinkNeverOverwriteInputsOrTargets)
{
    sv::research::FileExperiment first(root / "scenario.json");
    EXPECT_ANY_THROW(first.reserve(root / "scenario.json"));
    std::filesystem::create_hard_link(root / "scenario.json", root / "alias.json");
    EXPECT_ANY_THROW(first.reserve(root / "alias.json"));
    std::filesystem::create_symlink("absent-target.json", root / "link.json");
    EXPECT_ANY_THROW(first.reserve(root / "link.json"));
    EXPECT_TRUE(std::filesystem::is_symlink(root / "link.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "absent-target.json"));
}

TEST_F(ResearchBatch, FailedCaptureReservationCannotBeExecuted)
{
    write("scenario.json",
          R"({"schema_version":1,"capture_frames":true,"variants":[{"mode":"multi_band"}]})");
    std::filesystem::create_directory(root / "report.json.frames");
    sv::research::FileExperiment experiment(root / "scenario.json");
    EXPECT_ANY_THROW(experiment.reserve(root / "report.json"));
    EXPECT_ANY_THROW(experiment.execute({}, &cancel));
}
