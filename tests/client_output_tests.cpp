#include "sv/client_output.hpp"
#include <gtest/gtest.h>

namespace
{
sv::Message support_message()
{
    boost::json::array inputs;
    for (int camera = 0; camera < 4; ++camera)
    {
        inputs.emplace_back(boost::json::object{{"camera_id", camera},
                                                {"used", true},
                                                {"calibration_id", "test"},
                                                {"sequence_id", "3"},
                                                {"release_timestamp_ns", "7"}});
    }
    return {11,
            {{"output_product", "render_support_v1"},
             {"support_schema", sv::render_support_schema()},
             {"session_id", "s"},
             {"buffer_token", "t"},
             {"width", 2},
             {"height", 2},
             {"stride_bytes", 8},
             {"pixel_format", "RGBA8"},
             {"row_origin", "top_left"},
             {"diagnostic_view", "color"},
             {"frame_id", "9"},
             {"frame_set_id", "3"},
             {"state_revision", "2"},
             {"config_revision", "1"},
             {"applied_command_id", "4"},
             {"inputs", inputs}},
            {0, 0, 0, 0, 4, 1, 4, 255, 1, 0, 6, 255, 0, 0, 2, 255}};
}
} // namespace

TEST(ClientOutput, LegacyAndExplicitFinalFramesRemainCompatible)
{
    sv::Message message;
    EXPECT_EQ(sv::client::validate_output_product(message), sv::OutputProduct::FinalFrame);
    message.header["output_product"] = "final_frame";
    EXPECT_EQ(sv::client::validate_output_product(message), sv::OutputProduct::FinalFrame);
    message.header["support_schema"] = sv::render_support_schema();
    EXPECT_THROW(sv::client::validate_output_product(message), std::invalid_argument);
}

TEST(ClientOutput, OwningViewPreservesPayloadAndProvenance)
{
    auto message = std::make_shared<sv::Message>(support_message());
    const auto *data = message->payload.data();
    sv::client::RenderSupportView view(message);
    message.reset();
    EXPECT_EQ(view.width(), 2);
    EXPECT_EQ(view.height(), 2);
    EXPECT_EQ(view.message().payload.data(), data);
    EXPECT_EQ(view.message().header.at("config_revision").as_string(), "1");
    EXPECT_FALSE(view.at(0, 0).geometry);
    EXPECT_EQ(view.at(1, 0).eligible_cameras, 4);
    EXPECT_EQ(view.at(1, 0).positive_local_weights, 1);
    EXPECT_EQ(view.at(1, 0).path, sv::RenderSupportPath::gpu_fusion);
    EXPECT_EQ(view.at(0, 1).path, sv::RenderSupportPath::hybrid_fusion);
    EXPECT_EQ(view.at(0, 1).positive_local_weights, 0);
    EXPECT_THROW(view.at(-1, 0), std::out_of_range);
    EXPECT_THROW(view.at(2, 0), std::out_of_range);
    EXPECT_THROW(view.at(0, 2), std::out_of_range);
    EXPECT_THROW(sv::client::RenderSupportView(nullptr), std::invalid_argument);
}

TEST(ClientOutput, AcceptsThresholdFallbackWithPositiveWeightsAndHybridZeroWeights)
{
    auto message = support_message();
    message.payload = {4, 4, 3, 255, 4, 0, 6, 255, 0, 0, 5, 255, 0, 0, 1, 255};
    EXPECT_NO_THROW(sv::client::validate_output_product(message));
}

TEST(ClientOutput, RejectsMalformedMetadataAndProvenance)
{
    const std::vector<std::pair<std::string, boost::json::value>> cases{
        {"output_product", "unknown_v2"},
        {"output_product", 1},
        {"support_schema", boost::json::object{}},
        {"width", -1},
        {"height", 2161},
        {"stride_bytes", 7},
        {"pixel_format", "RGB8"},
        {"row_origin", "bottom_left"},
        {"session_id", ""},
        {"buffer_token", ""},
        {"frame_id", "-1"},
        {"config_revision", "invalid"},
        {"applied_command_id", "18446744073709551616"},
        {"diagnostic_view", "coverage"},
        {"inputs", boost::json::array{}}};
    for (const auto &[key, value] : cases)
    {
        SCOPED_TRACE(key);
        auto message = support_message();
        message.header[key] = value;
        EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    }
    auto message = support_message();
    message.payload.pop_back();
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message = support_message();
    message.type = 21;
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    for (const auto *key : {"config_revision", "inputs", "support_schema"})
    {
        message = support_message();
        message.header.erase(key);
        EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    }
    message = support_message();
    message.header.at("inputs").as_array()[1].as_object()["camera_id"] = 0;
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message = support_message();
    message.header.at("inputs").as_array()[1].as_object()["camera_id"] = 4294967297ULL;
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
}

TEST(ClientOutput, RejectsInvalidCountsPathsAndDomains)
{
    for (const std::vector<unsigned char> &pixel : {std::vector<unsigned char>{5, 1, 4, 255},
                                                    {1, 2, 4, 255},
                                                    {0, 0, 7, 255},
                                                    {0, 0, 0, 255},
                                                    {1, 0, 1, 255},
                                                    {0, 0, 4, 255},
                                                    {1, 0, 4, 255},
                                                    {0, 0, 6, 255},
                                                    {1, 0, 5, 255},
                                                    {0, 0, 2, 0}})
    {
        auto message = support_message();
        std::copy(pixel.begin(), pixel.end(), message.payload.begin());
        EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    }
    EXPECT_THROW(sv::parse_output_product("unsupported"), std::invalid_argument);
    EXPECT_THROW(sv::output_product_name(static_cast<sv::OutputProduct>(99)),
                 std::invalid_argument);
}

