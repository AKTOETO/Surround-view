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

    const auto candidates =
        discover_local_ipc_candidates({first, first, QStringLiteral("relative/path"),
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

TEST(SimulatorDiscovery, SkipsUnconnectableLongUtf8PathsAndContinues)
{
    QTemporaryDir temporary;
    ASSERT_TRUE(temporary.isValid());
    const auto tooLong = QDir(temporary.path()).filePath(QString(81, 'x'));
    const auto tooManyBytes = QDir(temporary.path()).filePath(QString(40, QChar(0x044f)));
    const auto valid = QDir(temporary.path()).filePath("valid");
    create_endpoint_files(tooLong);
    create_endpoint_files(tooManyBytes);
    create_endpoint_files(valid);
    EXPECT_EQ(discover_local_ipc_candidates({tooLong, tooManyBytes, valid}), QStringList{valid});
}
