#include "command_parameters.hpp"
#include <gtest/gtest.h>
#include <limits>

namespace
{
using sv::command_parameters::camera_index;
using sv::command_parameters::finite_number;

TEST(CommandParameters, GeometryAcceptsSignedUnsignedAndDouble)
{
    for (const auto &value :
         {boost::json::value(int64_t(1)), boost::json::value(uint64_t(1)), boost::json::value(1.0)})
    {
        EXPECT_DOUBLE_EQ(finite_number(value), 1.0);
    }
    EXPECT_DOUBLE_EQ(finite_number(int64_t(-7)), -7);
    EXPECT_DOUBLE_EQ(finite_number(uint64_t(0)), 0);
    EXPECT_DOUBLE_EQ(finite_number(-.25), -.25);
    // Geometry deliberately converts to double; exact IDs do not use this path.
    EXPECT_DOUBLE_EQ(finite_number(std::numeric_limits<uint64_t>::max()),
                     static_cast<double>(std::numeric_limits<uint64_t>::max()));
}

TEST(CommandParameters, GeometryRejectsOtherTypesAndNonfiniteValues)
{
    for (const auto &value :
         {boost::json::value(true), boost::json::value(false), boost::json::value("1"),
          boost::json::value(nullptr), boost::json::value(boost::json::array{}),
          boost::json::value(boost::json::object{}),
          boost::json::value(std::numeric_limits<double>::infinity()),
          boost::json::value(-std::numeric_limits<double>::infinity()),
          boost::json::value(std::numeric_limits<double>::quiet_NaN())})
    {
        EXPECT_THROW(finite_number(value), std::invalid_argument);
    }
}

TEST(CommandParameters, CameraIndexChecksRangeBeforeNarrowing)
{
    for (int64_t index = 0; index < 4; ++index)
    {
        EXPECT_EQ(camera_index(index), index);
        EXPECT_EQ(camera_index(static_cast<uint64_t>(index)), index);
    }
    for (const auto &value :
         {boost::json::value(int64_t(-1)), boost::json::value(int64_t(4)),
          boost::json::value(int64_t(1) << 32), boost::json::value((int64_t(1) << 32) + 1),
          boost::json::value(std::numeric_limits<int64_t>::max()),
          boost::json::value(std::numeric_limits<uint64_t>::max()), boost::json::value(true),
          boost::json::value(0.0), boost::json::value(.5), boost::json::value("0"),
          boost::json::value(nullptr)})
    {
        EXPECT_THROW(camera_index(value), std::invalid_argument);
    }
}
} // namespace
