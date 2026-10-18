#pragma once
#include "observation_policy.hpp"
#include "trial_plan.hpp"
#include <boost/json.hpp>

namespace comparison_fixture
{
inline boost::json::object report()
{
    using namespace boost::json;
    const object view{{"azimuth_rad", 0.0},         {"elevation_rad", 1.0},
                      {"distance_m", 8.0},          {"fov_y_rad", 1.0},
                      {"clip_m", array{.1, 100.0}}, {"target_m", array{0, 0, 0}}};
    array inputs, guards;
    for (unsigned i = 0; i < 4; ++i)
    {
        const object image{{"algorithm", "packed_rgb8_sha256_v1"},
                           {"sha256", std::string(64, char('0' + i))},
                           {"width", 8},
                           {"height", 8},
                           {"pixel_format", "RGB8"},
                           {"row_origin", "top_left"}};
        object camera{{"camera_id", i},
                      {"calibration_id", "fixture"},
                      {"source_timestamp_ns", "0"},
                      {"image_identity", image},
                      {"camera_model_sha256", std::string(64, 'a')}};
        guards.push_back(camera);
        camera["used"] = true;
        inputs.push_back(camera);
    }
    const object surface{{"type", "dome_floor_v1"}, {"dome_radius_m", 12}};
    object metadata{{"view", view},
                    {"width", 8},
                    {"height", 8},
                    {"pixel_format", "RGBA8"},
                    {"row_origin", "top_left"},
                    {"output_product", "final_frame"},
                    {"diagnostic_view", "color"},
                    {"inputs", inputs},
                    {"source_type", "replay"},
                    {"paused", true},
                    {"health", "READY"},
                    {"surface", surface}};
    array variants, samples, summaries, qualities;
    for (unsigned i = 0; i < 2; ++i)
    {
        const object fusion{{"mode", i ? "hard_best_angle" : "edge_feather"},
                            {"diagnostic", "color"}};
        variants.push_back(fusion);
        metadata["fusion"] = fusion;
        samples.push_back(object{{"variant", i},
                                 {"frame_index", 0},
                                 {"block", 0},
                                 {"warmup", false},
                                 {"trial_index", i},
                                 {"metadata", metadata}});
        summaries.push_back(object{
            {"variant", i},
            {"timings", object{{"render_wall",
                                object{{"count", 1}, {"p50_ms", 1.0 + i}, {"p95_ms", 2.0 + i}}}}}});
        qualities.push_back(object{{"variant", i},
                                   {"frames", 1},
                                   {"selected_pixels", 1},
                                   {"pixel_weighted_mae_rgb8", .000001},
                                   {"pixel_weighted_rmse_rgb8", .001},
                                   {"pixel_weighted_psnr_db", 108.13},
                                   {"exact_match", false}});
    }
    auto observations = sv::research::observation_policy(false, true);
    observations["telemetry_before"] =
        object{{"enabled", false}, {"period_ms", 100}, {"history_capacity", 128}};
    observations["telemetry_after"] = observations.at("telemetry_before");
    observations["telemetry_stable"] = true;
    return {{"schema_version", 1},
            {"observation_policy", observations},
            {"experiment", "paused_frame_fusion_screen"},
            {"success", true},
            {"restored", true},
            {"scenario", object{{"schema_version", 1},
                                {"frames", 1},
                                {"warmup", 0},
                                {"repeats", 1},
                                {"seed", 17},
                                {"reset_replay", true},
                                {"capture_frames", false},
                                {"variants", variants}}},
            {"trial_order_algorithm", sv::research::trial_order_algorithm},
            {"percentile_definition", "nearest rank; warmup excluded; per-frame timings, not FPS"},
            {"samples", samples},
            {"summaries", summaries},
            {"quality_summaries", qualities},
            {"frame_baselines", array{object{{"frame_index", 0}, {"metadata", metadata}}}},
            {"initial_state", object{{"surface", surface}}},
            {"catalog", object{{"backend", "fixture"},
                               {"gl_vendor", "fixture"},
                               {"gl_renderer", "fixture"},
                               {"source_revision", "fixture"},
                               {"source_fingerprint", std::string(64, 'c')}}},
            {"quality_acceptance", object{{"status", "not_requested"}}},
            {"quality_reference",
             object{{"schema_version", 2},
                    {"algorithm", "masked_rgb8_v1"},
                    {"kind", "regression"},
                    {"width", 8},
                    {"height", 8},
                    {"view", view},
                    {"frames", array{object{{"inputs", guards},
                                            {"selected_pixels", 1},
                                            {"rgb_pixels_sha256", std::string(64, 'b')},
                                            {"mask_pixels_sha256", std::string(64, 'd')}}}}}}};
}
} // namespace comparison_fixture
