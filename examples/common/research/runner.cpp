#include "connection.hpp"
#include "digest.hpp"
#include "quality_metrics.hpp"
#include "quality_reference.hpp"
#include "scenario.hpp"
#include "trial_plan.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <thread>

namespace sv::research
{
namespace
{
boost::json::object settings(const client::FusionSettings &fusion)
{
    boost::json::object result{{"mode", fusion.mode},
                               {"diagnostic", fusion.diagnostic},
                               {"edge_width_px", fusion.edge_width_px},
                               {"angle_power", fusion.angle_power},
                               {"pyramid_levels", fusion.pyramid_levels},
                               {"smoothness_weight", fusion.smoothness_weight},
                               {"pyramid_boundary", fusion.pyramid_boundary}};
    if (fusion.seam_solver != "binary_pairs")
    {
        result["seam_solver"] = fusion.seam_solver;
    }
    if (fusion.fallback_display.mode != "legacy")
    {
        result["fallback_display"] = boost::json::object{{"mode", fusion.fallback_display.mode}};
    }
    return result;
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

boost::json::array summaries(const boost::json::array &samples, size_t variant_count,
                             std::optional<unsigned> frame_index = {})
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
                    sample.at("variant").to_number<size_t>() != variant ||
                    (frame_index && sample.at("frame_index").to_number<unsigned>() != *frame_index))
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
                        std::function<void(unsigned, unsigned)> progress,
                        std::function<std::string(const Sample &)> capture,
                        std::shared_ptr<const QualityReference> quality)
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
                                    {"seed", scenario.seed},
                                    {"frames", scenario.frames},
                                    {"capture_frames", scenario.capture_frames},
                                    {"reset_replay", scenario.reset_replay}};
    if (scenario.quality_reference)
    {
        description["quality_reference"] = *scenario.quality_reference;
    }
    // Also validate callers that construct Scenario directly (e.g. future GUI).
    (void)parse_scenario(description);
    const auto plan = make_trial_plan(scenario);
    const auto plan_json = trial_plan_json(plan);
    const auto serialized_plan = boost::json::serialize(plan_json);
    const auto serialized = boost::json::serialize(description);
    boost::json::object report{
        {"schema_version", 1},
        {"trial_order_algorithm", trial_order_algorithm},
        {"trial_plan", plan_json},
        {"trial_plan_sha256", sha256(serialized_plan.data(), serialized_plan.size())},
        {"scenario", description},
        {"scenario_sha256", sha256(serialized.data(), serialized.size())},
        {"experiment",
         scenario.frames == 1 ? "paused_frame_fusion_screen" : "paused_sequence_fusion_screen"},
        {"success", false},
        {"restore_required", false},
        {"restored", false},
        {"samples", boost::json::array{}},
        {"frame_baselines", boost::json::array{}},
        {"restore_scope", "fusion_surface_pause_only"},
        {"cursor_restored", false},
        {"limitation",
         "Sequential paused frame sets; external reference independence is declared, not verified; "
         "no "
         "cursor restoration, GPU cache reset, sustained FPS, or durable progress checkpoints"}};
    std::unique_ptr<Connection> connection;
    boost::json::object original;
    std::string revision, lease_id, restore_reference;
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
        if (scenario.capture_frames && !capture)
        {
            throw std::runtime_error("capture_frames requires a capture consumer");
        }
        if (scenario.quality_reference && !quality)
        {
            throw std::runtime_error("quality_reference requires a loaded evaluator");
        }
        if (quality)
        {
            quality->validate(scenario);
            report["quality_reference"] = quality->description();
        }
        connection = std::make_unique<Connection>(options);
        if (quality && !connection->supports("frame_view_v1"))
        {
            throw std::runtime_error("server lacks frame_view_v1");
        }
        if (quality && quality->requires_content_identity() &&
            !connection->supports("replay_input_identity_v1"))
        {
            throw std::runtime_error("server lacks replay_input_identity_v1");
        }
        const bool lease_step = connection->supports("experiment_step_v1");
        report["lease_step_supported"] = lease_step;
        if (scenario.frames > 1 && !lease_step)
        {
            throw std::runtime_error("server lacks experiment_step_v1");
        }
        if (scenario.reset_replay && !connection->supports("replay_reset_v1"))
        {
            throw std::runtime_error("server lacks replay_reset_v1");
        }
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
        const auto paused = accepted(*connection, scenario.reset_replay ? "replay_reset" : "pause",
                                     {{"lease_id", lease_id}});
        auto baseline = connection->frame(text(paused, "state_revision"));
        report["ready_preparation"] = scenario.reset_replay ? "reset_first_row" : "paused_current";
        if (baseline->header.at("health") != "READY" && lease_step && !scenario.reset_replay)
        {
            const auto stepped = accepted(*connection, "step", {{"lease_id", lease_id}});
            baseline = connection->frame(text(stepped, "state_revision"));
            report["ready_preparation"] = "step";
        }
        if (baseline->header.at("health") != "READY" ||
            baseline->header.at("source_type") != "replay")
        {
            throw std::runtime_error("research requires a READY replay frame set");
        }
        report["baseline"] = baseline->header;
        report["baseline_rgba_sha256"] = sha256(baseline->payload.data(), baseline->payload.size());
        unsigned completed = 0;
        size_t planned_index = 0;
        const unsigned total =
            scenario.variants.size() * (scenario.warmup + scenario.repeats) * scenario.frames;
        for (unsigned frame_index = 0; frame_index < scenario.frames; ++frame_index)
        {
            if (frame_index)
            {
                check_cancel();
                accepted(*connection, "experiment_renew", {{"lease_id", lease_id}});
                const auto state = accepted(*connection, "state");
                if (state.at("surface") != original.at("surface"))
                {
                    const auto surface_ack = accepted(*connection, "configure_surface",
                                                      {{"base_config_revision", revision},
                                                       {"surface", original.at("surface")},
                                                       {"lease_id", lease_id}});
                    revision = text(surface_ack, "config_revision");
                }
                const auto fusion_ack = accepted(*connection, "configure_fusion",
                                                 {{"base_config_revision", revision},
                                                  {"fusion", original.at("fusion")},
                                                  {"lease_id", lease_id}});
                revision = text(fusion_ack, "config_revision");
                const auto stepped = accepted(*connection, "step", {{"lease_id", lease_id}});
                auto next = connection->frame(text(stepped, "state_revision"));
                if (next->header.at("health") != "READY" ||
                    next->header.at("source_type") != "replay" ||
                    next->header.at("frame_set_id") == baseline->header.at("frame_set_id") ||
                    next->header.at("fusion") != original.at("fusion") ||
                    next->header.at("surface") != original.at("surface"))
                {
                    throw std::runtime_error("sequence step provenance mismatch");
                }
                baseline = std::move(next);
            }
            restore_reference = sha256(baseline->payload.data(), baseline->payload.size());
            report["last_baseline_rgba_sha256"] = restore_reference;
            report.at("frame_baselines")
                .as_array()
                .push_back(boost::json::object{{"frame_index", frame_index},
                                               {"metadata", baseline->header},
                                               {"rgba_sha256", restore_reference}});
            // Warmup blocks are also complete blocks; every measurement re-applies its profile.
            for (unsigned block = 0; block < scenario.warmup + scenario.repeats; ++block)
            {
                for (size_t slot = 0; slot < scenario.variants.size(); ++slot)
                {
                    const auto trial_index = planned_index++;
                    const auto index = plan.at(trial_index).variant;
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
                        // An omitted surface means the baseline, not the previous variant's
                        // carrier.
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
                    boost::json::object sample{
                        {"trial_index", trial_index},
                        {"frame_index", frame_index},
                        {"variant", index},
                        {"block", block},
                        {"warmup", block < scenario.warmup},
                        {"metadata", frame->header},
                        {"rgba_sha256", sha256(frame->payload.data(), frame->payload.size())}};
                    if (quality && !sample.at("warmup").as_bool())
                    {
                        sample["quality"] = quality->evaluate(
                            {frame_index, static_cast<unsigned>(index), block, false, frame});
                    }
                    if (scenario.capture_frames && !sample.at("warmup").as_bool())
                    {
                        sample["rgba_file"] = capture(
                            {frame_index, static_cast<unsigned>(index), block, false, frame});
                    }
                    report.at("samples").as_array().push_back(std::move(sample));
                    ++completed;
                    if (progress)
                    {
                        progress(completed, total);
                    }
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
            report["restored_rgba_sha256"] = sha256(frame->payload.data(), frame->payload.size());
            if (frame->header.at("fusion") != original.at("fusion") ||
                (!restore_reference.empty() &&
                 text(report, "restored_rgba_sha256") != restore_reference))
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
    boost::json::array frame_summaries;
    for (unsigned index = 0; index < report.at("frame_baselines").as_array().size(); ++index)
    {
        frame_summaries.push_back(
            boost::json::object{{"frame_index", index},
                                {"variants", summaries(report.at("samples").as_array(),
                                                       scenario.variants.size(), index)}});
    }
    report["frame_summaries"] = std::move(frame_summaries);
    if (quality)
    {
        try
        {
            report["quality_summaries"] =
                quality_summaries(report.at("samples").as_array(), scenario.variants.size());
        }
        catch (const std::exception &error)
        {
            report["quality_error"] = error.what();
            report["success"] = false;
        }
    }
    report["percentile_definition"] = "nearest rank; warmup excluded; per-frame timings, not FPS";
    return report;
}
} // namespace sv::research
