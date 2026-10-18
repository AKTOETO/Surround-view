#include "batch.hpp"
#include "batch_plan.hpp"
#include "digest.hpp"
#include "file_experiment.hpp"
#include "sv/json.hpp"
#include <chrono>
#include <fstream>
#include <gtest/gtest.h>
#include <sys/stat.h>

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

TEST_F(ResearchBatch, MemoryInputRetainsExactBytesAndBaseWithoutCreatingSourceFile)
{
    const std::string text = R"({"schema_version":1,"variants":[{"mode":"multi_band"}]})";
    sv::research::FileExperiment first(text, root);
    sv::research::FileExperiment equivalent(text + "\n", root);
    EXPECT_NE(first.input_sha256(), equivalent.input_sha256());
    first.reserve(root / "memory-report.json");
    const auto result = first.execute({}, &cancel);
    EXPECT_EQ(result.at("scenario_input_kind"), "memory_json_bytes_v1");
    EXPECT_FALSE(result.contains("scenario_input_path"));
    EXPECT_EQ(std::string(result.at("scenario_input_base_directory").as_string()), root.string());
    EXPECT_ANY_THROW((sv::research::FileExperiment("", root)));
    EXPECT_ANY_THROW((sv::research::FileExperiment(std::string(1048577, ' '), root)));
    EXPECT_ANY_THROW((sv::research::FileExperiment(
        R"({"schema_version":1,"reset_replay":true,"quality_reference":"absent.json","variants":[{"mode":"multi_band"}]})",
        root)));
}

TEST_F(ResearchBatch, CompiledPlanPreservesOrderHashAndReusableSnapshot)
{
    auto document = plan();
    std::swap(document.at("runs").as_array()[0], document.at("runs").as_array()[1]);
    const auto summary = sv::research::run_batch(document, root, root / "memory", {}, &cancel);
    EXPECT_EQ(summary.at("plan"), document);
    EXPECT_EQ(summary.at("runs").as_array()[0].as_object().at("id"), "second");
    EXPECT_EQ(summary.at("plan_snapshot"), "inputs/plan.json");
    std::ifstream input(root / "memory/inputs/plan.json", std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(input)), {});
    EXPECT_EQ(text, boost::json::serialize(document) + '\n');
    EXPECT_EQ(std::string(summary.at("plan_sha256").as_string()),
              sv::research::sha256(text.data(), text.size()));
    // Relative entries are relative to the supplied base, not the snapshot directory.
    // GUI plans contain absolute paths and can be rerun directly from their snapshot.
    for (auto &member : document.at("runs").as_array())
    {
        member.as_object()["scenario"] = (root / "scenario.json").string();
    }
    const auto absolute = sv::research::run_batch(document, root, root / "absolute", {}, &cancel);
    const auto replay =
        sv::research::run_batch(root / "absolute/inputs/plan.json", root / "rerun", {}, &cancel);
    EXPECT_EQ(absolute.at("plan_sha256"), replay.at("plan_sha256"));
    EXPECT_EQ(absolute.at("runs"), replay.at("runs"));
    EXPECT_FALSE(replay.contains("plan_snapshot"));
    EXPECT_THROW(sv::research::run_batch(document, root, root / "absolute", {}, &cancel),
                 std::runtime_error);
}

TEST_F(ResearchBatch, CompiledPlanUsesTheSameStrictPreflightBeforeOutput)
{
    auto document = plan();
    document.at("runs").as_array()[1].as_object()["scenario"] = "missing.json";
    EXPECT_ANY_THROW(sv::research::run_batch(document, root, root / "bad", {}, &cancel));
    EXPECT_FALSE(std::filesystem::exists(root / "bad"));
    document = plan();
    document.at("runs").as_array()[1].as_object()["id"] = "first";
    EXPECT_ANY_THROW(sv::research::run_batch(document, root, root / "duplicate", {}, &cancel));
    EXPECT_FALSE(std::filesystem::exists(root / "duplicate"));
    document["oversize"] = std::string(65536, 'x');
    EXPECT_ANY_THROW(sv::research::run_batch(document, root, root / "oversize", {}, &cancel));
    EXPECT_FALSE(std::filesystem::exists(root / "oversize"));
}

