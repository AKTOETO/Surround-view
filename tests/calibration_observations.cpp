#include "sv/calibration_job.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace
{
struct ObservationFixture : testing::Test
{
    std::vector<sv::Vec3> train, validation;
    std::vector<sv::Pixel> train_uv, validation_uv;
    sv::CalibrationProvenance provenance;

    void SetUp() override
    {
        provenance.dataset_id = "test-dataset";
        for (int i = 0; i < 6; ++i)
        {
            train.push_back({double(i), 0., 4.});
            validation.push_back({double(i), 1., 4.});
            train_uv.push_back({double(i), 2., true});
            validation_uv.push_back({double(i), 3., true});
            provenance.training.observation_ids.push_back("train-" + std::to_string(i));
            provenance.validation.observation_ids.push_back("validation-" + std::to_string(i));
            provenance.training.frame_ids.push_back("training-frame");
            provenance.validation.frame_ids.push_back("validation-frame");
        }
    }

    sv::CalibrationSplitAudit validate()
    {
        return sv::validate_calibration_observations(train, train_uv, validation, validation_uv,
                                                     provenance);
    }
};

TEST_F(ObservationFixture, DistinctFramesPermitManyCornersPerFrame)
{
    const auto audit = validate();
    EXPECT_EQ(audit.dataset_id, "test-dataset");
    EXPECT_EQ(audit.training_observations, 6u);
    EXPECT_EQ(audit.validation_observations, 6u);
    EXPECT_EQ(audit.training_frames, 1u);
    EXPECT_EQ(audit.validation_frames, 1u);
}

TEST_F(ObservationFixture, SameMetricTargetInDifferentFramesIsAllowed)
{
    validation = train;
    EXPECT_NO_THROW(validate()); // Different UV and frame identity, not a duplicate observation.
}

TEST_F(ObservationFixture, DuplicateIdWithinSplitRejected)
{
    provenance.training.observation_ids[1] = provenance.training.observation_ids[0];
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, DuplicateIdAcrossSplitsRejectedEvenWithChangedCoordinates)
{
    provenance.validation.observation_ids[0] = provenance.training.observation_ids[3];
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, SameFrameWithDifferentCornerIdsRejected)
{
    provenance.validation.frame_ids[2] = provenance.training.frame_ids[0];
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, RenamedExactCopyStillRejected)
{
    validation[4] = train[1];
    validation_uv[4] = train_uv[1];
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, RepeatedContentWithinSplitRejected)
{
    train[1] = train[0];
    train_uv[1] = train_uv[0];
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, MissingAndInvalidMetadataRejected)
{
    provenance.validation.frame_ids.pop_back();
    EXPECT_THROW(validate(), std::invalid_argument);
    provenance.validation.frame_ids.push_back("contains space");
    EXPECT_THROW(validate(), std::invalid_argument);
    provenance.validation.frame_ids.back() = std::string(129, 'a');
    EXPECT_THROW(validate(), std::invalid_argument);
    provenance.validation.frame_ids.back() = "frame";
    provenance.dataset_id.clear();
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, InvalidPixelsAndNonfiniteCoordinatesRejected)
{
    train_uv[0].valid = false;
    EXPECT_THROW(validate(), std::invalid_argument);
    train_uv[0].valid = true;
    train[0].x = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROW(validate(), std::invalid_argument);
    train[0].x = 0.;
    validation_uv[0].v = std::numeric_limits<double>::infinity();
    EXPECT_THROW(validate(), std::invalid_argument);
}

TEST_F(ObservationFixture, ManagerRejectsBeforeEnqueueAndIdAllocation)
{
    sv::CalibrationJobManager manager(
        [](const auto &camera, const auto &, const auto &, const auto &)
        { return sv::ExtrinsicCalibration{camera, 6, 0.}; });
    sv::Camera camera;
    camera.fx = camera.fy = 100.;
    camera.width = camera.height = 640;
    auto invalid = provenance;
    invalid.validation.frame_ids[0] = invalid.training.frame_ids[0];
    EXPECT_THROW(manager.submit_job("owner", 0, 0, camera, train, train_uv, validation,
                                    validation_uv, {}, invalid),
                 std::invalid_argument);
    EXPECT_EQ(manager.submit_job("owner", 0, 0, camera, train, train_uv, validation, validation_uv,
                                 {}, provenance),
              "calib-job-1");
}

TEST(CalibrationProvenance, MalformedJsonRejected)
{
    EXPECT_THROW(sv::parse_calibration_provenance(boost::json::object{}), std::exception);
    EXPECT_THROW(sv::parse_calibration_provenance(boost::json::object{
                     {"dataset_id", "test"}, {"training", 1}, {"validation", 2}}),
                 std::exception);
}
} // namespace
