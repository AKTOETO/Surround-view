#include "digest.hpp"
#include "regular_input.hpp"
#include "research_batch_draft.hpp"
#include "research_gui_bundle_fixture.hpp"
#include "sv/json.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(ResearchGuiUnit, BundleExportVerifyAreOfflineExclusiveAndKeepLiteralPaths)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto source = QUrl::fromLocalFile(gui_bundle::scenario(temporary.path()));
    const auto parent = QUrl::fromLocalFile(temporary.path());
    const auto folder = QUrl::fromLocalFile(temporary.path() + "/bundle # %");
    ResearchController controller;
    EXPECT_FALSE(controller.available());
    ASSERT_TRUE(controller.exportBundle(source, parent, "bundle # %"));
    EXPECT_TRUE(controller.busy());
    EXPECT_FALSE(controller.verifyBundle(folder));
    EXPECT_FALSE(controller.exportBundle(source, parent, "second"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(controller.rows().isEmpty());
    EXPECT_EQ(controller.qualityStatus(), "not_available");
    EXPECT_TRUE(controller.bundleInfo().value("verified").toBool());
    EXPECT_EQ(controller.bundleInfo().value("files").toInt(), 1);
    const auto digest = controller.bundleInfo().value("manifest_sha256");
    EXPECT_EQ(digest.toString().size(), 64);
    EXPECT_EQ(controller.bundleInfo().value("directory").toString(), folder.toLocalFile());
    ASSERT_TRUE(controller.verifyBundle(folder));
    EXPECT_TRUE(controller.bundleInfo().isEmpty()); // old receipt is not current evidence
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 0);
    EXPECT_EQ(controller.bundleInfo().value("manifest_sha256"), digest);
    EXPECT_EQ(controller.bundleInfo().value("trials").toInt(), 1);
    EXPECT_FALSE(controller.bundleInfo().value("server_recording_included").toBool());
    ASSERT_TRUE(controller.exportBundle(source, parent, "bundle # %"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    EXPECT_FALSE(std::filesystem::exists((temporary.path() + "/second").toStdString()));
    ASSERT_TRUE(controller.verifyBundle(folder));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.bundleInfo().value("manifest_sha256"), digest); // no overwrite
}

TEST(ResearchGuiUnit, BundleRejectsRemoteUrlsNamesAndTamperingBeforeRunOutput)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    const auto source = QUrl::fromLocalFile(gui_bundle::scenario(temporary.path()));
    const auto parent = QUrl::fromLocalFile(temporary.path());
    for (const auto &name : {"", " ", ".", "..", "../escape", "a/b", "a\\b"})
    {
        EXPECT_FALSE(controller.exportBundle(source, parent, name));
        EXPECT_FALSE(controller.busy());
    }
    EXPECT_FALSE(controller.verifyBundle(QUrl("https://example.org/bundle")));
    EXPECT_FALSE(controller.exportBundle(source, QUrl("https://example.org"), "new"));
    ASSERT_TRUE(controller.exportBundle(source, parent, "bundle"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0);
    const auto folder = QUrl::fromLocalFile(temporary.path() + "/bundle");
    const auto output = temporary.path() + "/result";
    EXPECT_FALSE(controller.startBundle(folder, output)); // run needs endpoint
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    ASSERT_TRUE(controller.verifyBundle(folder));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0);
    std::ofstream((temporary.path() + "/bundle/scenario.json").toStdString(), std::ios::app) << ' ';
    sv::client::Options connection;
    connection.endpoint.directory = (temporary.path() + "/no-server").toStdString();
    controller.setConnection(connection);
    ASSERT_TRUE(controller.startBundle(folder, output));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
}

TEST(ResearchGuiUnit, BundleWorkerCanBeCancelledAndDestroyedWithoutEndpoint)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto source = QUrl::fromLocalFile(gui_bundle::scenario(temporary.path()));
    const auto parent = QUrl::fromLocalFile(temporary.path());
    {
        ResearchController controller;
        ASSERT_TRUE(controller.exportBundle(source, parent, "cancelled"));
        controller.cancel();
        ASSERT_TRUE(gui_bundle::wait(controller));
        EXPECT_TRUE(controller.exitCode() == 0 || controller.exitCode() == 3);
        EXPECT_TRUE(controller.reportPath().isEmpty());
    }
    {
        ResearchController controller;
        ASSERT_TRUE(controller.exportBundle(source, parent, "destroyed"));
        // Destruction cancels and joins the worker; queued callbacks target this QObject.
    }
    QCoreApplication::processEvents();
}

