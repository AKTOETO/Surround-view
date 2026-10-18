#pragma once
#include <boost/json.hpp>

// Applies axes only to relevant fusion families; shares the common deterministic expander.
boost::json::array researchFusionVariants(const boost::json::object &form);

// Explicit carrier profiles cross every applicable fusion variant.
boost::json::array researchCarrierVariants(const boost::json::array &fusion,
                                           const boost::json::value &profiles);
