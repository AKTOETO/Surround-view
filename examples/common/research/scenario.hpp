#pragma once
#include "sv/client.hpp"
#include <atomic>
#include <functional>
#include <optional>
#include <vector>

namespace sv::research
{
struct Variant
{
    client::FusionSettings fusion;
    std::optional<boost::json::object> surface;
};

struct Scenario
{
    std::vector<Variant> variants;
    unsigned repeats = 3, warmup = 2, seed = 1, frames = 1;
    bool capture_frames = false;
};

struct Sample
{
    unsigned frame_index, variant, block;
    bool warmup;
    std::shared_ptr<const Message> frame;
};

Scenario parse_scenario(const boost::json::value &);
// Blocking application service. Run outside a GUI thread; cancel is optional.
boost::json::object run(const Scenario &, const client::Options &,
                        const std::atomic_bool *cancel = nullptr,
                        std::function<void(unsigned, unsigned)> progress = {},
                        std::function<std::string(const Sample &)> capture = {});
} // namespace sv::research
