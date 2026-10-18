#include "report_evidence.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>
#include <stdexcept>
#include <tuple>

namespace sv::research::comparison
{
void cancelled(const std::atomic_bool *cancel)
{
    if (cancel && cancel->load())
    {
        throw std::runtime_error("cancelled");
    }
}

unsigned integer(const boost::json::value &value, unsigned maximum)
{
    if ((!value.is_int64() && !value.is_uint64()) || (value.is_int64() && value.as_int64() < 0) ||
        value.to_number<uint64_t>() > maximum)
    {
        throw std::invalid_argument("invalid report integer");
    }
    return value.to_number<unsigned>();
}

bool equal(const boost::json::value &left, const boost::json::value &right)
{
    if (left.is_number() && right.is_number())
    {
        return left.to_number<long double>() == right.to_number<long double>();
    }
    if (left.is_object() && right.is_object())
    {
        if (left.as_object().size() != right.as_object().size())
        {
            return false;
        }
        for (const auto &entry : left.as_object())
        {
            const auto *value = right.as_object().if_contains(entry.key());
            if (!value || !equal(entry.value(), *value))
            {
                return false;
            }
        }
        return true;
    }
    if (left.is_array() && right.is_array())
    {
        if (left.as_array().size() != right.as_array().size())
        {
            return false;
        }
        for (size_t i = 0; i < left.as_array().size(); ++i)
        {
            if (!equal(left.as_array()[i], right.as_array()[i]))
            {
                return false;
            }
        }
        return true;
    }
    return left == right;
}

namespace
{
bool digest(const boost::json::value &v)
{
    if (!v.is_string() || v.as_string().size() != 64)
    {
        return false;
    }
    for (auto c : v.as_string())
    {
        if (!(c >= '0' && c <= '9') && !(c >= 'a' && c <= 'f'))
        {
            return false;
        }
    }
    return true;
}

boost::json::object frame_identity(const boost::json::object &metadata)
{
    boost::json::object result;
    for (const auto *key : {"view", "width", "height", "pixel_format", "row_origin",
                            "output_product", "diagnostic_view"})
    {
        result[key] = metadata.at(key);
    }
    if (result.at("output_product") != "final_frame" || result.at("diagnostic_view") != "color")
    {
        throw std::invalid_argument("comparison requires final color frames");
    }
    if (metadata.at("source_type") != "replay" || metadata.at("health") != "READY" ||
        !metadata.at("paused").as_bool())
    {
        throw std::invalid_argument("comparison requires READY paused replay frames");
    }
    if (!integer(result.at("width"), 16384) || !integer(result.at("height"), 16384) ||
        !result.at("view").is_object())
    {
        throw std::invalid_argument("invalid output shape/view");
    }
    if (result.at("pixel_format") != "RGBA8" || result.at("row_origin") != "top_left")
    {
        throw std::invalid_argument("invalid output pixel layout");
    }
    boost::json::array inputs;
    const auto &view = result.at("view").as_object();
    for (const auto *key : {"azimuth_rad", "elevation_rad", "distance_m", "fov_y_rad"})
    {
        if (!view.at(key).is_number() || !std::isfinite(view.at(key).to_number<double>()))
        {
            throw std::invalid_argument("invalid view scalar");
        }
    }
    for (const auto *key : {"target_m", "clip_m"})
    {
        const auto &values = view.at(key).as_array();
        if (values.size() != (std::string_view(key) == "target_m" ? 3U : 2U))
        {
            throw std::invalid_argument("invalid view vector");
        }
        for (const auto &v : values)
        {
            if (!v.is_number() || !std::isfinite(v.to_number<double>()))
            {
                throw std::invalid_argument("invalid view coordinate");
            }
        }
    }
    const auto &clip = view.at("clip_m").as_array();
    if (view.at("distance_m").to_number<double>() <= 0 ||
        view.at("fov_y_rad").to_number<double>() <= 0 ||
        view.at("fov_y_rad").to_number<double>() >= 3.141592653589793 ||
        clip[0].to_number<double>() <= 0 ||
        clip[1].to_number<double>() <= clip[0].to_number<double>())
    {
        throw std::invalid_argument("invalid view bounds");
    }
    const auto &source = metadata.at("inputs").as_array();
    if (source.size() != 4)
    {
        throw std::invalid_argument("comparison requires four input identities");
    }
    std::set<unsigned> ids;
    for (const auto &entry : source)
    {
        const auto &camera = entry.as_object();
        const auto id = integer(camera.at("camera_id"), 3);
        if (!ids.insert(id).second || !camera.at("used").as_bool())
        {
            throw std::invalid_argument("invalid camera input");
        }
        const auto &image = camera.at("image_identity").as_object();
        if (image.at("algorithm") != "packed_rgb8_sha256_v1" || !digest(image.at("sha256")) ||
            !digest(camera.at("camera_model_sha256")) || image.at("pixel_format") != "RGB8" ||
            image.at("row_origin") != "top_left")
        {
            throw std::invalid_argument("missing content/model identity");
        }
        if (!integer(image.at("width"), 16384) || !integer(image.at("height"), 16384) ||
            !camera.at("calibration_id").is_string() ||
            camera.at("calibration_id").as_string().empty() ||
            camera.at("calibration_id").as_string().size() > 512)
        {
            throw std::invalid_argument("invalid input dimensions/calibration label");
        }
        const auto &timestamp = camera.at("source_timestamp_ns").as_string();
        uint64_t parsed = 0;
        const auto converted =
            std::from_chars(timestamp.data(), timestamp.data() + timestamp.size(), parsed);
        if (timestamp.empty() || converted.ec != std::errc{} ||
            converted.ptr != timestamp.data() + timestamp.size())
        {
            throw std::invalid_argument("invalid input timestamp");
        }
        inputs.push_back(
            boost::json::object{{"camera_id", id},
                                {"calibration_id", camera.at("calibration_id")},
                                {"source_timestamp_ns", camera.at("source_timestamp_ns")},
                                {"image_identity", image},
                                {"camera_model_sha256", camera.at("camera_model_sha256")}});
    }
    std::sort(inputs.begin(), inputs.end(),
              [](const auto &a, const auto &b)
              {
                  return a.as_object().at("camera_id").template to_number<unsigned>() <
                         b.as_object().at("camera_id").template to_number<unsigned>();
              });
    result["inputs"] = std::move(inputs);
    return result;
}
} // namespace

Evidence evidence(const boost::json::object &report, const std::atomic_bool *cancel)
{
    if (integer(report.at("schema_version"), 1) != 1 || !report.at("experiment").is_string())
    {
        throw std::invalid_argument("unsupported individual research report");
    }
    if (report.at("experiment") != "paused_frame_fusion_screen" &&
        report.at("experiment") != "paused_sequence_fusion_screen")
    {
        throw std::invalid_argument("unsupported report experiment");
    }
    const auto &description = report.at("scenario").as_object();
    const auto scenario = parse_scenario(description);
    const auto &variants = description.at("variants").as_array();
    const auto &samples = report.at("samples").as_array();
    if (samples.size() > 10000)
    {
        throw std::invalid_argument("too many report samples");
    }
    Evidence result;
    for (const auto &value : variants)
    {
        auto fusion = value.as_object();
        auto *surface = fusion.if_contains("surface");
        boost::json::value actual_surface = surface ? *surface : boost::json::value();
        fusion.erase("surface");
        if (actual_surface.is_null())
        {
            if (const auto *initial = report.if_contains("initial_state");
                initial && initial->is_object())
            {
                if (const auto *s = initial->as_object().if_contains("surface"))
                {
                    actual_surface = *s;
                }
            }
        }
        result.profiles.push_back({{"fusion", fusion}, {"surface", actual_surface}});
        result.observed.push_back(false);
    }
    const auto total = variants.size() * scenario.frames * (scenario.warmup + scenario.repeats);
    result.complete = report.at("success").as_bool() && report.at("restored").as_bool() &&
                      samples.size() == total;
    if (const auto *acceptance = report.if_contains("quality_acceptance"))
    {
        const auto &status = acceptance->as_object().at("status");
        if (status != "passed" && status != "failed" && status != "not_requested")
        {
            result.complete = false;
        }
    }
    std::set<std::tuple<unsigned, unsigned, unsigned>> slots;
    unsigned trial = 0;
    for (const auto &value : samples)
    {
        cancelled(cancel);
        const auto &sample = value.as_object();
        if (integer(sample.at("trial_index"), 9999) != trial++)
        {
            throw std::invalid_argument("invalid trial index sequence");
        }
        const auto index = integer(sample.at("variant"), unsigned(variants.size() - 1));
        const auto frame = integer(sample.at("frame_index"), scenario.frames - 1);
        const auto block = integer(sample.at("block"), scenario.warmup + scenario.repeats - 1);
        if (sample.at("warmup").as_bool() != (block < scenario.warmup) ||
            !slots.emplace(frame, block, index).second)
        {
            throw std::invalid_argument("duplicate or invalid report sample coordinate");
        }
        const auto &metadata = sample.at("metadata").as_object();
        if (const auto *fusion = metadata.if_contains("fusion");
            fusion && fusion->is_object() && metadata.contains("surface"))
        {
            const boost::json::object profile{{"fusion", *fusion},
                                              {"surface", metadata.at("surface")}};
            if (result.observed[index] && !equal(result.profiles[index], profile))
            {
                result.identity = false;
            }
            const auto &declared = variants[index].as_object();
            for (const auto &field : declared)
            {
                const auto *actual = std::string_view(field.key()) == "surface"
                                         ? metadata.if_contains("surface")
                                         : fusion->as_object().if_contains(field.key());
                if (!actual || !equal(field.value(), *actual))
                {
                    result.identity = false;
                }
            }
            result.profiles[index] = profile;
            result.observed[index] = true;
        }
        else
        {
            result.identity = false;
        }
    }
    const auto *baselines = report.if_contains("frame_baselines");
    if (!baselines || !baselines->is_array() || baselines->as_array().size() != scenario.frames)
    {
        result.identity = false;
    }
    else
    {
        for (size_t i = 0; i < baselines->as_array().size(); ++i)
        {
            try
            {
                const auto &baseline = baselines->as_array()[i].as_object();
                if (integer(baseline.at("frame_index"), scenario.frames - 1) != i)
                {
                    throw std::invalid_argument("frame order");
                }
                result.frames.push_back(frame_identity(baseline.at("metadata").as_object()));
            }
            catch (const std::exception &)
            {
                result.identity = false;
            }
        }
    }
    for (const auto &value : samples)
    {
        cancelled(cancel);
        const auto &sample = value.as_object();
        try
        {
            const auto frame = sample.at("frame_index").to_number<unsigned>();
            if (frame >= result.frames.size() ||
                !equal(frame_identity(sample.at("metadata").as_object()), result.frames[frame]))
            {
                result.identity = false;
            }
        }
        catch (const std::exception &)
        {
            result.identity = false;
        }
    }
    return result;
}
} // namespace sv::research::comparison
