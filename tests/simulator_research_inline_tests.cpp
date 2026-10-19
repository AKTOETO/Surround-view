#include "batch_bundle.hpp"
#include "batch_plan.hpp"
#include "research_batch_draft.hpp"
#include "research_gui_bundle_fixture.hpp"
#include "sv/json.hpp"
#include <QJsonDocument>
#include <QTemporaryDir>
#include <gtest/gtest.h>

TEST(ResearchGuiDraft, InlineMemberFreezesFormAndMixedPlanRoundTripsOffline)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    auto values = gui_bundle::values();
    const auto reference = QUrl::fromLocalFile(temporary.path() + "/missing # %.json");
    const auto snapshot = controller.batchScenario(values, reference);
    ASSERT_TRUE(snapshot.value("success").toBool());
    auto scenario = snapshot.value("scenario").toMap();
    values["frames"] = 2;
    EXPECT_EQ(scenario.value("frames").toInt(), 1);
    EXPECT_EQ(scenario.value("quality_reference").toString(), reference.toLocalFile());
    const QVariantList members{QVariantMap{{"id", "inline"}, {"scenario", scenario}},
                               QVariantMap{{"id", "file"}, {"scenario", reference}}};
    const auto plan = researchBatchDraft(members);
    EXPECT_EQ(plan.at("schema_version").to_number<unsigned>(), 2U);
    EXPECT_TRUE(plan.at("runs").as_array()[0].as_object().at("scenario").is_object());
    const auto target = QUrl::fromLocalFile(temporary.path() + "/plan.json");
    ASSERT_TRUE(controller.saveBatch(members, target).value("success").toBool());
    const auto imported = controller.importBatch(target);
    ASSERT_TRUE(imported.value("success").toBool());
    EXPECT_EQ(researchBatchDraft(imported.value("members").toList()), plan);
    EXPECT_FALSE(controller.busy());
    EXPECT_FALSE(controller.available());
    EXPECT_TRUE(controller.reportPath().isEmpty());
}

TEST(ResearchGuiDraft, InlineImportResolvesReferenceWithoutReadingAndPreservesSymlinkMeaning)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const std::filesystem::path root(temporary.path().toStdString());
    std::filesystem::create_directories(root / "real/deep");
    std::filesystem::create_directory_symlink(root / "real/deep", root / "link");
    auto scenario = researchDraft(gui_bundle::values(), "link/../reference # %.json");
    const boost::json::object plan{
        {"schema_version", 2},
        {"runs", boost::json::array{boost::json::object{{"id", "one"}, {"scenario", scenario}}}}};
    sv::research::write_batch_plan(plan, root / "plan.json");
    const auto imported = researchImportBatch(QUrl::fromLocalFile(temporary.path() + "/plan.json"));
    const auto result = researchBatchDraft(imported);
    const auto &reference = result.at("runs")
                                .as_array()[0]
                                .as_object()
                                .at("scenario")
                                .as_object()
                                .at("quality_reference");
    EXPECT_EQ(std::string(reference.as_string()), (root / "link/../reference # %.json").string());
    ResearchController controller;
    auto bad = gui_bundle::values();
    bad["frames"] = 0;
    EXPECT_FALSE(controller.batchScenario(bad).value("success").toBool());
    EXPECT_FALSE(controller.batchScenario(gui_bundle::values(), QUrl("https://example.invalid/ref"))
                     .value("success")
                     .toBool());
    scenario["unknown"] = true;
    const auto encoded =
        QJsonDocument::fromJson(QByteArray::fromStdString(boost::json::serialize(scenario)))
            .toVariant();
    EXPECT_ANY_THROW(researchBatchDraft({QVariantMap{{"id", "bad"}, {"scenario", encoded}}}));
}

TEST(ResearchGuiUnit, InlineCompositionExportsAndVerifiesWithoutSourceScenarioFiles)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    ResearchController controller;
    const auto snapshot = controller.batchScenario(gui_bundle::values());
    ASSERT_TRUE(snapshot.value("success").toBool());
    const QVariantList members{
        QVariantMap{{"id", "one"}, {"scenario", snapshot.value("scenario")}},
        QVariantMap{{"id", "two"}, {"scenario", snapshot.value("scenario")}}};
    const auto parent = QUrl::fromLocalFile(temporary.path());
    ASSERT_TRUE(controller.exportBatchDraftBundle(members, parent, "inline # %"));
    EXPECT_FALSE(controller.batchScenario(gui_bundle::values()).value("success").toBool());
    ASSERT_TRUE(gui_bundle::wait(controller));
    ASSERT_EQ(controller.exitCode(), 0) << controller.status().toStdString();
    const auto root = temporary.path() + "/inline # %";
    sv::research::BatchBundle frozen(root.toStdString());
    EXPECT_EQ(frozen.summary().at("runs").as_array().size(), 2U);
    EXPECT_FALSE(std::filesystem::exists((temporary.path() + "/scenario.json").toStdString()));
    ASSERT_TRUE(controller.verifyBundle(QUrl::fromLocalFile(root), true));
    ASSERT_TRUE(gui_bundle::wait(controller));
    EXPECT_EQ(controller.exitCode(), 0);
}
