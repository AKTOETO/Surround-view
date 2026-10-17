#pragma once
#include "scenario.hpp"

namespace sv::research
{
inline constexpr const char *trial_order_algorithm = "mt19937_fisher_yates_reject_v1";

struct Trial
{
    unsigned frame_index, variant, block;
    bool warmup;
};

// Validated, bounded plan. Does not connect to the server or mutate the scenario.
std::vector<Trial> make_trial_plan(const Scenario &);
boost::json::array trial_plan_json(const std::vector<Trial> &);
boost::json::array expand_variant_matrix(const boost::json::object &);
} // namespace sv::research
