#pragma once
#include <boost/json.hpp>

// Applies axes only to relevant fusion families; shares the common deterministic expander.
boost::json::array researchFusionVariants(const boost::json::object &form);
