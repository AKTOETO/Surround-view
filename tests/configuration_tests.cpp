#include "sv/client_configuration.hpp"
#include "sv/config_service.hpp"
#include <atomic>
#include <gtest/gtest.h>
#include <thread>

namespace
{
sv::Message reply(boost::json::object fields)
{
    const auto revision = fields.at("revision");
    return {21,
            {{"accepted", true},
             {"command_id", "7"},
             {"config_revision", revision},
             {"configuration", std::move(fields)}},
            {}};
}
} // namespace

TEST(ConfigurationService, ActiveAndPersistedRemainDistinctUntilExplicitPersistence)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    auto first = service.read();
    EXPECT_EQ(first.at("revision"), "0");
    EXPECT_FALSE(first.at("has_runtime_changes").as_bool());
    EXPECT_FALSE(first.at("persistence_configured").as_bool());
    auto effective = initial.effective;
    effective.as_object()["profile_id"] = "runtime-profile";
    std::string error;
    ASSERT_TRUE(store.update_runtime_if_revision(effective, 0, error)) << error;
    const auto active = service.read();
    EXPECT_EQ(active.at("revision"), "1");
    EXPECT_TRUE(active.at("has_runtime_changes").as_bool());
    EXPECT_EQ(active.at("active").as_object().at("profile_id"), "runtime-profile");
    EXPECT_EQ(active.at("persisted"), first.at("persisted"));
    EXPECT_EQ(store.revision(), 1U); // Reads do not publish revisions.
    store.persist();
    EXPECT_FALSE(service.read().at("has_runtime_changes").as_bool());
    EXPECT_EQ(service.read().at("revision"), "1");
}

TEST(ConfigurationService, RejectsOversizedSnapshotWithoutChangingStore)
{
    auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    initial.profile_id = std::string(sv::max_header, 'x');
    initial.effective.as_object()["profile_id"] = initial.profile_id;
    sv::ConfigStore store(initial);
    EXPECT_THROW(sv::ConfigService(store).read(), std::length_error);
    EXPECT_EQ(store.revision(), 0U);
}

TEST(ConfigurationService, ConcurrentReadKeepsConfigAndRevisionTogether)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    std::atomic<bool> finished{false}, success{true};
    std::thread writer(
        [&]
        {
            for (uint64_t i = 1; i <= 64; ++i)
            {
                auto effective = initial.effective;
                effective.as_object()["profile_id"] = "revision-" + std::to_string(i);
                std::string error;
                if (!store.update_runtime_if_revision(effective, i - 1, error))
                {
                    success = false;
                    break;
                }
            }
            finished = true;
        });
    do
    {
        const auto state = service.read();
        const auto revision = sv::parse_decimal_u64(std::string(state.at("revision").as_string()));
        const auto expected =
            revision ? "revision-" + std::to_string(revision) : initial.profile_id;
        EXPECT_EQ(std::string(state.at("active").as_object().at("profile_id").as_string()),
                  expected);
        EXPECT_EQ(state.at("persisted"), initial.effective);
    } while (!finished);
    writer.join();
    EXPECT_TRUE(success);
    EXPECT_EQ(store.revision(), 64U);
}

TEST(ClientConfiguration, OwnsObjectsAndPreservesUint64Revision)
{
    sv::ConfigStore store(sv::load_config(SV_TEST_CONFIG_PATH));
    auto fields = sv::ConfigService(store).read();
    fields["revision"] = "18446744073709551615";
    auto message = reply(fields);
    const auto snapshot = sv::client::parse_configuration_snapshot(message);
    message.header.clear();
    EXPECT_EQ(snapshot.revision, UINT64_MAX);
    EXPECT_EQ(snapshot.active, fields.at("active").as_object());
    EXPECT_FALSE(snapshot.has_runtime_changes);
}

TEST(ClientConfiguration, RejectsMalformedEnvelopeAndContradictorySnapshot)
{
    sv::ConfigStore store(sv::load_config(SV_TEST_CONFIG_PATH));
    const auto original = reply(sv::ConfigService(store).read());
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"schema_version", 2},
             {"schema_version", 1.0},
             {"scope", "session"},
             {"revision", "18446744073709551616"},
             {"revision", "1"},
             {"active", false},
             {"persisted", nullptr},
             {"has_runtime_changes", true},
             {"persistence_configured", 1}})
    {
        auto message = original;
        message.header.at("configuration").as_object()[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_configuration_snapshot(message)) << key;
    }
    auto message = original;
    message.type = 11;
    EXPECT_THROW(sv::client::parse_configuration_snapshot(message), std::invalid_argument);
    message = original;
    message.header["accepted"] = false;
    EXPECT_THROW(sv::client::parse_configuration_snapshot(message), std::invalid_argument);
    message = original;
    message.payload = {1};
    EXPECT_THROW(sv::client::parse_configuration_snapshot(message), std::invalid_argument);
}
