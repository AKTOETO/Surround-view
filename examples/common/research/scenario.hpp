#pragma once
#include "sv/client.hpp"
#include <atomic>
#include <functional>
#include <vector>

namespace sv::research
{
struct Scenario
{
    std::vector<client::FusionSettings> variants;
    unsigned repeats = 3, warmup = 2, seed = 1;
};

Scenario parse_scenario(const boost::json::value &);
// Blocking application service. Run outside a GUI thread; cancel is optional.
boost::json::object run(const Scenario &, const client::Options &,
                        const std::atomic_bool *cancel = nullptr,
                        std::function<void(unsigned, unsigned)> progress = {});
} // namespace sv::research
