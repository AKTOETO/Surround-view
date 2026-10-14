#include "sv/client_configuration.hpp"
#include "sv/config_operations.hpp"
#include "sv/config_service.hpp"
#include <gtest/gtest.h>

namespace
{
constexpr char instance[] = "0123456789abcdef0123456789abcdef";
constexpr char operation[] = "request-0123456789";

sv::ConfigOperations::Request request()
{
    return {0, {{"fusion", boost::json::object{{"mode", "angular_feather"}}}}, false};
}

sv::Message response(const sv::ConfigOperations &operations, const std::string &id,
                     const std::string &revision = "1", bool accepted = true)
{
    return {21,
            {{"command_id", "2"},
             {"accepted", accepted},
             {"config_revision", revision},
             {"configuration_operation", operations.status(instance, id)}},
            {}};
}
} // namespace

TEST(ConfigurationOperations, ReplaysCommittedResultWithoutApplyingAndRejectsChangedIntent)
{
    sv::ConfigOperations journal(instance);
    unsigned calls = 0;
    auto apply = [&](const auto &) { ++calls; };
    EXPECT_FALSE(journal.execute(instance, operation, request(), apply).replayed);
    EXPECT_TRUE(journal.execute(instance, operation, request(), apply).replayed);
    EXPECT_EQ(calls, 1U);
    const auto parsed =
        sv::client::parse_configuration_operation(response(journal, operation, "9"));
    EXPECT_EQ(parsed.revision, 1U); // Historical committed revision, not current ACK revision.
    EXPECT_EQ(parsed.state, "committed");
    auto changed = request();
    changed.persist = true;
    EXPECT_THROW(journal.execute(instance, operation, changed, apply), std::invalid_argument);
    changed = request();
    changed.base_revision = 1;
    EXPECT_THROW(journal.execute(instance, operation, changed, apply), std::invalid_argument);
    changed = request();
    changed.changes["fusion"].as_object()["mode"] = "hard_best_angle";
    EXPECT_THROW(journal.execute(instance, operation, changed, apply), std::invalid_argument);
    EXPECT_EQ(calls, 1U);
}

TEST(ConfigurationOperations, RetainsRejectedResultWithoutRetryingEvenAfterFaultIsFixed)
{
    sv::ConfigOperations journal(instance);
    unsigned calls = 0;
    auto failing = [&](const auto &)
    {
        ++calls;
        throw std::runtime_error("injected_disk_failure");
    };
    const auto result = journal.execute(instance, operation, request(), failing);
    EXPECT_EQ(result.entry.state, sv::ConfigOperations::Entry::State::Rejected);
    EXPECT_TRUE(journal.execute(instance, operation, request(), failing).replayed);
    EXPECT_EQ(calls, 1U);
    const auto parsed =
        sv::client::parse_configuration_operation(response(journal, operation, "0", false));
    EXPECT_EQ(parsed.state, "rejected");
    EXPECT_EQ(parsed.reason, "injected_disk_failure");
    EXPECT_FALSE(parsed.persisted);
}

TEST(ConfigurationOperations, FullJournalRetainsOldEntriesAndRejectsBeforeMutation)
{
    sv::ConfigOperations journal(instance);
    unsigned calls = 0;
    auto apply = [&](const auto &) { ++calls; };
    for (size_t i = 0; i < sv::ConfigOperations::capacity; ++i)
    {
        journal.execute(instance, "operation-000000-" + std::to_string(i), request(), apply);
    }
    EXPECT_THROW(journal.execute(instance, operation, request(), apply), std::length_error);
    EXPECT_TRUE(journal.execute(instance, "operation-000000-0", request(), apply).replayed);
    EXPECT_EQ(calls, sv::ConfigOperations::capacity);
    EXPECT_EQ(journal.status(instance, operation).at("state"), "not_found");
}

TEST(ConfigurationOperations, RejectsNamespaceIdentityAndOversizeBeforeExecuting)
{
    sv::ConfigOperations journal(instance);
    unsigned calls = 0;
    auto apply = [&](const auto &) { ++calls; };
    EXPECT_THROW(journal.execute("old-process", operation, request(), apply),
                 std::invalid_argument);
    for (const auto &id : {"", "short", "operation-with-invalid/slash"})
    {
        EXPECT_THROW(journal.execute(instance, id, request(), apply), std::invalid_argument);
    }
    auto large = request();
    large.changes["extra"] = std::string(sv::ConfigOperations::max_request_bytes, 'x');
    EXPECT_THROW(journal.execute(instance, operation, large, apply), std::length_error);
    auto overflow = request();
    overflow.base_revision = UINT64_MAX;
    EXPECT_THROW(journal.execute(instance, operation, overflow, apply), std::overflow_error);
    EXPECT_EQ(calls, 0U);
    EXPECT_EQ(sv::client::parse_configuration_operation(response(journal, operation, "0")).state,
              "not_found");
    sv::ConfigOperations restarted("another-instance");
    EXPECT_THROW(restarted.status(instance, operation), std::invalid_argument);
}

TEST(ConfigurationOperations, NonstandardFailureStaysPendingAndCannotBeExecutedAgain)
{
    sv::ConfigOperations journal(instance);
    try
    {
        journal.execute(instance, operation, request(), [](const auto &) { throw 42; });
        FAIL();
    }
    catch (int)
    {
    }
    bool called = false;
    EXPECT_TRUE(
        journal.execute(instance, operation, request(), [&](const auto &) { called = true; })
            .replayed);
    EXPECT_FALSE(called);
    EXPECT_EQ(journal.status(instance, operation).at("state"), "pending");
}

TEST(ConfigurationOperations, TypedParserRejectsContradictionsAndFutureCommittedRevision)
{
    sv::ConfigOperations journal(instance);
    journal.execute(instance, operation, request(), [](const auto &) {});
    auto ack = response(journal, operation);
    EXPECT_NO_THROW(sv::client::parse_configuration_operation(ack));
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"revision", "2"},
             {"base_config_revision", "18446744073709551615"},
             {"reason", "unexpected"},
             {"state", "unknown"},
             {"operation_id", ""},
             {"persisted", 1}})
    {
        auto bad = ack;
        bad.header.at("configuration_operation").as_object()[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_configuration_operation(bad));
    }
    EXPECT_THROW(sv::client::parse_configuration_operation(response(journal, operation, "0")),
                 std::invalid_argument);
}
