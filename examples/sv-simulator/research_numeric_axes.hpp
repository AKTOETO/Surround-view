#pragma once
#include <boost/json.hpp>
#include <string_view>

// Validate all requested axes; attach only parameters used by the selected color method.
void researchNumericAxes(const boost::json::object &form, std::string_view mode,
                         boost::json::object &axes);
void validateResearchNumericCatalog(const boost::json::object &variant,
                                    const boost::json::object &catalog);
