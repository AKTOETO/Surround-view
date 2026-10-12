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
