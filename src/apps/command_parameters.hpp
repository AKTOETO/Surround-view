#pragma once
#include <boost/json.hpp>
#include <cmath>
#include <stdexcept>

namespace sv::command_parameters
{
// Geometry is real-valued; accept every JSON numeric representation, but never
// coerce booleans/strings. IDs and counters must use their own exact parsers.
inline double finite_number(const boost::json::value &value)
{
    const double result = value.is_double()  ? value.as_double()
                          : value.is_int64() ? static_cast<double>(value.as_int64())
                          : value.is_uint64()
                              ? static_cast<double>(value.as_uint64())
                              : throw std::invalid_argument("command_number_required");
    if (!std::isfinite(result))
    {
        throw std::invalid_argument("command_number_nonfinite");
    }
    return result;
}

inline int camera_index(const boost::json::value &value)
{
    if (!value.is_int64() && !value.is_uint64())
    {
        throw std::invalid_argument("camera_id_integer_required");
    }
    if (value.is_int64())
    {
        const auto index = value.as_int64();
        if (index < 0 || index > 3)
        {
            throw std::invalid_argument("camera_id_out_of_range");
        }
        return static_cast<int>(index);
    }
    const auto index = value.as_uint64();
    if (index > 3)
    {
        throw std::invalid_argument("camera_id_out_of_range");
    }
    return static_cast<int>(index);
}
} // namespace sv::command_parameters
