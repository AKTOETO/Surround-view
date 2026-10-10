#include "scenario.hpp"
#include <cmath>
#include <stdexcept>

namespace sv::research
{
namespace
{
void keys(const boost::json::object &object, std::initializer_list<std::string_view> allowed)
{
    for (const auto &field : object)
    {
        bool found = false;
        for (const auto key : allowed)
        {
            found = found || field.key() == key;
        }
        if (!found)
        {
            throw std::invalid_argument("unknown scenario field: " + std::string(field.key()));
        }
    }
}

unsigned integer(const boost::json::value &value, unsigned low, unsigned high)
{
    const auto n = value.to_number<int64_t>();
    if ((!value.is_int64() && !value.is_uint64()) || n < low || n > high)
    {
        throw std::invalid_argument("scenario integer out of range");
    }
    return static_cast<unsigned>(n);
}

double positive(const boost::json::value &value, double high)
{
    const auto n = value.to_number<double>();
    if (!std::isfinite(n) || n <= 0 || n > high)
    {
        throw std::invalid_argument("scenario fusion parameter out of range");
    }
    return n;
}
} // namespace

Scenario parse_scenario(const boost::json::value &value)
{
    const auto &object = value.as_object();
    keys(object,
         {"schema_version", "variants", "repeats", "warmup", "seed", "frames", "capture_frames"});
    if (integer(object.at("schema_version"), 1, 1) != 1)
    {
        throw std::invalid_argument("scenario schema version");
    }
    Scenario result;
    if (auto v = object.if_contains("frames"))
    {
        result.frames = integer(*v, 1, 256);
    }
    if (auto v = object.if_contains("capture_frames"))
    {
        result.capture_frames = v->as_bool();
    }
    if (auto v = object.if_contains("repeats"))
    {
        result.repeats = integer(*v, 1, 1000);
    }
    if (auto v = object.if_contains("warmup"))
    {
        result.warmup = integer(*v, 0, 100);
    }
    if (auto v = object.if_contains("seed"))
    {
        result.seed = integer(*v, 0, 2147483647);
    }
    const auto &variants = object.at("variants").as_array();
    if (variants.empty() || variants.size() > 32 ||
        variants.size() * (result.repeats + result.warmup) * result.frames > 10000)
    {
        throw std::invalid_argument("scenario trial budget exceeded");
    }
    for (const auto &variant : variants)
    {
        const auto &fields = variant.as_object();
        keys(fields, {"mode", "diagnostic", "edge_width_px", "angle_power", "pyramid_levels",
                      "smoothness_weight", "pyramid_boundary", "surface"});
        client::FusionSettings fusion;
        fusion.mode = std::string(fields.at("mode").as_string());
        if (fusion.mode != "edge_feather" && fusion.mode != "hard_best_angle" &&
            fusion.mode != "angular_feather" && fusion.mode != "seam_distance_feather" &&
            fusion.mode != "graph_cut_seam" && fusion.mode != "multi_band" &&
            fusion.mode != "graph_cut_multi_band")
        {
            throw std::invalid_argument("unsupported scenario fusion mode");
        }
        if (auto v = fields.if_contains("diagnostic"))
        {
            fusion.diagnostic = std::string(v->as_string());
        }
        if (fusion.diagnostic != "color" && fusion.diagnostic != "coverage" &&
            fusion.diagnostic != "weights")
        {
            throw std::invalid_argument("unsupported scenario diagnostic");
        }
        if (auto v = fields.if_contains("edge_width_px"))
        {
            fusion.edge_width_px = positive(*v, 4096);
        }
        if (auto v = fields.if_contains("angle_power"))
        {
            fusion.angle_power = positive(*v, 32);
        }
        if (auto v = fields.if_contains("pyramid_levels"))
        {
            fusion.pyramid_levels = integer(*v, 1, 8);
        }
        if (auto v = fields.if_contains("smoothness_weight"))
        {
            fusion.smoothness_weight = v->to_number<double>();
            if (!std::isfinite(fusion.smoothness_weight) || fusion.smoothness_weight < 0 ||
                fusion.smoothness_weight > 100)
            {
                throw std::invalid_argument("scenario smoothness out of range");
            }
        }
        if (auto v = fields.if_contains("pyramid_boundary"))
        {
            fusion.pyramid_boundary = std::string(v->as_string());
        }
        if (fusion.pyramid_boundary != "zero" && fusion.pyramid_boundary != "normalized")
        {
            throw std::invalid_argument("unsupported pyramid boundary");
        }
        Variant parsed_variant{std::move(fusion), std::nullopt};
        if (auto v = fields.if_contains("surface"))
        {
            parsed_variant.surface = v->as_object();
        }
        result.variants.push_back(std::move(parsed_variant));
    }
    return result;
}
} // namespace sv::research
