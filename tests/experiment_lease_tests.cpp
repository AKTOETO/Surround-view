#include "sv/experiment_lease.hpp"
#include <gtest/gtest.h>

TEST(ExperimentLease, OwnershipDeadlineRenewalAndBaseline)
{
    sv::ExperimentLease lease;
    auto config = std::make_shared<sv::Config>();
    config->fusion.mode = "angular_feather";
    std::string error;
    ASSERT_TRUE(lease.acquire("owner", config, true, 100, 250, error));
    const auto id = lease.id();
    EXPECT_TRUE(lease.baseline_paused());
    EXPECT_EQ(lease.baseline().fusion.mode, "angular_feather");
    EXPECT_FALSE(lease.acquire("other", config, false, 100, 250, error));
    EXPECT_FALSE(lease.allows("configure_fusion", "other", id, 100));
    EXPECT_FALSE(lease.allows("configure_fusion", "owner", "bad", 100));
    EXPECT_TRUE(lease.allows("configure_fusion", "owner", id, 100));
    EXPECT_TRUE(lease.allows("step", "owner", id, 100));
    EXPECT_FALSE(lease.allows("step", "other", id, 100));
    EXPECT_FALSE(lease.allows("step", "owner", "", 100));
    EXPECT_FALSE(lease.allows("apply_calibration", "owner", id, 100));
    EXPECT_FALSE(lease.allows("orbit", "owner", id, 100));
    EXPECT_TRUE(lease.allows("state", "other", "", 100));
    ASSERT_TRUE(lease.renew("owner", id, 200, error));
    EXPECT_FALSE(lease.expired(250000199));
    EXPECT_TRUE(lease.expired(250000200));
    EXPECT_FALSE(lease.renew("owner", id, 250000200, error));
    EXPECT_FALSE(lease.allows("pause", "owner", id, 250000200));
    EXPECT_FALSE(lease.allows("step", "owner", id, 250000200));
    lease.start_restore();
    EXPECT_FALSE(lease.allows("configure_fusion", "owner", id, 201));
    lease.complete();
    EXPECT_EQ(lease.state(), sv::ExperimentLease::State::Idle);
    EXPECT_FALSE(lease.allows("configure_fusion", "owner", id, 300000000));
    EXPECT_TRUE(lease.allows("configure_fusion", "owner", "", 300000000));
    ASSERT_TRUE(lease.acquire("owner", config, false, 300000000, 500, error));
    EXPECT_NE(lease.id(), id);
}

TEST(ExperimentLease, LimitsAndRestoreFailureDoNotUnlockMutations)
{
    sv::ExperimentLease lease;
    auto config = std::make_shared<sv::Config>();
    std::string error;
    EXPECT_FALSE(lease.acquire("", config, false, 0, 500, error));
    EXPECT_FALSE(lease.acquire("owner", nullptr, false, 0, 500, error));
    EXPECT_FALSE(lease.acquire("owner", config, false, 0, 249, error));
    EXPECT_FALSE(lease.acquire("owner", config, false, 0, 30001, error));
    ASSERT_TRUE(lease.acquire("owner", config, false, 0, 30000, error));
    lease.start_restore();
    lease.fail("restore failed");
    EXPECT_EQ(lease.status().at("state").as_string(), "failed");
    EXPECT_FALSE(lease.allows("configure_fusion", "owner", "", 1));
    EXPECT_FALSE(lease.acquire("owner", config, false, 1, 250, error));
    EXPECT_TRUE(lease.allows("state", "owner", "", 1));
}
