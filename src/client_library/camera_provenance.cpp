#include "camera_provenance.hpp"
#include "sv/client_output.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace sv::client
{
namespace
{
unsigned integer(const boost::json::value &value, unsigned min, unsigned max)
{
    if ((!value.is_int64() && !value.is_uint64()) || (value.is_int64() && value.as_int64() < 0))
    {
        throw std::invalid_argument("camera integer required");
    }
    const auto number = value.to_number<uint64_t>();
    if (number < min || number > max)
    {
        throw std::invalid_argument("camera integer out of range");
    }
    return unsigned(number);
}

uint64_t identity(const boost::json::object &value, const char *key)
{
    return parse_decimal_u64(std::string(value.at(key).as_string()));
}
} // namespace

std::optional<CameraCaptureMode> parse_camera_mode(const boost::json::value &value)
{
    if (value.is_null())
    {
        return {};
    }
    const auto &fields = value.as_object();
    CameraCaptureMode result;
    result.width = integer(fields.at("width"), 1, 16384);
    result.height = integer(fields.at("height"), 1, 16384);
    result.open_attempt = identity(fields, "open_attempt");
    if (!result.open_attempt)
    {
        throw std::invalid_argument("zero camera attempt");
    }
    auto read_properties = [](const boost::json::object &fields, auto &target, unsigned max_buffers)
    {
        if (!fields.at("fps").is_null())
        {
            const auto &fps = fields.at("fps");
            if (!fps.is_number() || !std::isfinite(fps.to_number<double>()) ||
                fps.to_number<double>() <= 0)
            {
                throw std::invalid_argument("invalid camera FPS");
            }
            target.fps = fps.to_number<double>();
        }
        if (!fields.at("fourcc").is_null())
        {
            target.fourcc = std::string(fields.at("fourcc").as_string());
            if (target.fourcc->size() != 4 ||
                !std::all_of(target.fourcc->begin(), target.fourcc->end(),
                             [](unsigned char c) { return c >= 32 && c <= 126; }))
            {
                throw std::invalid_argument("invalid camera FOURCC");
            }
        }
        if (!fields.at("buffer_count").is_null())
        {
            target.buffer_count = integer(fields.at("buffer_count"), 1, max_buffers);
        }
    };
    read_properties(fields, result, 32);
    if (const auto *automatic = fields.if_contains("automatic_properties");
        automatic && !automatic->is_null())
    {
        CameraAutomaticProperties properties;
        read_properties(automatic->as_object(), properties, std::numeric_limits<uint32_t>::max());
        if ((result.fps && properties.fps) || (result.fourcc && properties.fourcc) ||
            (result.buffer_count && properties.buffer_count))
        {
            throw std::invalid_argument("camera property is both explicit and automatic");
        }
        result.automatic_properties = std::move(properties);
    }
    return result;
}

CameraInputProvenance parse_camera_input(const boost::json::object &input)
{
    CameraInputProvenance result;
    result.camera_id = int(integer(input.at("camera_id"), 0, 3));
    result.source_sequence = identity(input, "source_sequence_id");
    const auto &capture = input.at("camera_capture").as_object();
    result.capture.open_attempt = identity(capture, "open_attempt");
    result.capture.negotiated_mode = parse_camera_mode(capture.at("negotiated_mode"));
    if (!result.capture.open_attempt ||
        (result.capture.negotiated_mode &&
         result.capture.negotiated_mode->open_attempt != result.capture.open_attempt))
    {
        throw std::invalid_argument("inconsistent frame capture identity");
    }
    return result;
}

std::vector<CameraInputProvenance> parse_selected_camera_inputs(const boost::json::value &value)
{
    const auto &inputs = value.as_array();
    if (inputs.size() > 4)
    {
        throw std::invalid_argument("too many selected camera inputs");
    }
    std::array<bool, 4> seen{};
    std::vector<CameraInputProvenance> result;
    for (const auto &input : inputs)
    {
        auto parsed = parse_camera_input(input.as_object());
        if (seen[parsed.camera_id])
        {
            throw std::invalid_argument("duplicate selected camera input");
        }
        seen[parsed.camera_id] = true;
        result.push_back(std::move(parsed));
    }
    return result;
}

std::vector<CameraInputProvenance> parse_frame_camera_inputs(const Message &message)
{
    if (message.type != 11)
    {
        throw std::invalid_argument("camera provenance requires frame");
    }
    const auto &inputs = message.header.at("inputs").as_array();
    if (inputs.size() != 4)
    {
        throw std::invalid_argument("four frame inputs required");
    }
    std::array<bool, 4> seen{};
    std::vector<CameraInputProvenance> result;
    for (const auto &entry : inputs)
    {
        const auto &input = entry.as_object();
        const auto id = integer(input.at("camera_id"), 0, 3);
        if (seen[id])
        {
            throw std::invalid_argument("duplicate frame camera");
        }
        seen[id] = true;
        const bool used = input.at("used").as_bool();
        if (const auto *capture = input.if_contains("camera_capture");
            capture && !capture->is_null())
        {
            const auto *source = message.header.if_contains("source_type");
            if (!used || !source || source->as_string() != "camera")
            {
                throw std::invalid_argument("camera provenance on unused or non-camera input");
            }
            result.push_back(parse_camera_input(input));
        }
    }
    return result;
}
} // namespace sv::client