namespace
{
sv::Message visibility_message()
{
    auto message = support_message();
    message.header["output_product"] = "source_visibility_v1";
    message.header.erase("support_schema");
    message.header["source_visibility_schema"] = sv::source_visibility_schema();
    message.payload = {0, 0, 0, 0, 1, 10, 11, 12, 7, 8, 9, 2, 6, 6, 6, 6};
    return message;
}
} // namespace

TEST(ClientOutput, VisibilityViewOwnsFourCameraStatusesAndChecksBounds)
{
    auto message = std::make_shared<sv::Message>(visibility_message());
    const auto *bytes = message->payload.data();
    sv::client::SourceVisibilityView view(message);
    message.reset();
    EXPECT_EQ(view.width(), 2);
    EXPECT_EQ(view.height(), 2);
    EXPECT_EQ(view.message().payload.data(), bytes);
    EXPECT_EQ(view.at(1, 0, 1), sv::SourceVisibilityStatus::accepted_ambiguous);
    EXPECT_EQ(view.at(1, 0, 2), sv::SourceVisibilityStatus::accepted_clear);
    EXPECT_EQ(view.at(1, 0, 3), sv::SourceVisibilityStatus::footprint_occluder);
    EXPECT_TRUE(sv::source_visibility_eligible(view.at(1, 0, 1)));
    EXPECT_FALSE(sv::source_visibility_eligible(view.at(1, 0, 3)));
    EXPECT_THROW(view.at(0, 0, 4), std::out_of_range);
    EXPECT_THROW(view.at(-1, 0, 0), std::out_of_range);
    EXPECT_THROW(view.at(0, 2, 0), std::out_of_range);
    EXPECT_THROW(sv::client::SourceVisibilityView(nullptr), std::invalid_argument);
    EXPECT_THROW(sv::client::RenderSupportView(std::make_shared<sv::Message>(view.message())),
                 std::invalid_argument);
}

TEST(ClientOutput, VisibilityRejectsSchemaUnknownStatusesAndMissingCameraClaims)
{
    for (int byte : {13, 255, 0, 6})
    {
        auto message = visibility_message();
        message.payload[4] = byte;
        EXPECT_THROW(sv::client::validate_output_product(message), std::invalid_argument);
    }
    auto message = visibility_message();
    message.header["source_visibility_schema"] = boost::json::object{};
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message = visibility_message();
    message.header["support_schema"] = sv::render_support_schema();
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message = visibility_message();
    message.payload.pop_back();
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message = visibility_message();
    message.header.at("inputs").as_array()[1].as_object()["used"] = false;
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
    message.payload[5] = message.payload[9] = 2;
    EXPECT_NO_THROW(sv::client::validate_output_product(message));
    message = visibility_message();
    message.header["output_product"] = "final_frame";
    EXPECT_THROW(sv::client::validate_output_product(message), std::exception);
}

namespace
{
sv::Message disagreement_message()
{
    auto m = support_message();
    m.header["output_product"] = "source_disagreement_v1";
    m.header.erase("support_schema");
    m.header["source_disagreement_schema"] = sv::source_disagreement_schema();
    m.payload = {0, 0, 0, 0, 0, 1, 0, 255, 0, 2, 255, 255, 192, 4, 255, 255};
    return m;
}
} // namespace

TEST(ClientOutput, DisagreementOwnsUndefinedAndDefinedZeroWithBounds)
{
    auto message = std::make_shared<sv::Message>(disagreement_message());
    const auto *payload = message->payload.data();
    sv::client::SourceDisagreementView view(message);
    message.reset();
    EXPECT_EQ(view.message().payload.data(), payload);
    EXPECT_EQ(view.width(), 2);
    EXPECT_EQ(view.height(), 2);
    EXPECT_FALSE(view.at(0, 0).carrier);
    EXPECT_FALSE(view.at(1, 0).defined);
    EXPECT_EQ(view.at(1, 0).eligible_cameras, 1);
    EXPECT_TRUE(view.at(0, 1).defined);
    EXPECT_EQ(view.at(0, 1).encoded_rgb_range, 0);
    EXPECT_EQ(view.at(1, 1).encoded_rgb_range, 192);
    EXPECT_THROW(view.at(2, 0), std::out_of_range);
    EXPECT_THROW(view.at(-1, 0), std::out_of_range);
    EXPECT_THROW(sv::client::SourceDisagreementView(nullptr), std::invalid_argument);
}

TEST(ClientOutput, DisagreementRejectsMalformedPixelsSchemasAndMissingInputs)
{
    for (const auto &pixel : {std::vector<unsigned char>{1, 1, 0, 255},
                              {0, 5, 255, 255},
                              {0, 2, 0, 255},
                              {0, 1, 255, 255},
                              {0, 2, 255, 0},
                              {0, 0, 0, 1}})
    {
        auto m = disagreement_message();
        std::copy(pixel.begin(), pixel.end(), m.payload.begin());
        EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
    }
    auto m = disagreement_message();
    m.header["source_visibility_schema"] = sv::source_visibility_schema();
    EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
    m = disagreement_message();
    m.header["source_disagreement_schema"] = boost::json::object{};
    EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
    m = disagreement_message();
    m.header["inputs"].as_array()[0].as_object()["used"] = false;
    EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
    m = disagreement_message();
    m.payload.pop_back();
    EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
    m = disagreement_message();
    m.header["output_product"] = "final_frame";
    EXPECT_THROW(sv::client::validate_output_product(m), std::invalid_argument);
}
