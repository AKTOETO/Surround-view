#include "connection.hpp"
#include "scenario.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <numeric>
#include <openssl/sha.h>
#include <random>
#include <stdexcept>
#include <thread>

namespace sv::research
{
namespace
{
std::string hash(const void *data, size_t size)
{
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(static_cast<const unsigned char *>(data), size, digest);
    std::string result;
    constexpr char digits[] = "0123456789abcdef";
    for (auto byte : digest)
    {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}

boost::json::object settings(const client::FusionSettings &fusion)
{
    return {{"mode", fusion.mode},
            {"diagnostic", fusion.diagnostic},
            {"edge_width_px", fusion.edge_width_px},
            {"angle_power", fusion.angle_power},
            {"pyramid_levels", fusion.pyramid_levels},
            {"smoothness_weight", fusion.smoothness_weight}};
}

boost::json::object accepted(Connection &connection, std::string operation,
                             boost::json::object parameters = {})
{
    auto ack = connection.request(std::move(operation), std::move(parameters));
    if (!ack.at("accepted").as_bool())
    {
        throw std::runtime_error("server rejected: " + std::string(ack.at("reason").as_string()));
    }
    return ack;
}

std::string text(const boost::json::object &object, const char *key)
{
    return std::string(object.at(key).as_string());
}

boost::json::array summaries(const boost::json::array &samples, size_t variant_count)
{
    boost::json::array result;
    for (size_t variant = 0; variant < variant_count; ++variant)
    {
        boost::json::object metrics;
        for (const auto *metric : {"render_wall", "gpu_draw", "upload_cpu", "readback_copy_cpu",
                                   "fusion_cpu", "layer_readback_cpu"})
        {
            std::vector<double> values;
            for (const auto &value : samples)
            {
                const auto &sample = value.as_object();
                if (sample.at("warmup").as_bool() ||
                    sample.at("variant").to_number<size_t>() != variant)
                {
                    continue;
                }
                const auto &spans =
                    sample.at("metadata").as_object().at("pipeline_spans_ms").as_object();
                const auto *timing = spans.if_contains(metric);
                if (timing && timing->is_number())
                {
                    values.push_back(timing->to_number<double>());
                }
            }
            if (values.empty())
            {
                metrics[metric] = nullptr;
                continue;
            }
            std::sort(values.begin(), values.end());
            metrics[metric] = boost::json::object{
                {"count", values.size()},
                {"p50_ms", values[static_cast<size_t>(std::ceil(.5 * values.size())) - 1]},
                {"p95_ms", values[static_cast<size_t>(std::ceil(.95 * values.size())) - 1]}};
        }
        result.push_back(boost::json::object{{"variant", variant}, {"timings", metrics}});
    }
    return result;
}
} // namespace

boost::json::object run(const Scenario &scenario, const client::Options &options,
                        const std::atomic_bool *cancel,
                        std::function<void(unsigned, unsigned)> progress)
{
    boost::json::array variants;
    for (const auto &variant : scenario.variants)
    {
        auto description = settings(variant.fusion);
        if (variant.surface)
        {
            description["surface"] = *variant.surface;
        }
        variants.push_back(std::move(description));
    }
    boost::json::object description{{"schema_version", 1},
                                    {"variants", variants},
                                    {"repeats", scenario.repeats},
                                    {"warmup", scenario.warmup},
                                    {"seed", scenario.seed}};
    // Also validate callers that construct Scenario directly (e.g. future GUI).
    (void)parse_scenario(description);
    const auto serialized = boost::json::serialize(description);
    boost::json::object report{{"schema_version", 1},
                               {"scenario", description},
                               {"scenario_sha256", hash(serialized.data(), serialized.size())},
                               {"experiment", "paused_frame_fusion_screen"},
                               {"success", false},
                               {"restore_required", false},
                               {"restored", false},
                               {"samples", boost::json::array{}},
                               {"limitation",
                                "One paused frame set; no independent quality truth, "
                                "temporal reset, sustained FPS, or durable progress checkpoints"}};
    std::unique_ptr<Connection> connection;
    boost::json::object original;
    std::string revision, lease_id;
    auto check_cancel = [&]
    {
        if (cancel && cancel->load())
        {
            throw std::runtime_error("cancelled");
        }
    };
    try
    {
        check_cancel();
        connection = std::make_unique<Connection>(options);
        const auto catalog = accepted(*connection, "fusion_catalog");
        report["catalog"] = catalog.at("fusion_catalog");
        original = accepted(*connection, "state");
        report["initial_state"] = original;
        if (original.at("source_type").as_string() != "replay")
        {
            throw std::runtime_error("research requires replay source");
        }
        const auto acquired = accepted(*connection, "experiment_acquire", {{"ttl_ms", 30000}});
        lease_id = text(acquired.at("experiment_lease").as_object(), "lease_id");
        report["lease_id"] = lease_id;
        revision = text(acquired, "config_revision");
        report["restore_required"] = true;
        const auto paused = accepted(*connection, "pause", {{"lease_id", lease_id}});
        const auto baseline = connection->frame(text(paused, "state_revision"));
        if (baseline->header.at("health") != "READY" ||
            baseline->header.at("source_type") != "replay")
        {
            throw std::runtime_error("research requires a READY replay frame set");
        }
        report["baseline"] = baseline->header;
        report["baseline_rgba_sha256"] = hash(baseline->payload.data(), baseline->payload.size());
        std::mt19937 random(scenario.seed);
        unsigned completed = 0;
        const unsigned total = scenario.variants.size() * (scenario.warmup + scenario.repeats);
        // Warmup blocks are also complete blocks; every measurement re-applies its profile.
        for (unsigned block = 0; block < scenario.warmup + scenario.repeats; ++block)
        {
            std::vector<size_t> order(scenario.variants.size());
            std::iota(order.begin(), order.end(), 0);
            std::shuffle(order.begin(), order.end(), random);
            for (const auto index : order)
            {
                check_cancel();
                accepted(*connection, "experiment_renew", {{"lease_id", lease_id}});
                const auto &variant = scenario.variants[index];
                if (variant.surface)
                {
                    const auto surface_ack = accepted(*connection, "configure_surface",
                                                      {{"base_config_revision", revision},
                                                       {"surface", *variant.surface},
                                                       {"lease_id", lease_id}});
                    revision = text(surface_ack, "config_revision");
                }
                else
                {
                    // An omitted surface means the baseline, not the previous variant's carrier.
                    const auto state = accepted(*connection, "state");
                    if (state.at("surface") != original.at("surface"))
                    {
                        const auto surface_ack = accepted(*connection, "configure_surface",
                                                          {{"base_config_revision", revision},
                                                           {"surface", original.at("surface")},
                                                           {"lease_id", lease_id}});
                        revision = text(surface_ack, "config_revision");
                    }
                }
                const auto fusion = settings(variant.fusion);
                auto ack = accepted(*connection, "configure_fusion",
                                    {{"base_config_revision", revision},
                                     {"fusion", fusion},
                                     {"lease_id", lease_id}});
                revision = text(ack, "config_revision");
                auto frame = connection->frame(text(ack, "state_revision"));
                if (text(frame->header, "config_revision") != revision ||
                    frame->header.at("fusion") != fusion ||
                    frame->header.at("surface") != (variant.surface
                                                        ? boost::json::value(*variant.surface)
                                                        : original.at("surface")) ||
                    frame->header.at("inputs") != baseline->header.at("inputs") ||
                    frame->header.at("frame_set_id") != baseline->header.at("frame_set_id") ||
                    frame->header.at("health") != "READY")
                {
                    throw std::runtime_error("trial provenance mismatch");
                }
                report.at("samples").as_array().push_back(boost::json::object{
                    {"variant", index},
                    {"block", block},
                    {"warmup", block < scenario.warmup},
                    {"metadata", frame->header},
                    {"rgba_sha256", hash(frame->payload.data(), frame->payload.size())}});
                ++completed;
                if (progress)
                {
                    progress(completed, total);
                }
            }
        }
        report["success"] = true;
    }
    catch (const std::exception &error)
    {
        report["error"] = error.what();
    }
    if (report.at("restore_required").as_bool())
    {
        try
        {
            accepted(*connection, "experiment_renew", {{"lease_id", lease_id}});
            const auto state_before_restore = accepted(*connection, "state");
            if (state_before_restore.at("surface") != original.at("surface"))
            {
                const auto surface_ack = accepted(*connection, "configure_surface",
                                                  {{"base_config_revision", revision},
                                                   {"surface", original.at("surface")},
                                                   {"lease_id", lease_id}});
                revision = text(surface_ack, "config_revision");
            }
            // Revision check avoids overwriting an unexpected concurrent config change.
            const auto restored = accepted(*connection, "configure_fusion",
                                           {{"base_config_revision", revision},
                                            {"fusion", original.at("fusion")},
                                            {"lease_id", lease_id}});
            auto frame = connection->frame(text(restored, "state_revision"));
            report["restored_frame"] = frame->header;
            report["restored_rgba_sha256"] = hash(frame->payload.data(), frame->payload.size());
            if (frame->header.at("fusion") != original.at("fusion") ||
                (report.contains("baseline_rgba_sha256") &&
                 report.at("restored_rgba_sha256") != report.at("baseline_rgba_sha256")))
            {
                throw std::runtime_error("restore output mismatch");
            }
            accepted(*connection, "experiment_release", {{"lease_id", lease_id}});
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(options.timeout_ms);
            while (true)
            {
                const auto state = accepted(*connection, "state");
                const auto &status = state.at("experiment_lease").as_object();
                if (status.at("state").as_string() == "idle")
                {
                    if (state.at("fusion") != original.at("fusion") ||
                        state.at("surface") != original.at("surface") ||
                        state.at("paused") != original.at("paused"))
                    {
                        throw std::runtime_error("lease restore state mismatch");
                    }
                    report["final_state"] = state;
                    break;
                }
                if (status.at("state").as_string() == "failed" ||
                    std::chrono::steady_clock::now() >= deadline)
                {
                    throw std::runtime_error("lease restore failed or timed out");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            report["restored"] = true;
        }
        catch (const std::exception &error)
        {
            report["restore_error"] = error.what();
            report["success"] = false;
        }
    }
    report["summaries"] = summaries(report.at("samples").as_array(), scenario.variants.size());
    report["percentile_definition"] = "nearest rank; warmup excluded; per-frame timings, not FPS";
    return report;
}
} // namespace sv::research