TEST_F(ResearchBatch, PlanStructureCanBeImportedWithoutOpeningScenarioOrReference)
{
    const auto text = boost::json::serialize(plan()) + "\n  ";
    EXPECT_EQ(sv::research::parse_batch_plan(text), plan());
    std::filesystem::remove(root / "scenario.json");
    EXPECT_EQ(sv::research::parse_batch_plan(text), plan());
    write("import.json", text);
    EXPECT_EQ(sv::research::read_batch_plan(root / "import.json"), text);
    EXPECT_ANY_THROW(sv::research::parse_batch_plan(""));
    EXPECT_ANY_THROW(sv::research::parse_batch_plan(std::string(65537, ' ')));
    auto duplicate = plan();
    duplicate.at("runs").as_array()[1].as_object()["id"] = "first";
    EXPECT_ANY_THROW(sv::research::parse_batch_plan(boost::json::serialize(duplicate)));
}

TEST_F(ResearchBatch, BoundedPlanReadRejectsNonRegularAndOversizeInputs)
{
    EXPECT_ANY_THROW(sv::research::read_batch_plan(root));
    ASSERT_EQ(::mkfifo((root / "pipe.json").c_str(), 0600), 0);
    EXPECT_ANY_THROW(sv::research::read_batch_plan(root / "pipe.json"));
    write("huge.json", std::string(65537, ' '));
    EXPECT_ANY_THROW(sv::research::read_batch_plan(root / "huge.json"));
    write("empty.json", "");
    EXPECT_ANY_THROW(sv::research::read_batch_plan(root / "empty.json"));
    EXPECT_ANY_THROW(sv::research::read_batch_plan(root / "missing.json"));
}

TEST_F(ResearchBatch, DraftSaveValidatesBeforeCreationAndHashesExactWrittenBytes)
{
    const auto document = plan();
    std::filesystem::remove(root / "scenario.json"); // drafts do not read scenarios
    const auto digest = sv::research::write_batch_plan(document, root / "draft.json");
    const auto text = sv::research::read_batch_plan(root / "draft.json");
    EXPECT_EQ(text, boost::json::serialize(document) + '\n');
    EXPECT_EQ(digest, sv::research::sha256(text.data(), text.size()));
    EXPECT_EQ(sv::research::parse_batch_plan(text), document);
    auto invalid = document;
    invalid.at("runs").as_array()[1].as_object()["id"] = "first";
    EXPECT_ANY_THROW(sv::research::write_batch_plan(invalid, root / "invalid.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "invalid.json"));
    EXPECT_ANY_THROW(sv::research::write_batch_plan(document, root / "draft.json"));
    EXPECT_EQ(sv::research::read_batch_plan(root / "draft.json"), text);
}

TEST_F(ResearchBatch, DraftSaveNeverReplacesHardLinksOrSymlinksAndRequiresExistingParent)
{
    write("existing.json", "keep-original");
    std::filesystem::create_hard_link(root / "existing.json", root / "hard.json");
    std::filesystem::create_symlink("existing.json", root / "linked.json");
    std::filesystem::create_symlink("missing.json", root / "dangling.json");
    for (const auto *name : {"existing.json", "hard.json", "linked.json", "dangling.json"})
    {
        EXPECT_ANY_THROW(sv::research::write_batch_plan(plan(), root / name));
    }
    EXPECT_EQ(sv::research::read_batch_plan(root / "existing.json"), "keep-original");
    EXPECT_TRUE(std::filesystem::is_symlink(root / "dangling.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "missing.json"));
    EXPECT_ANY_THROW(sv::research::write_batch_plan(plan(), root / "absent-parent/plan.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "absent-parent"));
}
