#include "research_gui_bundle_fixture.hpp"
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
