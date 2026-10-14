#include "sv/client_configuration.hpp"
#include "sv/config_service.hpp"
#include <gtest/gtest.h>

namespace
{
sv::Message validation_reply(boost::json::object result, const char *key)
{
    return {21,
            {{"accepted", true},
             {"command_id", "8"},
             {"config_revision", "0"},
             {key, std::move(result)}},
            {}};
}
} // namespace

TEST(ConfigurationValidation, ReplacesWholeGroupsWithoutMutatingStore)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    const auto before = service.read();
    const auto result = service.validate(0,
                                         {{"fusion", boost::json::object{{"mode", "multi_band"}}},
                                          {"surface", initial.effective.as_object().at("surface")}},
                                         initial.view, sv::OutputProduct::FinalFrame);
    EXPECT_EQ(result.at("changes").as_object().at("fusion").as_object().at("pyramid_levels"), 4);
    EXPECT_FALSE(result.at("applied").as_bool());
    EXPECT_FALSE(result.at("renderer_prepared").as_bool());
    EXPECT_EQ(service.read(), before);
    EXPECT_EQ(store.active()->fusion.mode, initial.fusion.mode);
    const auto typed = sv::client::parse_configuration_validation(
        validation_reply(result, "configuration_validation"));
    EXPECT_EQ(typed.base_revision, 0U);
    EXPECT_TRUE(typed.changes.contains("surface"));
}

TEST(ConfigurationValidation, RejectsStaleEmptyUnsupportedAndMalformedChanges)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    const auto before = service.read();
    EXPECT_THROW(service.validate(1, {}, initial.view, sv::OutputProduct::FinalFrame),
                 std::invalid_argument);
    for (const auto &changes : std::vector<boost::json::object>{
             {},
             {{"cameras", boost::json::object{}}},
             {{"fusion", false}},
             {{"fusion", boost::json::object{{"mode", "unknown"}}}},
             {{"fusion", boost::json::object{{"mode", "edge_feather"}, {"angle_power", true}}}},
             {{"fusion", boost::json::object{{"mode", "edge_feather"}, {"angle_power", 0}}}},
             {{"fusion",
               boost::json::object{{"mode", "edge_feather"}, {"angle_power", 2}, {"typo", 1}}}}})
    {
        EXPECT_ANY_THROW(service.validate(0, changes, initial.view, sv::OutputProduct::FinalFrame));
    }
    EXPECT_EQ(service.read(), before);
}

TEST(ConfigurationValidation, EnforcesCurrentViewOutputAndResearchBudget)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    sv::ConfigService service(store);
    const boost::json::object changes{
        {"fusion", boost::json::object{{"mode", "edge_feather"}, {"diagnostic", "coverage"}}}};
    for (auto product : {sv::OutputProduct::RenderSupport, sv::OutputProduct::SourceVisibility,
                         sv::OutputProduct::SourceDisagreement})
    {
        EXPECT_THROW(service.validate(0, changes, initial.view, product), std::invalid_argument);
    }
    auto unsafe = initial.view;
    unsafe.elevation = 0; // Current orbit is checked, not just Config.view.
    EXPECT_THROW(service.validate(0, changes, unsafe, sv::OutputProduct::FinalFrame),
                 std::invalid_argument);
    auto large = initial.effective;
    large.as_object()["output"] = boost::json::object{{"width", 1920}, {"height", 1080}};
    sv::ConfigStore large_store(sv::parse_config(large));
    EXPECT_THROW(sv::ConfigService(large_store)
                     .validate(0, {{"fusion", boost::json::object{{"mode", "multi_band"}}}},
                               initial.view, sv::OutputProduct::FinalFrame),
                 std::invalid_argument);
    EXPECT_EQ(store.revision(), 0U);
}

TEST(ConfigurationDiscovery, CatalogSharesFusionSurfaceBoundsAndExplicitPolicies)
{
    sv::ConfigStore store(sv::load_config(SV_TEST_CONFIG_PATH));
    const auto fields = sv::ConfigService(store).catalog();
    const auto result =
        sv::client::parse_configuration_catalog(validation_reply(fields, "configuration_catalog"));
    EXPECT_EQ(result.operations.size(), 4U);
    EXPECT_TRUE(result.apply_supported);
    EXPECT_EQ(result.replaceable_groups, (std::vector<std::string>{"fusion", "surface"}));
    EXPECT_EQ(result.fusion, sv::fusion_catalog());
    EXPECT_EQ(result.surface, sv::surface_catalog());
    EXPECT_EQ(result.surface.at("geometry_fields").as_object().size(), 4U);
    EXPECT_EQ(result.surface.at("geometry_fields")
                  .as_object()
                  .at("cube_floor_v1")
                  .as_object()
                  .at("face_cells")
                  .as_object()
                  .at("maximum"),
              128);
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"generic_apply_supported", false},
             {"prepares_renderer", true},
             {"reserves_revision", true},
             {"persistence", "automatic"},
             {"omitted_groups", "reset"},
             {"replaceable_groups", boost::json::array{"fusion", "fusion"}},
             {"operations", boost::json::array{}}})
    {
        auto bad = fields;
        bad[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_configuration_catalog(
            validation_reply(bad, "configuration_catalog")));
    }
}

TEST(ClientConfigurationValidation, RejectsMisleadingAndMalformedResult)
{
    const auto initial = sv::load_config(SV_TEST_CONFIG_PATH);
    sv::ConfigStore store(initial);
    const auto fields = sv::ConfigService(store).validate(
        0, {{"fusion", boost::json::object{{"mode", "edge_feather"}}}}, initial.view,
        sv::OutputProduct::FinalFrame);
    for (const auto &[key, value] : std::vector<std::pair<std::string, boost::json::value>>{
             {"base_config_revision", "1"},
             {"valid", false},
             {"applied", true},
             {"renderer_prepared", true},
             {"changes", boost::json::object{}},
             {"changes", boost::json::object{{"cameras", boost::json::object{}}}},
             {"changes", boost::json::object{{"fusion", false}}},
             {"schema_version", 2}})
    {
        auto bad = fields;
        bad[key] = value;
        EXPECT_ANY_THROW(sv::client::parse_configuration_validation(
            validation_reply(bad, "configuration_validation")));
    }
    auto bad = validation_reply(fields, "configuration_validation");
    bad.header["accepted"] = false;
    EXPECT_THROW(sv::client::parse_configuration_validation(bad), std::invalid_argument);
}
