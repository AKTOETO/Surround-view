#include "sv/client_configuration.hpp"
#include "sv/config_service.hpp"
#include <chrono>
#include <filesystem>
#include <gtest/gtest.h>

namespace
{
struct Publication
{
    unsigned prepared = 0, published = 0, discarded = 0;
};

class FakePrepared final : public sv::PreparedConfiguration
{
    Publication &counts_;
    bool published_ = false;

  public:

    explicit FakePrepared(Publication &counts) : counts_(counts)
    {
        ++counts_.prepared;
    }

    ~FakePrepared() override
    {
        counts_.discarded += !published_;
    }

    void publish() noexcept override
    {
        published_ = true;
        ++counts_.published;
    }
};

class ConfigurationApply : public testing::Test
{
  protected:

    sv::Config initial = sv::load_config(SV_TEST_CONFIG_PATH);
    std::filesystem::path directory =
        std::filesystem::temp_directory_path() /
        ("sv-config-apply-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path path = directory / "server.json";
    Publication counts;
    sv::PrepareConfiguration prepare = [&](const sv::Config &)
    { return std::make_unique<FakePrepared>(counts); };
    boost::json::object changes{{"fusion", boost::json::object{{"mode", "angular_feather"}}}};

    void SetUp() override
    {
        std::filesystem::create_directories(directory);
        sv::write_json(path, initial.effective);
    }

    void TearDown() override
    {
        std::filesystem::remove_all(directory);
    }
};
} // namespace

TEST_F(ConfigurationApply, RuntimeThenPersistentSnapshotPublishesExactlyOnceEach)
{
    sv::ConfigStore store(initial, path);
    sv::ConfigService service(store);
    const auto runtime =
        service.apply(0, changes, false, initial.view, sv::OutputProduct::FinalFrame, prepare);
    EXPECT_FALSE(runtime.at("persisted").as_bool());
    EXPECT_EQ(store.revision(), 1U);
    EXPECT_EQ(store.active()->fusion.mode, "angular_feather");
    EXPECT_EQ(sv::load_config(path).effective, initial.effective);
    const auto saved = service.apply(1, {{"surface", initial.effective.as_object().at("surface")}},
                                     true, initial.view, sv::OutputProduct::FinalFrame, prepare);
    EXPECT_TRUE(saved.at("persisted").as_bool());
    EXPECT_EQ(store.revision(), 2U);
    EXPECT_EQ(sv::load_config(path).effective, store.active()->effective);
    EXPECT_EQ(store.persisted()->effective, store.active()->effective);
    EXPECT_EQ(store.persisted()->fusion.mode,
              "angular_feather"); // Full active snapshot, not a selective save.
    EXPECT_EQ(counts.published, 2U);
    EXPECT_EQ(counts.discarded, 0U);
    sv::Message ack{21,
                    {{"accepted", true},
                     {"command_id", "9"},
                     {"config_revision", "2"},
                     {"configuration_application", saved}},
                    {}};
    auto result = sv::client::parse_configuration_application(ack);
    EXPECT_EQ(result.base_revision, 1U);
    EXPECT_EQ(result.revision, 2U);
    EXPECT_TRUE(result.persisted);
}

TEST_F(ConfigurationApply, PreparationAndPersistenceFailureKeepActiveBaseline)
{
    sv::ConfigStore store(initial, path);
    sv::ConfigService service(store);
    auto failing = [](const sv::Config &) -> std::unique_ptr<sv::PreparedConfiguration>
    { throw std::runtime_error("injected preparation failure"); };
    EXPECT_THROW(
        service.apply(0, changes, true, initial.view, sv::OutputProduct::FinalFrame, failing),
        std::runtime_error);
    EXPECT_EQ(sv::load_config(path).effective, initial.effective);
    std::filesystem::remove(path);
    std::filesystem::create_directory(path); // Deterministic rename failure.
    EXPECT_ANY_THROW(
        service.apply(0, changes, true, initial.view, sv::OutputProduct::FinalFrame, prepare));
    EXPECT_EQ(store.revision(), 0U);
    EXPECT_EQ(store.active()->effective, initial.effective);
    EXPECT_EQ(store.persisted()->effective, initial.effective);
    EXPECT_EQ(counts.published, 0U);
    EXPECT_EQ(counts.discarded, 1U);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
}

TEST_F(ConfigurationApply, RechecksRevisionAfterPreparationBeforeWritingFile)
{
    sv::ConfigStore store(initial, path);
    sv::ConfigService service(store);
    auto racing = [&](const sv::Config &) -> std::unique_ptr<sv::PreparedConfiguration>
    {
        auto other = initial.effective;
        other.as_object()["profile_id"] = "other-publication";
        std::string error;
        if (!store.update_runtime_if_revision(other, 0, error))
        {
            throw std::runtime_error(error);
        }
        return std::make_unique<FakePrepared>(counts);
    };
    EXPECT_THROW(
        service.apply(0, changes, true, initial.view, sv::OutputProduct::FinalFrame, racing),
        std::runtime_error);
    EXPECT_EQ(store.revision(), 1U);
    EXPECT_EQ(store.active()->profile_id, "other-publication");
    EXPECT_EQ(counts.published, 0U);
    EXPECT_EQ(counts.discarded, 1U);
    EXPECT_EQ(sv::load_config(path).effective, initial.effective);
}

TEST_F(ConfigurationApply, RejectsMissingPersistenceStaleInvalidAndEmptyPreparation)
{
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    EXPECT_ANY_THROW(
        service.apply(0, changes, true, initial.view, sv::OutputProduct::FinalFrame, prepare));
    EXPECT_ANY_THROW(
        service.apply(1, changes, false, initial.view, sv::OutputProduct::FinalFrame, prepare));
    EXPECT_ANY_THROW(
        service.apply(0, {}, false, initial.view, sv::OutputProduct::FinalFrame, prepare));
    auto empty = [](const sv::Config &) -> std::unique_ptr<sv::PreparedConfiguration>
    { return {}; };
    EXPECT_ANY_THROW(
        service.apply(0, changes, false, initial.view, sv::OutputProduct::FinalFrame, empty));
    EXPECT_EQ(counts.prepared, 0U);
    EXPECT_EQ(store.revision(), 0U);
}

TEST(ClientConfigurationApplication, RejectsContradictoryReceiptAndKeepsCatalogV1Compatibility)
{
    boost::json::object fields{{"schema_version", 1},
                               {"scope", "config_store_groups"},
                               {"base_config_revision", "0"},
                               {"revision", "1"},
                               {"applied", true},
                               {"persisted", false}};
    sv::Message original{21,
                         {{"accepted", true},
                          {"command_id", "9"},
                          {"config_revision", "1"},
                          {"configuration_application", fields}},
                         {}};
    EXPECT_NO_THROW(sv::client::parse_configuration_application(original));
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"base_config_revision", "18446744073709551615"},
             {"revision", "2"},
             {"applied", false},
             {"persisted", 1}})
    {
        auto bad = original;
        bad.header.at("configuration_application").as_object()[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_configuration_application(bad));
    }
    auto legacy = sv::configuration_catalog();
    legacy["schema_version"] = 1;
    legacy["generic_apply_supported"] = false;
    legacy["persistence"] = "none";
    legacy["operations"] = boost::json::array{"config_read", "config_catalog", "config_validate"};
    sv::Message ack{
        21, {{"accepted", true}, {"command_id", "10"}, {"configuration_catalog", legacy}}, {}};
    EXPECT_FALSE(sv::client::parse_configuration_catalog(ack).apply_supported);
}
