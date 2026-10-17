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
        live ? "ResearchGuiLive.*" : "ResearchGuiDraft.*:ResearchGuiUnit.*";
    return RUN_ALL_TESTS();
}
