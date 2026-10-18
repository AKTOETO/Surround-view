#include "batch_plan.hpp"
#include "digest.hpp"
#include "research_batch_draft.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include "research_results.hpp"
#include "scenario.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
std::optional<sv::client::Options> live;

QVariantMap draft()
{
    return {{"modes", QStringList{"multi_band"}},
            {"frames", 2},
            {"warmup", 1},
            {"repeats", 3},
            {"seed", 17},
            {"boundary", "both"},
            {"pyramid_levels", 3},
            {"seam_solver", "binary_pairs"}};
}

bool waitFor(std::function<bool()> ready, int timeout = 12000)
{
    QElapsedTimer timer;
    timer.start();
    while (!ready() && timer.elapsed() < timeout)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return ready();
}

boost::json::object report(const std::filesystem::path &path)
{
    std::ifstream input(path);
    return boost::json::parse(input).as_object();
}
} // namespace

TEST(ResearchGuiDraft, ExpandsOnlyRelevantAxesAndEnforcesReplayCapture)
{
    auto values = draft();
    values["modes"] = QStringList{"edge_feather",          "hard_best_angle", "angular_feather",
                                  "seam_distance_feather", "graph_cut_seam",  "multi_band",
                                  "graph_cut_multi_band"};
    values["seam_solver"] = "alpha_expansion";
    const auto document = researchDraft(values);
    const auto scenario = sv::research::parse_scenario(document);
    ASSERT_EQ(scenario.variants.size(), 9U);
    EXPECT_TRUE(scenario.reset_replay);
    EXPECT_TRUE(scenario.capture_frames);
    EXPECT_FALSE(scenario.quality_reference);
    const auto &variants = document.at("variants").as_array();
    EXPECT_FALSE(variants[0].as_object().contains("pyramid_boundary"));
    EXPECT_FALSE(variants[0].as_object().contains("pyramid_levels"));
    EXPECT_FALSE(variants[0].as_object().contains("seam_solver"));
    EXPECT_EQ(variants[4].as_object().at("seam_solver"), "alpha_expansion");
    EXPECT_EQ(variants[5].as_object().at("pyramid_boundary"), "zero");
    EXPECT_EQ(variants[6].as_object().at("pyramid_boundary"), "normalized");
}

TEST(ResearchGuiDraft, RejectsInvalidTypesUnknownMethodsDuplicatesAndTrialBudget)
{
    for (const auto &change :
         std::vector<QVariantMap>{{{"frames", true}},
                                  {{"frames", 1.5}},
                                  {{"seed", "17"}},
                                  {{"seed", -1}},
                                  {{"repeats", 0}},
                                  {{"pyramid_levels", true}},
                                  {{"pyramid_levels", 9}},
                                  {{"boundary", "invalid"}},
                                  {{"seam_solver", "invalid"}},
                                  {{"modes", QStringList{}}},
                                  {{"modes", QStringList{"multi_band", "multi_band"}}},
                                  {{"modes", QStringList{"not_supported"}}},
                                  {{"unknown", 1}},
                                  {{"frames", 256}, {"repeats", 1000}}})
    {
        auto values = draft();
        for (auto it = change.begin(); it != change.end(); ++it)
        {
            values[it.key()] = it.value();
        }
        EXPECT_ANY_THROW(researchDraft(values));
    }
}

TEST(ResearchGuiUnit, DisconnectedOrInvalidDraftDoesNotCreateOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/result";
    ResearchController controller;
    EXPECT_FALSE(controller.startDraft(draft(), output));
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    controller.setConnection(sv::client::Options{});
    auto invalid = draft();
    invalid["frames"] = false;
    EXPECT_FALSE(controller.startDraft(invalid, output));
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
}

