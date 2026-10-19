#include "sv/build_passport.hpp"
#include <gtest/gtest.h>

TEST(BuildPassport, ConfiguredDescriptorIsBoundedVerifiedAndOrderIndependent)
{
    const auto claim = sv::configured_build_passport();
    ASSERT_EQ(claim.at("policy"), "configured_build_v1");
    EXPECT_EQ(sv::verified_build_passport(claim), claim);
    EXPECT_LT(boost::json::serialize(claim).size(), 17000U);
    auto descriptor = claim.at("descriptor").as_object();
    boost::json::object reversed;
    for (auto it = descriptor.rbegin(); it != descriptor.rend(); ++it)
    {
        reversed[it->key()] = it->value();
    }
    EXPECT_EQ(sv::make_build_passport(reversed), claim);
    EXPECT_TRUE(descriptor.at("features").as_object().at("SV_GPU").is_bool());
    EXPECT_GT(descriptor.at("headers").as_object().at("glm").to_number<int>(), 0);
}

TEST(BuildPassport, CompilerFlagsConfigurationAndDependenciesChangeIdentity)
{
    const auto original = sv::configured_build_passport();
    for (const auto &field : {std::pair{"configuration", "name"},
                              {"configuration", "cxx_flags"},
                              {"toolchain", "compiler_version"},
                              {"dependencies", "opencv_configured_version"},
                              {"target", "sdk_id"}})
    {
        auto descriptor = original.at("descriptor").as_object();
        descriptor.at(field.first).as_object()[field.second] = "other";
        const auto updated = sv::make_build_passport(descriptor);
        EXPECT_NE(updated.at("sha256"), original.at("sha256"));
        EXPECT_EQ(sv::verified_build_passport(updated), updated);
        auto tampered = original;
        tampered["descriptor"] = descriptor;
        EXPECT_TRUE(sv::verified_build_passport(tampered).is_null());
    }
}

TEST(BuildPassport, LegacyFutureUnknownFieldsTypesAndLimitsDoNotVerify)
{
    const auto original = sv::configured_build_passport();
    EXPECT_TRUE(sv::verified_build_passport(nullptr).is_null());
    auto value = original;
    value["schema_version"] = 2;
    EXPECT_TRUE(sv::verified_build_passport(value).is_null());
    value = original;
    value["extra"] = true;
    EXPECT_TRUE(sv::verified_build_passport(value).is_null());
    value = original;
    value["sha256"] = std::string(64, '0');
    EXPECT_TRUE(sv::verified_build_passport(value).is_null());
    auto descriptor = original.at("descriptor").as_object();
    descriptor.at("features").as_object()["SV_GPU"] = 1;
    EXPECT_ANY_THROW(sv::make_build_passport(descriptor));
    descriptor = original.at("descriptor").as_object();
    descriptor.at("configuration").as_object()["cxx_flags"] = std::string(8193, 'x');
    EXPECT_ANY_THROW(sv::make_build_passport(descriptor));
    descriptor = original.at("descriptor").as_object();
    descriptor["unknown"] = true;
    EXPECT_ANY_THROW(sv::make_build_passport(descriptor));
}
