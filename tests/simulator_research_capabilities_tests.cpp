#include "research_capabilities.hpp"
#include "research_capabilities_fixture.hpp"
#include "research_controller.hpp"
#include "research_draft.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <filesystem>
#include <gtest/gtest.h>
#include <vector>

TEST(ResearchGuiCapabilities, IntersectsKnownMethodsAndRequiresExplicitCommonEnums)
{
    auto catalog = capability_fixture::catalog();
    const auto full = researchCapabilities(catalog);
    EXPECT_EQ(full.value("modes").toStringList().size(), 7);
    EXPECT_TRUE(full.value("boundaries").toStringList().contains("both"));
    EXPECT_NO_THROW(
        validateResearchCapabilities(researchDraft(capability_fixture::form()), catalog));
    catalog["modes"] = QStringList{"future_method", "edge_feather"};
    catalog.remove("pyramid_levels");
    catalog.remove("pyramid_boundary");
    catalog.remove("seam_solver");
    EXPECT_EQ(researchCapabilities(catalog).value("modes").toStringList(),
              QStringList{"edge_feather"});
    auto form = capability_fixture::form();
    EXPECT_ANY_THROW(validateResearchCapabilities(researchDraft(form), catalog));
    form["modes"] = QStringList{"edge_feather"};
    EXPECT_NO_THROW(validateResearchCapabilities(researchDraft(form), catalog));
}

TEST(ResearchGuiCapabilities, RejectsSelectedBoundaryLevelOrSolverOutsideServerSubset)
{
    auto catalog = capability_fixture::catalog();
    catalog["pyramid_boundary"] = QStringList{"normalized"};
    catalog["seam_solver"] = QStringList{"alpha_expansion"};
    catalog["pyramid_levels"] = QVariantMap{{"min", 3}, {"max", 4}};
    auto form = capability_fixture::form();
    EXPECT_ANY_THROW(validateResearchCapabilities(researchDraft(form), catalog));
    form["boundary"] = "normalized";
    form["seam_solver"] = "alpha_expansion";
    EXPECT_NO_THROW(validateResearchCapabilities(researchDraft(form), catalog));
    form["pyramid_levels"] = 2;
    EXPECT_ANY_THROW(validateResearchCapabilities(researchDraft(form), catalog));
    form["pyramid_levels"] = 5;
    EXPECT_ANY_THROW(validateResearchCapabilities(researchDraft(form), catalog));
    catalog["pyramid_levels"] = QVariantMap{{"min", 9}, {"max", 12}};
    EXPECT_FALSE(
        researchCapabilities(catalog).value("modes").toStringList().contains("multi_band"));
}

TEST(ResearchGuiCapabilities, MissingPrerequisitesDisableOnlyAffectedFamilies)
{
    auto catalog = capability_fixture::catalog();
    catalog.remove("seam_solver");
    auto caps = researchCapabilities(catalog);
    EXPECT_TRUE(caps.value("modes").toStringList().contains("multi_band"));
    EXPECT_FALSE(caps.value("modes").toStringList().contains("graph_cut_seam"));
    catalog.remove("pyramid_levels");
    caps = researchCapabilities(catalog);
    EXPECT_FALSE(caps.value("modes").toStringList().contains("multi_band"));
    EXPECT_TRUE(caps.value("modes").toStringList().contains("edge_feather"));
}

TEST(ResearchGuiCapabilities, MalformedOrUnknownCatalogNeverEnablesTheForm)
{
    ResearchController controller;
    for (const auto &change :
         std::vector<QVariantMap>{{{"version", true}},
                                  {{"version", 6}},
                                  {{"diagnostics", QStringList{"coverage"}}},
                                  {{"modes", QStringList{"edge_feather", "edge_feather"}}},
                                  {{"modes", QStringList{"unknown"}}},
                                  {{"pyramid_levels", QVariantMap{{"min", 4}, {"max", 3}}}},
                                  {{"pyramid_levels", QVariantMap{{"min", 1.5}, {"max", 8}}}},
                                  {{"pyramid_boundary", QStringList{"zero", "zero"}}}})
    {
        auto catalog = capability_fixture::catalog();
        for (auto i = change.begin(); i != change.end(); ++i)
        {
            catalog[i.key()] = i.value();
        }
        EXPECT_FALSE(controller.draftCapabilities(catalog).value("ready").toBool());
        EXPECT_FALSE(
            controller.draftCapabilityError(capability_fixture::form(), catalog).isEmpty());
    }
    EXPECT_FALSE(controller.draftCapabilities({}).value("ready").toBool());
}

TEST(ResearchGuiCapabilities, UnsupportedProfileFailsBeforeCreatingOutputOrStartingWorker)
{
    ResearchController controller;
    controller.setConnection(sv::client::Options{});
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto output = directory.path() + "/missing";
    auto catalog = capability_fixture::catalog();
    catalog["modes"] = QStringList{"edge_feather"};
    EXPECT_FALSE(controller.startCatalogDraft(capability_fixture::form(), output, {}, catalog));
    EXPECT_FALSE(controller.busy());
    EXPECT_EQ(controller.exitCode(), 3);
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
    // A compatible snapshot delegates to the ordinary immutable launch/IO worker.
    catalog = capability_fixture::catalog();
    EXPECT_TRUE(
        controller.startCatalogDraft(capability_fixture::form(), directory.path(), {}, catalog));
    EXPECT_FALSE(controller.startCatalogDraft(capability_fixture::form(), output, {}, {}));
    QElapsedTimer timer;
    timer.start();
    while (controller.busy() && timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    ASSERT_FALSE(controller.busy());
    EXPECT_FALSE(std::filesystem::exists(output.toStdString()));
}