TEST(ResearchGuiUnit, WorkerRejectsExistingDirectoryWithoutOverwritingOrConnecting)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    std::ofstream(root / "keep.txt") << "original";
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    ASSERT_TRUE(controller.startDraft(draft(), directory.path()));
    EXPECT_FALSE(controller.startDraft(draft(), directory.path() + "/second"));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(controller.rows().isEmpty());
    std::ifstream input(root / "keep.txt");
    std::string contents;
    input >> contents;
    EXPECT_EQ(contents, "original");
    EXPECT_FALSE(std::filesystem::exists(root / "scenario.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "second"));
}

TEST(ResearchGuiUnit, RowsPreserveMeasuredZeroAndUnavailableMetrics)
{
    const auto document = boost::json::parse(R"({
      "scenario":{"variants":[{"mode":"multi_band","pyramid_boundary":"normalized"},
                                {"mode":"edge_feather"}]},
      "summaries":[{"timings":{"render_wall":{"count":3,"p50_ms":0,"p95_ms":0.125}}},
                   {"timings":{"render_wall":null}}],
      "quality_summaries":[{"pixel_weighted_mae_rgb8":0}]
    })")
                              .as_object();
    const auto rows = researchResultRows(document);
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0].toMap().value("p50").toString(), "0.000");
    EXPECT_EQ(rows[0].toMap().value("p95").toString(), "0.125");
    EXPECT_EQ(rows[0].toMap().value("mae").toString(), "0.000");
    EXPECT_EQ(rows[0].toMap().value("measured").toInt(), 3);
    EXPECT_EQ(rows[1].toMap().value("p50").toString(), QString::fromUtf8("—"));
    EXPECT_EQ(rows[1].toMap().value("mae").toString(), QString::fromUtf8("—"));
}

TEST(ResearchGuiUnit, UrlAndReferencePreflightFailuresLeaveNoOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    const auto output = directory.path() + "/preflight";
    for (const auto &url :
         {QUrl("https://example.invalid/scenario.json"), QUrl("file:///tmp/scene.json?query"),
          QUrl("file:///tmp/scene.json#fragment")})
    {
        EXPECT_FALSE(controller.startScenario(url, output));
        EXPECT_EQ(controller.qualityStatus(), "not_available");
        EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    }
    ASSERT_TRUE(controller.startDraftWithReference(
        draft(), output, QUrl::fromLocalFile(directory.path() + "/missing-reference.json")));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_EQ(controller.qualityStatus(), "not_available");
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    EXPECT_TRUE(controller.reportPath().isEmpty());
    const auto input = directory.path() + "/scenario with # and %.json";
    std::ofstream(input.toStdString()) << "invalid-json";
    ASSERT_TRUE(controller.startScenario(QUrl::fromLocalFile(input), output));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
}

TEST(ResearchGuiUnit, SmallQualityErrorsNeverDisplayAsExactZero)
{
    auto data = boost::json::parse(R"({
      "scenario":{"variants":[{"mode":"multi_band"}]},"summaries":[],
      "quality_summaries":[{"frames":1,"pixel_weighted_mae_rgb8":0.000001,
        "pixel_weighted_rmse_rgb8":0.000002,"pixel_weighted_psnr_db":162.1,"exact_match":false}]
    })")
                    .as_object();
    const auto row = researchResultRows(data)[0].toMap();
    EXPECT_NE(row.value("mae").toString(), "0.000");
    EXPECT_NEAR(row.value("mae").toString().toDouble(), .000001, 1e-15);
    EXPECT_EQ(row.value("psnr").toString(), "162.100");
    data.at("quality_summaries").as_array()[0].as_object()["exact_match"] = true;
    data.at("quality_summaries").as_array()[0].as_object()["pixel_weighted_mae_rgb8"] = 0;
    data.at("quality_summaries").as_array()[0].as_object()["pixel_weighted_rmse_rgb8"] = 0;
    data.at("quality_summaries").as_array()[0].as_object()["pixel_weighted_psnr_db"] = nullptr;
    EXPECT_TRUE(researchResultRows(data)[0].toMap().value("psnr").toString().startsWith(
        QString::fromUtf8("∞")));
}

TEST(ResearchGuiUnit, BatchPreflightsLateMembersAndRejectsRemoteUrls)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    std::ofstream(root / "scenario.json") << boost::json::serialize(researchDraft(draft()));
    std::ofstream(root / "plan.json") << R"({"schema_version":1,"runs":[
      {"id":"first","scenario":"scenario.json"},{"id":"second","scenario":"missing.json"}]})";
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    const auto output = directory.path() + "/output";
    EXPECT_FALSE(controller.startBatch(QUrl("https://example.invalid/plan.json"), output));
    ASSERT_TRUE(
        controller.startBatch(QUrl::fromLocalFile(directory.path() + "/plan.json"), output));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_TRUE(controller.batchMode());
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.batchRows().isEmpty());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(root / "output"));
}