TEST(ResearchGuiUnit, BatchBundleOfflinePreflightWrongTypeAndLateTamperLeaveNoRunOutput)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    const auto scenario = gui_bundle::scenario(temporary.path());
    const auto input = QUrl::fromLocalFile(gui_bundle::plan(temporary.path(), scenario));
    const auto parent = QUrl::fromLocalFile(temporary.path());
    const auto folder = QUrl::fromLocalFile(temporary.path() + "/batch # %");
    ASSERT_TRUE(controller.exportBundle(input, parent, "batch # %", true));
    EXPECT_FALSE(controller.verifyBundle(folder, true)); // same shared busy gate
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_EQ(controller.bundleInfo().value("policy").toString(), "batch_bundle_v1");
    EXPECT_EQ(controller.bundleInfo().value("runs").toList().size(), 2);
    EXPECT_EQ(controller.bundleInfo().value("trials").toInt(), 2);
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(controller.batchRows().isEmpty());
    ASSERT_TRUE(controller.verifyBundle(folder, false)); // wrong explicit type must fail
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    ASSERT_TRUE(controller.verifyBundle(folder, true));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 0);
    const auto output = temporary.path() + "/result";
    EXPECT_FALSE(controller.startBundle(folder, output, true)); // needs endpoint
    std::ofstream((temporary.path() + "/batch # %/run-1/scenario.json").toStdString(),
                  std::ios::app)
        << ' ';
    sv::client::Options connection;
    connection.endpoint.directory = (temporary.path() + "/no-server").toStdString();
    controller.setConnection(connection);
    ASSERT_TRUE(controller.startBundle(folder, output, true));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    EXPECT_TRUE(controller.batchRows().isEmpty());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
}

TEST(ResearchGuiUnit, BatchBundleExportReusesUrlGuardsAndWorkerTeardown)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto scenario = gui_bundle::scenario(temporary.path());
    const auto input = QUrl::fromLocalFile(gui_bundle::plan(temporary.path(), scenario));
    const auto parent = QUrl::fromLocalFile(temporary.path());
    {
        ResearchController controller;
        EXPECT_FALSE(controller.verifyBundle(QUrl("https://example.org/bundle"), true));
        EXPECT_FALSE(controller.exportBundle(input, parent, "../escape", true));
        ASSERT_TRUE(controller.exportBundle(input, parent, "cancelled-series", true));
        controller.cancel();
        ASSERT_TRUE(gui_bundle::wait(controller));
        EXPECT_TRUE(controller.exitCode() == 0 || controller.exitCode() == 3);
        EXPECT_TRUE(controller.reportPath().isEmpty());
    }
    {
        ResearchController controller;
        ASSERT_TRUE(controller.exportBundle(input, parent, "destroyed-series", true));
    }
    QCoreApplication::processEvents();
}

TEST(ResearchGuiUnit, UnsavedFormAndCompositionExportFreezeValuesAndKeepLiteralUrls)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    const auto parent = QUrl::fromLocalFile(temporary.path());
    auto values = gui_bundle::values();
    const auto bytes = boost::json::serialize(researchDraft(values)) + '\n';
    ASSERT_TRUE(controller.exportDraftBundle(values, parent, "form # %"));
    values["frames"] = 4;
    EXPECT_FALSE(controller.exportDraftBundle(values, parent, "overlap"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_EQ(controller.bundleInfo().value("source_scenario_sha256").toString().toStdString(),
              sv::research::sha256(bytes.data(), bytes.size()));
    const auto folder = controller.bundleInfo().value("directoryUrl").toUrl();
    EXPECT_EQ(folder, QUrl::fromLocalFile(temporary.path() + "/form # %"));
    auto document =
        sv::parse_json(sv::research::read_regular_file(
                           folder.toLocalFile().toStdString() + "/scenario.json", 1024 * 1024))
            .as_object();
    EXPECT_EQ(document.at("frames").to_number<unsigned>(), 1U);
    const auto source = QUrl::fromLocalFile(gui_bundle::scenario(temporary.path()));
    QVariantList members{QVariantMap{{"id", "second"}, {"scenario", source.toString()}},
                         QVariantMap{{"id", "first"}, {"scenario", source.toString()}}};
    const auto plan = boost::json::serialize(researchBatchDraft(members)) + '\n';
    ASSERT_TRUE(controller.exportBatchDraftBundle(members, parent, "series # %"));
    members.clear();
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_EQ(controller.bundleInfo().value("source_plan_sha256").toString().toStdString(),
              sv::research::sha256(plan.data(), plan.size()));
    const auto runs = controller.bundleInfo().value("runs").toList();
    ASSERT_EQ(runs.size(), 2);
    EXPECT_EQ(runs[0].toMap().value("id").toString(), "second");
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_FALSE(controller.available());
    EXPECT_FALSE(std::filesystem::exists((temporary.path() + "/overlap").toStdString()));
}

TEST(ResearchGuiUnit, UnsavedExportsRejectInvalidValuesPathsAndReferencesWithoutOutput)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    const auto parent = QUrl::fromLocalFile(temporary.path());
    auto values = gui_bundle::values();
    values["frames"] = false;
    EXPECT_FALSE(controller.exportDraftBundle(values, parent, "bad"));
    EXPECT_FALSE(controller.exportBatchDraftBundle({}, parent, "bad"));
    values = gui_bundle::values();
    EXPECT_FALSE(controller.exportDraftBundle(values, parent, "../escape"));
    EXPECT_FALSE(
        controller.exportDraftBundle(values, parent, "bad", QUrl("https://example.org/ref")));
    ASSERT_TRUE(controller.exportDraftBundle(
        values, parent, "bad", QUrl::fromLocalFile(temporary.path() + "/missing.json")));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    EXPECT_FALSE(std::filesystem::exists((temporary.path() + "/bad").toStdString()));
}
