#include "scenario.hpp"
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

TEST(ResearchScenario, DefaultsAndExplicitProfiles)
{
    const auto scenario = sv::research::parse_scenario(boost::json::parse(
        R"({"schema_version":1,"variants":[{"mode":"angular_feather","angle_power":4}]})"));
    ASSERT_EQ(scenario.variants.size(), 1U);
    EXPECT_EQ(scenario.repeats, 3U);
    EXPECT_EQ(scenario.warmup, 2U);
    EXPECT_EQ(scenario.variants[0].diagnostic, "color");
    EXPECT_DOUBLE_EQ(scenario.variants[0].angle_power, 4);
}

TEST(ResearchScenario, RejectsUnsupportedAndUnboundedScenariosBeforeConnection)
{
    for (const auto *json :
         {R"({"schema_version":2,"variants":[{"mode":"edge_feather"}]})",
          R"({"schema_version":1,"variants":[]})",
          R"({"schema_version":1,"repeats":0,"variants":[{"mode":"edge_feather"}]})",
          R"({"schema_version":1,"repeats":1.5,"variants":[{"mode":"edge_feather"}]})",
          R"({"schema_version":1,"warmup":101,"variants":[{"mode":"edge_feather"}]})",
          R"({"schema_version":1,"command":"shell","variants":[{"mode":"edge_feather"}]})",
          R"({"schema_version":1,"variants":[{"mode":"unknown"}]})",
          R"({"schema_version":1,"variants":[{"mode":"edge_feather","diagnostic":"unknown"}]})",
          R"({"schema_version":1,"variants":[{"mode":"edge_feather","angle_power":-1}]})",
          R"({"schema_version":1,"variants":[{"mode":"edge_feather","edge_width_px":4097}]})",
          R"({"schema_version":1,"variants":[{"mode":"edge_feather","shader":"code"}]})"})
    {
        EXPECT_ANY_THROW(sv::research::parse_scenario(boost::json::parse(json))) << json;
    }
    boost::json::array variants;
    for (int i = 0; i < 32; ++i)
    {
        variants.push_back(boost::json::object{{"mode", "edge_feather"}});
    }
    EXPECT_ANY_THROW(sv::research::parse_scenario(
        boost::json::object{{"schema_version", 1}, {"repeats", 1000}, {"variants", variants}}));
}

TEST(ResearchScenario, CancelBeforeConnectingReturnsFailureWithoutRestore)
{
    sv::research::Scenario scenario;
    scenario.variants.push_back({});
    std::atomic_bool cancel{true};
    const auto report = sv::research::run(scenario, {}, &cancel);
    EXPECT_FALSE(report.at("success").as_bool());
    EXPECT_FALSE(report.at("restore_required").as_bool());
    EXPECT_EQ(report.at("error").as_string(), "cancelled");
    EXPECT_EQ(report.at("scenario_sha256").as_string().size(), 64U);
}

TEST(ResearchScenarioIntegration, CancellationAndCallbackFailureRestoreBaseline)
{
    const auto *endpoint_json = std::getenv("SV_RESEARCH_TEST_ENDPOINT");
    if (!endpoint_json)
    {
        GTEST_SKIP() << "Executed with a live endpoint by client_transports";
    }
    const auto endpoint = boost::json::parse(endpoint_json).as_object();
    sv::client::Options options;
    options.timeout_ms = 3000;
    if (endpoint.contains("directory"))
    {
        options.endpoint.directory = std::string(endpoint.at("directory").as_string());
    }
    else
    {
        options.endpoint.transport = sv::client::Endpoint::Transport::Tcp;
        options.endpoint.host = std::string(endpoint.at("host").as_string());
        options.endpoint.control_port = endpoint.at("control_port").to_number<uint16_t>();
        options.endpoint.data_port = endpoint.at("data_port").to_number<uint16_t>();
    }
    const auto scenario = sv::research::parse_scenario(boost::json::parse(
        R"({"schema_version":1,"warmup":0,"repeats":3,"variants":[{"mode":"edge_feather"},{"mode":"angular_feather"}]})"));
    for (bool fail_callback : {false, true})
    {
        std::atomic_bool cancel{false};
        const auto report =
            sv::research::run(scenario, options, &cancel,
                              [&](unsigned, unsigned)
                              {
                                  if (fail_callback)
                                  {
                                      throw std::runtime_error("test callback failure");
                                  }
                                  cancel = true;
                              });
        EXPECT_FALSE(report.at("success").as_bool());
        EXPECT_TRUE(report.at("restored").as_bool()) << boost::json::serialize(report);
        EXPECT_EQ(report.at("samples").as_array().size(), 1U);
        EXPECT_EQ(report.at("baseline_rgba_sha256"), report.at("restored_rgba_sha256"));
        EXPECT_EQ(report.at("error").as_string(),
                  fail_callback ? "test callback failure" : "cancelled");
        // Give the single-session server time to observe orderly disconnect.
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
    }
}