TEST(ResearchGuiUnit, BatchRowsNeverTreatMissingEvidenceAsSuccess)
{
    const auto rows = researchBatchRows(boost::json::parse(R"({"runs":[
      {"id":"a","status":"reported","success":true,"restored":true,"quality_status":"failed","report":"a.json"},
      {"id":"b","status":"not_started","report":"b.json"},
      {"id":"c","status":"error","error":"disk","report":"c.json"}]})")
                                            .as_object());
    ASSERT_EQ(rows.size(), 3);
    EXPECT_EQ(rows[0].toMap().value("quality").toString(), "failed");
    EXPECT_EQ(rows[0].toMap().value("workflow").toString(), "завершён");
    EXPECT_EQ(rows[1].toMap().value("status").toString(), "not_started");
    EXPECT_EQ(rows[1].toMap().value("quality").toString(), "not_available");
    EXPECT_EQ(rows[1].toMap().value("restore").toString(), "не подтверждено");
    EXPECT_EQ(rows[2].toMap().value("error").toString(), "disk");
}

TEST(ResearchGuiDraft, BatchCompositionPreservesOrderAndLiteralLocalPaths)
{
    const auto url = QUrl::fromLocalFile("/tmp/scenario with # and %.json");
    const QVariantMap first{{"id", "first"}, {"scenario", url}};
    const QVariantMap second{{"id", "second"}, {"scenario", url.toString()}};
    const auto plan = researchBatchDraft({second, first});
    EXPECT_EQ(plan.at("runs").as_array()[0].as_object().at("id"), "second");
    EXPECT_EQ(plan.at("runs").as_array()[1].as_object().at("scenario"),
              "/tmp/scenario with # and %.json");
    EXPECT_ANY_THROW(researchBatchDraft({}));
    EXPECT_ANY_THROW(researchBatchDraft({true}));
    EXPECT_ANY_THROW(researchBatchDraft({QVariantMap{{"id", true}, {"scenario", url}}}));
    EXPECT_ANY_THROW(researchBatchDraft({QVariantMap{{"id", "a"}, {"scenario", true}}}));
    EXPECT_ANY_THROW(
        researchBatchDraft({QVariantMap{{"id", "a"}, {"scenario", url}, {"unknown", 0}}}));
    for (const auto &bad :
         {QUrl("https://example.invalid/scenario.json"), QUrl("file:///tmp/scenario.json?query"),
          QUrl("file:///tmp/scenario.json#fragment")})
    {
        EXPECT_ANY_THROW(researchBatchDraft({QVariantMap{{"id", "a"}, {"scenario", bad}}}));
    }
    QVariantList over;
    for (unsigned i = 0; i < 9; ++i)
    {
        over.push_back(first);
    }
    EXPECT_ANY_THROW(researchBatchDraft(over));
}

TEST(ResearchGuiDraft, ImportedBatchRetainsIdsPathsAndDoesNotReadScenarios)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    std::filesystem::create_directory(root / "plans");
    const auto source = root / "plans/batch # %.json";
    std::ofstream(source) << R"({"schema_version":1,"runs":[
        {"id":"case-1","scenario":"../missing # %.json"},
        {"id":"repeat","scenario":"../missing # %.json"}]})";
    const auto members =
        researchImportBatch(QUrl::fromLocalFile(QString::fromStdString(source.string())));
    ASSERT_EQ(members.size(), 2);
    EXPECT_EQ(members[0].toMap().value("id").toString(), "case-1");
    EXPECT_EQ(members[1].toMap().value("id").toString(), "repeat");
    const auto plan = researchBatchDraft(members);
    const auto selected = std::filesystem::path(
        std::string(plan.at("runs").as_array()[0].as_object().at("scenario").as_string()));
    EXPECT_EQ(selected.lexically_normal(), root / "missing # %.json");
    EXPECT_FALSE(std::filesystem::exists(root / "missing # %.json"));
    EXPECT_ANY_THROW(researchImportBatch(QUrl("https://example.invalid/plan.json")));
    // A symlinked batch is resolved from the selected batch directory, matching file run_batch.
    std::filesystem::create_symlink(source, root / "link.json");
    const auto linked = researchImportBatch(QUrl::fromLocalFile(directory.path() + "/link.json"));
    EXPECT_EQ(linked[0].toMap().value("scenario").toUrl().toLocalFile(),
              QString::fromStdString((root / "../missing # %.json").string()));
}

