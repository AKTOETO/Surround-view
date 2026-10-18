#include "research_capabilities_fixture.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include <QTemporaryDir>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

TEST(ResearchGuiCriteria, InheritsByDefaultAndRequiresReferenceForExplicitCriteria)
{
    auto form = capability_fixture::form();
    EXPECT_FALSE(researchDraft(form).contains("quality_criteria"));
    form["quality_criteria"] = QVariantMap{{"max_mae_rgb8", 0}, {"min_psnr_db", 1e-12}};
    EXPECT_ANY_THROW(researchDraft(form));
    const auto document = researchDraft(form, "/missing reference.json");
    EXPECT_EQ(document.at("quality_criteria").as_object().at("max_mae_rgb8").to_number<double>(),
              0);
    EXPECT_DOUBLE_EQ(
        document.at("quality_criteria").as_object().at("min_psnr_db").to_number<double>(), 1e-12);
}

TEST(ResearchGuiCriteria, InvalidCriteriaNeverStartWorkerOrCreateOutput)
{
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/result";
    for (const auto &criteria : std::vector<QVariantMap>{{},
                                                         {{"max_mae_rgb8", -1}},
                                                         {{"max_mae_rgb8", true}},
                                                         {{"max_rmse_rgb8", 256}},
                                                         {{"min_psnr_db", 201}},
                                                         {{"unknown", 1}}})
    {
        auto form = capability_fixture::form();
        form["quality_criteria"] = criteria;
        const auto reference = QUrl::fromLocalFile(directory.path() + "/absent.json");
        EXPECT_FALSE(controller.draftCapabilityError(form, capability_fixture::catalog(), reference)
                         .isEmpty());
        EXPECT_FALSE(
            controller.startCatalogDraft(form, output, reference, capability_fixture::catalog()));
        EXPECT_FALSE(controller.busy());
        EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    }
}

TEST(ResearchGuiCriteria, ReferenceUrlAndCriteriaReadinessAreValidatedWithoutLoadingFiles)
{
    ResearchController controller;
    auto form = capability_fixture::form();
    form["quality_criteria"] = QVariantMap{{"max_mae_rgb8", 0}};
    EXPECT_FALSE(controller.draftCapabilityError(form, capability_fixture::catalog()).isEmpty());
    EXPECT_FALSE(controller
                     .draftCapabilityError(form, capability_fixture::catalog(),
                                           QUrl("https://invalid/ref.json"))
                     .isEmpty());
    EXPECT_TRUE(controller
                    .draftCapabilityError(form, capability_fixture::catalog(),
                                          QUrl::fromLocalFile("/absent # %.json"))
                    .isEmpty());
    EXPECT_FALSE(controller.busy());
}
