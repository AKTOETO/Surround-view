#pragma once
#include "scenario.hpp"
#include <vector>

namespace sv::research::comparison
{
struct Evidence
{
    std::vector<boost::json::object> profiles;
    std::vector<bool> observed;
    boost::json::array frames;
    bool complete = false;
    bool identity = true;
};

bool equal(const boost::json::value &, const boost::json::value &);
unsigned integer(const boost::json::value &, unsigned maximum);
Evidence evidence(const boost::json::object &, const std::atomic_bool *);
void cancelled(const std::atomic_bool *);
} // namespace sv::research::comparison