TEST(ResearchGuiDraft, ImportedDotDotAcrossSymlinkKeepsFilesystemMeaning)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    std::filesystem::create_directories(root / "real/deep");
    std::filesystem::create_directory_symlink(root / "real/deep", root / "link");
    std::ofstream(root / "real/scenario.json") << "correct";
    std::ofstream(root / "scenario.json") << "different";
    std::ofstream(root / "plan.json") << R"({"schema_version":1,"runs":[
        {"id":"a","scenario":"link/../scenario.json"}]})";
    const auto rows = researchImportBatch(QUrl::fromLocalFile(directory.path() + "/plan.json"));
    const auto plan = researchBatchDraft(rows);
    EXPECT_EQ(std::filesystem::canonical(std::string(
                  plan.at("runs").as_array()[0].as_object().at("scenario").as_string())),
              root / "real/scenario.json");
    ResearchController controller;
    const auto target = QUrl::fromLocalFile(directory.path() + "/link/../saved.json");
    ASSERT_TRUE(controller.saveBatch(rows, target).value("success").toBool());
    EXPECT_TRUE(std::filesystem::exists(root / "real/saved.json"));
    EXPECT_FALSE(std::filesystem::exists(root / "saved.json"));
    const auto restored = controller.importBatch(target);
    ASSERT_TRUE(restored.value("success").toBool());
    const auto savedPlan = researchBatchDraft(restored.value("members").toList());
    EXPECT_EQ(std::filesystem::canonical(std::string(
                  savedPlan.at("runs").as_array()[0].as_object().at("scenario").as_string())),
              root / "real/scenario.json");
}

TEST(ResearchGuiUnit, ImportWithoutEndpointDoesNotMutateRunResultsAndBusyRejectsIt)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = directory.path() + "/plan.json";
    std::ofstream(source.toStdString())
        << R"({"schema_version":1,"runs":[{"id":"a","scenario":"missing.json"}]})";
    ResearchController controller;
    const auto imported = controller.importBatch(QUrl::fromLocalFile(source));
    EXPECT_TRUE(imported.value("success").toBool());
    EXPECT_FALSE(controller.busy());
    EXPECT_EQ(controller.exitCode(), -1);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    controller.setConnection(sv::client::Options{});
    ASSERT_TRUE(controller.startDraft(draft(), directory.path()));
    EXPECT_FALSE(controller.importBatch(QUrl::fromLocalFile(source)).value("success").toBool());
    const QVariantList saveMembers{
        QVariantMap{{"id", "a"}, {"scenario", QUrl::fromLocalFile(source)}}};
    EXPECT_FALSE(controller.saveBatch(saveMembers, QUrl::fromLocalFile(source + "-save"))
                     .value("success")
                     .toBool());
    EXPECT_FALSE(std::filesystem::exists((source + "-save").toStdString()));
    EXPECT_TRUE(controller.busy());
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 3);
}

TEST(ResearchGuiUnit, SavesDraftOfflineWithoutPreflightAndCanImportFromAnotherDirectory)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    std::filesystem::create_directories(root / "plans/nested");
    const auto source = QUrl::fromLocalFile(directory.path() + "/missing # %.json");
    const QVariantList members{QVariantMap{{"id", "repeat"}, {"scenario", source}},
                               QVariantMap{{"id", "first"}, {"scenario", source}}};
    ResearchController controller;
    const auto target = QUrl::fromLocalFile(directory.path() + "/plans/draft # %.json");
    const auto result = controller.saveBatch(members, target);
    ASSERT_TRUE(result.value("success").toBool())
        << result.value("message").toString().toStdString();
    EXPECT_FALSE(controller.available());
    EXPECT_FALSE(controller.busy());
    EXPECT_EQ(controller.exitCode(), -1);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_EQ(controller.qualityStatus(), "not_available");
    const auto bytes = sv::research::read_batch_plan(root / "plans/draft # %.json");
    EXPECT_EQ(result.value("sha256").toString().toStdString(),
              sv::research::sha256(bytes.data(), bytes.size()));
    std::filesystem::rename(root / "plans/draft # %.json", root / "plans/nested/moved.json");
    const auto restored =
        controller.importBatch(QUrl::fromLocalFile(directory.path() + "/plans/nested/moved.json"));
    ASSERT_TRUE(restored.value("success").toBool());
    EXPECT_EQ(researchBatchDraft(restored.value("members").toList()), researchBatchDraft(members));
    EXPECT_FALSE(controller.saveBatch({}, target).value("success").toBool());
    EXPECT_FALSE(std::filesystem::exists(root / "plans/draft # %.json"));
    auto duplicate = members;
    duplicate[1] = duplicate[0];
    EXPECT_FALSE(controller.saveBatch(duplicate, target).value("success").toBool());
    EXPECT_FALSE(std::filesystem::exists(root / "plans/draft # %.json"));
    EXPECT_FALSE(controller.saveBatch(members, QUrl("https://example.invalid/draft.json"))
                     .value("success")
                     .toBool());
}

