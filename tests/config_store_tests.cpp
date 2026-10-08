#include "sv/config_store.hpp"
#include <boost/json.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <string>

namespace
{
std::filesystem::path temporary_path(const std::string &label)
{
    return std::filesystem::temp_directory_path() /
           ("sv-config-store-" + label + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

void change_calibration_id(sv::Config &config, const std::string &id)
{
    config.cameras[0].calibration_id = id;
    config.effective.as_object().at("cameras").as_array()[0].as_object()["calibration_id"] = id;
}
} // namespace

TEST(ConfigStorePersistence, ValidUpdateReplacesFileBeforePublishingSnapshot)
{
    const auto directory = temporary_path("valid");
    std::filesystem::create_directories(directory);
    const auto path = directory / "server.json";
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial, path);

    auto updated = *store.active();
    change_calibration_id(updated, "persisted-camera-calibration");
    std::string error;
    ASSERT_TRUE(store.update(updated, error)) << error;
    EXPECT_EQ(store.revision(), 1U);
    EXPECT_EQ(store.active()->cameras[0].calibration_id, "persisted-camera-calibration");
    EXPECT_EQ(store.persisted()->cameras[0].calibration_id, "persisted-camera-calibration");

    auto stale = *store.active();
    change_calibration_id(stale, "stale-overwrite");
    EXPECT_FALSE(store.update_if_revision(stale, 0, error));
    EXPECT_EQ(error, "stale_config_revision");
    EXPECT_EQ(store.revision(), 1U);
    EXPECT_EQ(store.active()->cameras[0].calibration_id, "persisted-camera-calibration");

    std::ifstream input(path);
    ASSERT_TRUE(input.good());
    const auto persisted = sv::parse_config(boost::json::parse(input));
    EXPECT_EQ(persisted.cameras[0].calibration_id, "persisted-camera-calibration");

    auto invalid = *store.active();
    invalid.effective.as_object()["schema_version"] = 999;
    EXPECT_FALSE(store.update(invalid, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(store.revision(), 1U);
    EXPECT_EQ(store.active()->cameras[0].calibration_id, "persisted-camera-calibration");
    std::filesystem::remove_all(directory);
}

TEST(ConfigStorePersistence, FailedReplacementKeepsActiveSnapshot)
{
    const auto directory = temporary_path("failure");
    std::filesystem::create_directories(directory);
    const auto path = directory / "is-a-directory";
    std::filesystem::create_directory(path);
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial, path);
    auto updated = *store.active();
    change_calibration_id(updated, "must-not-be-published");

    std::string error;
    EXPECT_FALSE(store.update(updated, error));
    EXPECT_FALSE(error.empty());
    EXPECT_EQ(store.revision(), 0U);
    EXPECT_EQ(store.active()->cameras[0].calibration_id, initial.cameras[0].calibration_id);
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
    std::filesystem::remove_all(directory);
}
