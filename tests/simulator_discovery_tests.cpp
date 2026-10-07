#include "ipc_discovery.hpp"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <gtest/gtest.h>

namespace
{
void create_endpoint_files(const QString &directory)
{
    ASSERT_TRUE(QDir().mkpath(directory));
    for (const auto &name : {QStringLiteral("control.sock"), QStringLiteral("data.sock")})
    {
        QFile file(QDir(directory).filePath(name));
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.close();
    }
}
} // namespace

TEST(SimulatorDiscovery, KeepsExistingAbsoluteCandidatesInOrderAndDeduplicates)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto first = QDir(temporary.path()).filePath("first");
    const auto second = QDir(temporary.path()).filePath("second");
    create_endpoint_files(first);
    create_endpoint_files(second);

    const auto candidates = discover_local_ipc_candidates(
        {first, first, QStringLiteral("relative/path"),
         QDir(temporary.path()).filePath("missing"), second});
    EXPECT_EQ(candidates, (QStringList{first, second}));
}

TEST(SimulatorDiscovery, RequiresBothProtocolSockets)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto only_control = QDir(temporary.path()).filePath("partial");
    ASSERT_TRUE(QDir().mkpath(only_control));
    QFile control(QDir(only_control).filePath("control.sock"));
    ASSERT_TRUE(control.open(QIODevice::WriteOnly));
    control.close();

    EXPECT_TRUE(discover_local_ipc_candidates({only_control}).isEmpty());
}
