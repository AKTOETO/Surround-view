#include "qml_warning_capture.hpp"
#include "research_gui_bundle_fixture.hpp"
#include "sv/json.hpp"
#include <QFile>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
void setup(QQmlApplicationEngine &engine, ResearchController &controller)
{
    engine.rootContext()->setContextProperty("researchController", &controller);
    engine.rootContext()->setContextProperty("simulatorQtMajorVersion", QT_VERSION_MAJOR);
}

bool click(QObject *root, const char *name)
{
    auto *button = root->findChild<QObject *>(name);
    return button && button->property("enabled").toBool() &&
           QMetaObject::invokeMethod(button, "clicked");
}
} // namespace

TEST(SimulatorResearchBundleQml, OfflineExportVerifyControlsAndReceiptRejectChangedInputs)
{
    QmlWarningCapture warnings;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    QQmlApplicationEngine engine;
    setup(engine, controller);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
    std::unique_ptr<QObject> root(component.create());
    ASSERT_TRUE(root) << component.errorString().toStdString();
    root->setProperty("width", 1040);
    root->findChild<QObject *>("researchPlanMode")->setProperty("currentIndex", 4);
    auto *editor = root->findChild<QObject *>("researchBundleEditor");
    ASSERT_TRUE(editor);
    editor->setProperty("exportSource",
                        QUrl::fromLocalFile(gui_bundle::scenario(temporary.path())));
    editor->setProperty("exportParent", QUrl::fromLocalFile(temporary.path()));
    root->findChild<QObject *>("researchBundleShowExport")->setProperty("checked", true);
    root->findChild<QObject *>("researchBundleExportName")->setProperty("text", "bundle # %");
    auto *start = root->findChild<QObject *>("researchStart");
    auto *verify = root->findChild<QObject *>("researchBundleVerify");
    EXPECT_FALSE(start->property("enabled").toBool());
    EXPECT_FALSE(verify->property("enabled").toBool());
    ASSERT_TRUE(click(root.get(), "researchBundleExport"));
    EXPECT_FALSE(editor->property("enabled").toBool());
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    editor->setProperty("directory", QUrl::fromLocalFile(temporary.path() + "/bundle # %"));
    EXPECT_TRUE(verify->property("enabled").toBool());
    EXPECT_FALSE(start->property("enabled").toBool()); // no server; offline remains usable
    ASSERT_TRUE(click(root.get(), "researchBundleVerify"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0);
    const auto receipt =
        root->findChild<QObject *>("researchBundleReceipt")->property("text").toString();
    EXPECT_TRUE(receipt.contains("bundle # %"));
    EXPECT_TRUE(receipt.contains(controller.bundleInfo().value("manifest_sha256").toString()));
    std::ofstream((temporary.path() + "/bundle # %/scenario.json").toStdString(), std::ios::app)
        << ' ';
    ASSERT_TRUE(click(root.get(), "researchBundleVerify"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_TRUE(controller.bundleInfo().isEmpty());
    EXPECT_TRUE(controller.reportPath().isEmpty());
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}

TEST(SimulatorResearchBundleQml, VerifiedBundleRunsOnLiveServerThroughTypedControls)
{
    const auto source = qEnvironmentVariable("SV_RESEARCH_GUI_SCENARIO");
    if (source.isEmpty())
    {
        GTEST_SKIP() << "Executed with Unix/TCP regression fixture";
    }
    QmlWarningCapture warnings;
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    QQmlApplicationEngine engine;
    setup(engine, controller);
    QQmlComponent component(&engine, QUrl("qrc:/ResearchPanel.qml"));
    std::unique_ptr<QObject> root(component.create());
    ASSERT_TRUE(root) << component.errorString().toStdString();
    auto *item = qobject_cast<QQuickItem *>(root.get());
    ASSERT_TRUE(item);
    QQuickWindow window;
    window.resize(1040, 1340);
    window.setColor(QColor("#1a1f2c"));
    item->setParentItem(window.contentItem());
    item->setWidth(1000);
    item->setX(20);
    item->setY(15);
    window.show();
    root->findChild<QObject *>("researchPlanMode")->setProperty("currentIndex", 4);
    auto *editor = root->findChild<QObject *>("researchBundleEditor");
    ASSERT_TRUE(editor);
    editor->setProperty("exportSource", QUrl::fromLocalFile(source));
    editor->setProperty("exportParent", QUrl::fromLocalFile(temporary.path()));
    root->findChild<QObject *>("researchBundleShowExport")->setProperty("checked", true);
    root->findChild<QObject *>("researchBundleExportName")->setProperty("text", "portable # %");
    ASSERT_TRUE(click(root.get(), "researchBundleExport"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    const auto relocated = temporary.path() + "/relocated # %";
    std::filesystem::rename((temporary.path() + "/portable # %").toStdString(),
                            relocated.toStdString());
    editor->setProperty("directory", QUrl::fromLocalFile(relocated));
    ASSERT_TRUE(click(root.get(), "researchBundleVerify"));
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    const auto digest = controller.bundleInfo().value("manifest_sha256").toString();
    const auto endpoint =
        sv::parse_json(qEnvironmentVariable("SV_RESEARCH_GUI_ENDPOINT").toStdString()).as_object();
    sv::client::Options options;
    options.timeout_ms = 3000;
    if (endpoint.contains("directory"))
    {
        options.endpoint.directory = std::string(endpoint.at("directory").as_string());
    }
    else
    {
        options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
        options.endpoint.host = std::string(endpoint.at("host").as_string());
        options.endpoint.control_port = endpoint.at("control_port").to_number<uint16_t>();
        options.endpoint.data_port = endpoint.at("data_port").to_number<uint16_t>();
    }
    controller.setConnection(options);
    root->findChild<QObject *>("researchOutput")->setProperty("text", temporary.path() + "/result");
    const auto screenshot = qEnvironmentVariable("SV_RESEARCH_QUALITY_SCREENSHOT");
    if (!screenshot.isEmpty())
    {
        QCoreApplication::processEvents();
        ASSERT_TRUE(window.grabWindow().save(screenshot + ".bundle.png"));
    }
    ASSERT_TRUE(click(root.get(), "researchStart"));
    EXPECT_EQ(root->findChild<QObject *>("researchTabs")->property("currentIndex").toInt(), 1);
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    EXPECT_EQ(controller.qualityStatus(), "passed");
    EXPECT_FALSE(controller.rows().isEmpty());
    QFile report(controller.reportPath());
    ASSERT_TRUE(report.open(QIODevice::ReadOnly));
    const auto document = QJsonDocument::fromJson(report.readAll()).object();
    EXPECT_TRUE(document.value("restored").toBool());
    EXPECT_EQ(document.value("scenario_input_kind").toString(), "verified_bundle_snapshot_v1");
    EXPECT_EQ(document.value("input_bundle").toObject().value("manifest_sha256").toString(),
              digest);
    EXPECT_EQ(controller.bundleInfo().value("manifest_sha256").toString(), digest);
    EXPECT_TRUE(warnings.messages().isEmpty()) << warnings.messages().join('\n').toStdString();
}
