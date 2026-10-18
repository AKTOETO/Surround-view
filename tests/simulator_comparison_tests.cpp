#include "batch_plan.hpp"
#include "digest.hpp"
#include "research_comparison.hpp"
#include "research_comparison_fixture.hpp"
#include "sv/json.hpp"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QThread>
#include <fstream>
#include <gtest/gtest.h>

namespace
{
bool wait(ResearchComparison &model)
{
    QElapsedTimer timer;
    timer.start();
    while (model.busy() && timer.elapsed() < 10000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(1);
    }
    return !model.busy();
}
} // namespace

TEST(ResearchGuiComparison, AsyncFrozenPairSavesAuditAndClearsStaleResultsOnFailure)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto file = directory.path() + "/report # %.json";
    std::ofstream(file.toStdString()) << boost::json::serialize(comparison_fixture::report());
    const auto sourceBytes = sv::research::read_batch_plan(file.toStdString());
    ResearchComparison model;
    EXPECT_TRUE(model.compare(QUrl::fromLocalFile(file), QUrl::fromLocalFile(file)));
    EXPECT_TRUE(model.busy());
    EXPECT_FALSE(model.compare(QUrl("https://example.invalid"), QUrl{}));
    ASSERT_TRUE(wait(model));
    ASSERT_EQ(model.result().value("rows").toList().size(), 2);
    EXPECT_EQ(model.result().value("inputs").toString(), "matching_claims");
    const auto output = QUrl::fromLocalFile(directory.path() + "/comparison.json");
    EXPECT_TRUE(model.save(output));
    EXPECT_FALSE(model.save(output));
    const auto bytes = sv::research::read_batch_plan(output.toLocalFile().toStdString());
    const auto audit = sv::parse_json(bytes).as_object();
    EXPECT_EQ(audit.at("policy"), "report_claims_comparison_v1");
    EXPECT_EQ(std::string(audit.at("left_sha256").as_string()),
              sv::research::sha256(sourceBytes.data(), sourceBytes.size()));
    EXPECT_EQ(audit.at("left_sha256"), audit.at("right_sha256"));
    EXPECT_EQ(std::string(audit.at("left_path").as_string()), file.toStdString());
    EXPECT_EQ(sv::research::read_batch_plan(file.toStdString()), sourceBytes);
    EXPECT_FALSE(model.compare(QUrl("https://example.invalid"), QUrl::fromLocalFile(file)));
    EXPECT_TRUE(model.result().isEmpty());
    EXPECT_FALSE(model.save(QUrl::fromLocalFile(directory.path() + "/stale.json")));
    EXPECT_TRUE(model.compare(QUrl::fromLocalFile(file), QUrl::fromLocalFile(file + "-missing")));
    ASSERT_TRUE(wait(model));
    EXPECT_TRUE(model.result().isEmpty());
}

TEST(ResearchGuiComparison, CancellationAndDestructionJoinWithoutQueuedUseAfterFree)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto file = directory.path() + "/report # %.json";
    std::ofstream(file.toStdString()) << boost::json::serialize(comparison_fixture::report());
    ResearchComparison model;
    ASSERT_TRUE(model.compare(QUrl::fromLocalFile(file), QUrl::fromLocalFile(file)));
    model.cancel();
    ASSERT_TRUE(wait(model));
    EXPECT_TRUE(model.result().isEmpty());
    EXPECT_TRUE(model.status().contains("отменено"));
    {
        ResearchComparison temporary;
        ASSERT_TRUE(temporary.compare(QUrl::fromLocalFile(file), QUrl::fromLocalFile(file)));
    }
    QCoreApplication::processEvents();
}