TEST(ResearchGuiUnit, ComposedBatchLateFailureDoesNotCreateOutput)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto source = directory.path() + "/scenario.json";
    std::ofstream(source.toStdString()) << boost::json::serialize(researchDraft(draft()));
    const QVariantList members{
        QVariantMap{{"id", "first"}, {"scenario", QUrl::fromLocalFile(source)}},
        QVariantMap{{"id", "second"}, {"scenario", QUrl::fromLocalFile(source + "-absent")}}};
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    ASSERT_TRUE(controller.startBatchDraft(members, directory.path() + "/output"));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.batchRows().isEmpty());
    EXPECT_FALSE(std::filesystem::exists((directory.path() + "/output").toStdString()));
}

TEST(ResearchGuiLive, BatchCompletionAndCancellationShareTheNativeRunner)
{
    ASSERT_TRUE(live);
    for (const bool cancel : {false, true})
    {
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const std::filesystem::path root(directory.path().toStdString());
        auto values = draft();
        values["repeats"] = cancel ? 100 : 1;
        std::ofstream(root / "scenario.json") << boost::json::serialize(researchDraft(values));
        std::ofstream(root / "plan.json") << R"({"schema_version":1,"runs":[
          {"id":"first","scenario":"scenario.json"},{"id":"second","scenario":"scenario.json"}]})";
        ResearchController controller;
        controller.setConnection(live);
        const QVariantList members{
            QVariantMap{{"id", "first"},
                        {"scenario", QUrl::fromLocalFile(directory.path() + "/scenario.json")}},
            QVariantMap{{"id", "second"},
                        {"scenario", QUrl::fromLocalFile(directory.path() + "/scenario.json")}}};
        ASSERT_TRUE(
            cancel ? controller.startBatchDraft(members, directory.path() + "/output")
                   : controller.startBatch(QUrl::fromLocalFile(directory.path() + "/plan.json"),
                                           directory.path() + "/output"));
        controller.setConnection(std::nullopt);
        if (cancel)
        {
            ASSERT_TRUE(waitFor([&] { return controller.completed() > 0 || !controller.busy(); }));
            ASSERT_TRUE(controller.busy());
            EXPECT_TRUE(controller.status().contains("Сценарий №1"));
            EXPECT_FALSE(controller.status().contains("Все trials"));
            controller.cancel();
        }
        ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
        EXPECT_TRUE(controller.batchMode());
        EXPECT_TRUE(controller.rows().isEmpty());
        ASSERT_EQ(controller.batchRows().size(), 2);
        const auto summary = report(controller.reportPath().toStdString());
        EXPECT_TRUE(summary.at("restored").as_bool());
        EXPECT_EQ(controller.exitCode(), cancel ? 4 : 0);
        EXPECT_EQ(controller.qualityStatus(), cancel ? "incomplete" : "not_requested");
        EXPECT_EQ(summary.at("completed").to_number<unsigned>(), cancel ? 0U : 2U);
        EXPECT_EQ(controller.batchRows()[1].toMap().value("status").toString(),
                  cancel ? "not_started" : "reported");
    }
}

