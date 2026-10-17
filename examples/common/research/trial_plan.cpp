#include "trial_plan.hpp"
#include <algorithm>
#include <numeric>
#include <random>
#include <stdexcept>

namespace sv::research
{
namespace
{
uint32_t bounded(std::mt19937 &engine, uint32_t bound)
{
    const uint32_t threshold = (uint64_t(1) << 32) % bound;
    uint32_t word;
    do
    {
        word = static_cast<uint32_t>(engine());
    } while (word < threshold);
    return word % bound;
}
} // namespace

std::vector<Trial> make_trial_plan(const Scenario &scenario)
{
    const auto variants = scenario.variants.size();
    if (!variants || variants > 32 || !scenario.repeats || scenario.repeats > 1000 ||
        scenario.warmup > 100 || scenario.seed > 2147483647 || !scenario.frames ||
        scenario.frames > 256 ||
        uint64_t(variants) * (uint64_t(scenario.repeats) + scenario.warmup) * scenario.frames >
            10000)
    {
        throw std::invalid_argument("scenario trial budget exceeded");
    }
    std::vector<Trial> result;
    result.reserve(variants * (scenario.repeats + scenario.warmup) * scenario.frames);
    std::mt19937 engine(scenario.seed);
    for (unsigned frame = 0; frame < scenario.frames; ++frame)
    {
        for (unsigned block = 0; block < scenario.warmup + scenario.repeats; ++block)
        {
            std::vector<unsigned> order(variants);
            std::iota(order.begin(), order.end(), 0);
            for (size_t remaining = variants; remaining > 1; --remaining)
            {
                std::swap(order[remaining - 1], order[bounded(engine, remaining)]);
            }
            for (const auto variant : order)
            {
                result.push_back({frame, variant, block, block < scenario.warmup});
            }
        }
    }
    return result;
}

boost::json::array trial_plan_json(const std::vector<Trial> &plan)
{
    boost::json::array result;
    result.reserve(plan.size());
    for (size_t id = 0; id < plan.size(); ++id)
    {
        const auto &trial = plan[id];
        result.push_back(boost::json::object{{"trial_index", id},
                                             {"frame_index", trial.frame_index},
                                             {"variant", trial.variant},
                                             {"block", trial.block},
                                             {"warmup", trial.warmup}});
    }
    return result;
}
} // namespace sv::research
