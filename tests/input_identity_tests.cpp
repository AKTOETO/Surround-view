#include "sv/input_identity.hpp"
#include <gtest/gtest.h>
#include <limits>

TEST(InputIdentity, KnownPackedRgbBytesAndDimensions)
{
    sv::Image image{2, 2, 3, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    const auto original = sv::rgb8_image_sha256(image);
    EXPECT_EQ(original, "3b472f66fd05e50a689264bd30bec89a798411442085f0a9a2cb153283aa136e");
    image.pixels.back() ^= 1;
    EXPECT_NE(sv::rgb8_image_sha256(image), original);
    image.pixels.pop_back();
    EXPECT_THROW(sv::rgb8_image_sha256(image), std::invalid_argument);
    EXPECT_THROW(sv::rgb8_image_sha256({2, 2, 4, std::vector<unsigned char>(16)}),
                 std::invalid_argument);
    sv::Image reshaped{2, 4, 3, std::vector<unsigned char>(24)};
    const auto first_shape = sv::rgb8_image_sha256(reshaped);
    reshaped.width = 4;
    reshaped.height = 2;
    EXPECT_NE(first_shape, sv::rgb8_image_sha256(reshaped));
}

TEST(InputIdentity, CameraKnownAnswerAndNumericChangesWithoutRenamingCalibration)
{
    sv::Camera camera;
    camera.width = camera.height = 2;
    camera.fx = camera.fy = 2;
    camera.cx = camera.cy = .5;
    const auto original = sv::camera_model_sha256(camera);
    EXPECT_EQ(original, "00dc4666fa5739db5c942b6c871f97d8b288871ead42ba37f261c4b6a32998ee");
    camera.name = "cosmetic rename";
    camera.calibration_id = "separate metadata guard";
    EXPECT_EQ(original, sv::camera_model_sha256(camera));
    camera.T[1] = -0.0;
    EXPECT_EQ(original, sv::camera_model_sha256(camera));
    for (auto *parameter :
         {&camera.fx, &camera.k[3], &camera.T[3], &camera.theta_max, &camera.z_epsilon})
    {
        const auto old = *parameter;
        *parameter += .01;
        EXPECT_NE(original, sv::camera_model_sha256(camera));
        *parameter = old;
    }
    camera.fx = std::numeric_limits<double>::infinity();
    EXPECT_THROW(sv::camera_model_sha256(camera), std::invalid_argument);
}