TEST(ResearchGuiLive, CompletesWithResponsiveUiAndFrozenEndpoint)
{
    if (!live)
    {
        GTEST_SKIP() << "Executed by client_transports with a live endpoint";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/result";
    ResearchController controller;
    controller.setConnection(live);
    unsigned ticks = 0;
    QTimer heartbeat;
    QObject::connect(&heartbeat, &QTimer::timeout, [&] { ++ticks; });
    heartbeat.start(1);
    ASSERT_TRUE(controller.startDraft(draft(), output));
    controller.setConnection(std::nullopt); // changes availability, not the frozen worker request
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_GT(ticks, 2U);
    EXPECT_FALSE(controller.available());
    EXPECT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_EQ(controller.completed(), controller.total());
    EXPECT_EQ(controller.total(), 16U);
    ASSERT_EQ(controller.rows().size(), 2);
    EXPECT_EQ(controller.rows()[0].toMap().value("measured").toInt(), 6);
    EXPECT_EQ(controller.rows()[0].toMap().value("mae").toString(), QString::fromUtf8("—"));
    const auto data = report(controller.reportPath().toStdString());
    EXPECT_TRUE(data.at("success").as_bool());
    EXPECT_TRUE(data.at("restored").as_bool());
    EXPECT_FALSE(data.contains("quality_acceptance"));
    EXPECT_EQ(data.at("samples").as_array().size(), 16U);
}

TEST(ResearchGuiLive, CancelRestoresAndPublishesPartialReport)
{
    if (!live)
    {
        GTEST_SKIP() << "Executed by client_transports with a live endpoint";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    ResearchController controller;
    controller.setConnection(live);
    auto values = draft();
    values["warmup"] = 0;
    values["repeats"] = 100;
    ASSERT_TRUE(controller.startDraft(values, directory.path() + "/cancel"));
    ASSERT_TRUE(waitFor([&] { return controller.completed() > 0 || !controller.busy(); }));
    ASSERT_TRUE(controller.busy()) << controller.status().toStdString();
    controller.cancel();
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.exitCode(), 4);
    const auto data = report(controller.reportPath().toStdString());
    EXPECT_FALSE(data.at("success").as_bool());
    EXPECT_TRUE(data.at("restored").as_bool());
    EXPECT_LT(data.at("samples").as_array().size(), 400U);
    EXPECT_EQ(data.at("error"), "cancelled");
}

TEST(ResearchGuiLive, DestructorCancelsJoinsAndLeavesNoQueuedUseAfterFree)
{
    if (!live)
    {
        GTEST_SKIP() << "Executed by client_transports with a live endpoint";
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/shutdown";
    {
        ResearchController controller;
        controller.setConnection(live);
        auto values = draft();
        values["repeats"] = 100;
        ASSERT_TRUE(controller.startDraft(values, output));
        ASSERT_TRUE(waitFor([&] { return controller.completed() > 0 || !controller.busy(); }));
        ASSERT_TRUE(controller.busy());
    }
    QCoreApplication::processEvents();
    const auto data = report(std::filesystem::path(output.toStdString()) / "report.json");
    EXPECT_FALSE(data.at("success").as_bool());
    EXPECT_TRUE(data.at("restored").as_bool());
    EXPECT_EQ(data.at("error"), "cancelled");
}

TEST(ResearchGuiReferenceLive, ImportsAndDraftsShareGuardedQualityAndAcceptance)
{
    ASSERT_TRUE(live);
    const auto source = qEnvironmentVariable("SV_RESEARCH_GUI_SCENARIO");
    ASSERT_FALSE(source.isEmpty());
    const auto expected = qEnvironmentVariable("SV_RESEARCH_GUI_QUALITY");
    const auto scenario = report(source.toStdString());
    const auto &variant = scenario.at("variants").as_array()[0].as_object();
    auto values = draft();
    values["modes"] =
        QStringList{QString::fromStdString(std::string(variant.at("mode").as_string()))};
    values["boundary"] =
        QString::fromStdString(std::string(variant.at("pyramid_boundary").as_string()));
    values["pyramid_levels"] = variant.at("pyramid_levels").to_number<int>();
    values["seam_solver"] = "alpha_expansion";
    values["frames"] = 3;
    values["warmup"] = 1;
    values["repeats"] = 2;
    const auto reference = std::filesystem::path(source.toStdString()).parent_path() /
                           std::string(scenario.at("quality_reference").as_string());
    for (const bool imported : {true, false})
    {
        QTemporaryDir directory;
        ASSERT_TRUE(directory.isValid());
        const auto output = directory.path() + "/quality";
        ResearchController controller;
        controller.setConnection(live);
        ASSERT_TRUE(imported
                        ? controller.startScenario(QUrl::fromLocalFile(source), output)
                        : controller.startDraftWithReference(
                              values, output,
                              QUrl::fromLocalFile(QString::fromStdString(reference.string()))));
        ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
        EXPECT_EQ(controller.qualityStatus(), expected) << controller.status().toStdString();
        EXPECT_TRUE(controller.referenceInfo().contains("regression"));
        EXPECT_TRUE(controller.referenceInfo().contains("transport-fixture"));
        const int code = expected == "passed" || expected == "not_requested" ? 0
                         : expected == "failed"                              ? 5
                                                                             : 4;
        EXPECT_EQ(controller.exitCode(), code);
        const auto data = report(controller.reportPath().toStdString());
        EXPECT_TRUE(data.at("restored").as_bool());
        EXPECT_EQ(data.at("success").as_bool(), expected != "incomplete");
        EXPECT_EQ(std::string(data.at("scenario_input_kind").as_string()),
                  imported ? "file_bytes_v1" : "memory_json_bytes_v1");
        if (expected == "passed" || expected == "not_requested")
        {
            ASSERT_FALSE(controller.rows().isEmpty());
            EXPECT_EQ(controller.rows()[0].toMap().value("mae").toString(), "0.000");
            EXPECT_TRUE(controller.rows()[0].toMap().value("psnr").toString().startsWith(
                QString::fromUtf8("∞")));
        }
        if (expected == "not_requested")
        {
            EXPECT_TRUE(controller.status().contains("метрики рассчитаны"));
        }
        if (expected == "failed")
        {
            ASSERT_FALSE(controller.rows().isEmpty());
            EXPECT_GT(controller.rows()[0].toMap().value("mae").toString().toDouble(), 0);
            EXPECT_FALSE(controller.rows()[0].toMap().value("psnr").toString().contains(
                QString::fromUtf8("∞")));
        }
        if (!imported)
        {
            const auto snapshot =
                report(std::filesystem::path(output.toStdString()) / "scenario.json");
            EXPECT_TRUE(
                std::filesystem::path(std::string(snapshot.at("quality_reference").as_string()))
                    .is_absolute());
        }
        // A rejected new request must not retain an earlier passed/failed badge or report rows.
        EXPECT_FALSE(controller.startScenario(QUrl("https://example.invalid/scenario.json"),
                                              output + "-invalid"));
        EXPECT_EQ(controller.qualityStatus(), "not_available");
        EXPECT_TRUE(controller.rows().isEmpty());
        EXPECT_TRUE(controller.reportPath().isEmpty());
    }
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const std::filesystem::path root(directory.path().toStdString());
    ResearchController controller;
    controller.setConnection(live);
    const QVariantList members{
        QVariantMap{{"id", "first"}, {"scenario", QUrl::fromLocalFile(source)}},
        QVariantMap{{"id", "second"}, {"scenario", QUrl::fromLocalFile(source)}}};
    ASSERT_TRUE(controller.startBatchDraft(members, directory.path() + "/batch"));
    ASSERT_TRUE(waitFor([&] { return !controller.busy(); }));
    EXPECT_EQ(controller.qualityStatus(), expected);
    EXPECT_EQ(controller.exitCode(), expected == "failed" ? 5 : expected == "incomplete" ? 4 : 0);
    const auto summary = report(controller.reportPath().toStdString());
    EXPECT_EQ(summary.at("plan_snapshot"), "inputs/plan.json");
    EXPECT_EQ(report(root / "batch/inputs/plan.json"), summary.at("plan"));
    EXPECT_TRUE(summary.at("restored").as_bool());
    ASSERT_EQ(controller.batchRows().size(), 2);
    EXPECT_EQ(summary.at("reported").to_number<unsigned>(), expected == "incomplete" ? 1U : 2U);
    EXPECT_EQ(controller.batchRows()[1].toMap().value("status").toString(),
              expected == "incomplete" ? "not_started" : "reported");
    EXPECT_EQ(controller.batchRows()[0].toMap().value("quality").toString(), expected);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 3 && std::string(argv[1]) == "--unix")
    {
        live.emplace();
        live->endpoint.directory = argv[2];
    }
    else if (argc == 5 && std::string(argv[1]) == "--tcp")
    {
        live.emplace();
        live->endpoint.transport = sv::client::Endpoint::Transport::Tcp;
        live->endpoint.host = argv[2];
        live->endpoint.control_port = uint16_t(std::stoul(argv[3]));
        live->endpoint.data_port = uint16_t(std::stoul(argv[4]));
    }
    if (live)
    {
        live->timeout_ms = 3000;
    }
    argc = 1;
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::GTEST_FLAG(filter) =
        live ? (qEnvironmentVariableIsSet("SV_RESEARCH_GUI_SCENARIO") ? "ResearchGuiReferenceLive.*"
                                                                      : "ResearchGuiLive.*")
             : "ResearchGuiDraft.*:ResearchGuiUnit.*";
    return RUN_ALL_TESTS();
}
