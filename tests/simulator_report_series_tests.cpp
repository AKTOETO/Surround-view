#include "research_comparison.hpp"
#include "research_comparison_fixture.hpp"
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

QUrl write(const QString &root, const char *name, unsigned number)
{
    auto report = comparison_fixture::report();
    report["fixture_run"] = number;
    const auto file = root + '/' + name;
    std::ofstream(file.toStdString()) << boost::json::serialize(report);
    return QUrl::fromLocalFile(file);
}
} // namespace

TEST(ResearchGuiComparison, SeriesFreezesSelectionExportsAndSharesPairWorker)
{
    QTemporaryDir directory;
    ASSERT_TRUE(directory.isValid());
    const auto a = write(directory.path(), "a # %.json", 0);
    const auto b = write(directory.path(), "b.json", 1);
    const auto c = write(directory.path(), "c.json", 2);
    ResearchComparison model;
    QVariantList files{a, b, c};
    ASSERT_TRUE(model.compareSeries(files));
    EXPECT_TRUE(model.series());
    EXPECT_FALSE(model.compare(a, b));
    files.clear();
    ASSERT_TRUE(wait(model));
    EXPECT_EQ(model.result().value("report_count").toInt(), 3);
    EXPECT_EQ(model.result().value("inputs").toList().size(), 3);
    EXPECT_TRUE(model.save(QUrl::fromLocalFile(directory.path() + "/series.json")));
    EXPECT_FALSE(model.save(a));
    ASSERT_TRUE(model.compare(a, b));
    EXPECT_FALSE(model.series());
    ASSERT_TRUE(wait(model));
    EXPECT_EQ(model.result().value("policy").toString(), "report_claims_comparison_v4");
    EXPECT_FALSE(model.compareSeries({a}));
    EXPECT_TRUE(model.series());
    EXPECT_TRUE(model.result().isEmpty());
    EXPECT_FALSE(model.busy());
}

TEST(ResearchGuiComparison, SeriesDuplicateFailureCancellationAndTeardownClearStaleResults)
{
    QTemporaryDir directory;
    const auto a = write(directory.path(), "a.json", 0);
    const auto b = write(directory.path(), "b.json", 1);
    ResearchComparison model;
    EXPECT_FALSE(model.compareSeries({a, QUrl("https://example.invalid/report")}));
    EXPECT_FALSE(model.busy());
    ASSERT_TRUE(model.compareSeries({a, a}));
    ASSERT_TRUE(wait(model));
    EXPECT_TRUE(model.result().isEmpty());
    ASSERT_TRUE(model.compareSeries({a, b}));
    model.cancel();
    ASSERT_TRUE(wait(model));
    EXPECT_TRUE(model.result().isEmpty());
    EXPECT_FALSE(model.save(QUrl::fromLocalFile(directory.path() + "/cancelled.json")));
    {
        ResearchComparison closing;
        ASSERT_TRUE(closing.compareSeries({a, b}));
    }
    QCoreApplication::processEvents();
}
