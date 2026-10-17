#include "quality_reference.hpp"
#include "digest.hpp"
#include "quality_metrics.hpp"
#include "sv/frame.hpp"
#include "sv/input_identity.hpp"
#include "sv/json.hpp"
#include "sv/protocol.hpp"
#include <cmath>
#include <fstream>
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
        for (auto key : allowed)
        {
            found = found || field.key() == key;
        }
        if (!found)
        {
            throw std::invalid_argument("unknown quality reference field: " +
                                        std::string(field.key()));
        }
    }
}

unsigned integer(const boost::json::value &value, unsigned low, unsigned high)
{
    if ((!value.is_int64() && !value.is_uint64()) || value.to_number<int64_t>() < low ||
        value.to_number<int64_t>() > high)
    {
        throw std::invalid_argument("quality reference integer out of range");
    }
    return value.to_number<unsigned>();
}

std::string string(const boost::json::value &value, size_t maximum = 4096)
{
    const auto &text = value.as_string();
    if (text.empty() || text.size() > maximum || text.find('\0') != boost::json::string::npos)
    {
        throw std::invalid_argument("invalid quality reference string");
    }
    return std::string(text);
}

void digest_string(const boost::json::value &value)
{
    const auto text = string(value, 64);
    if (text.size() != 64 || text.find_first_not_of("0123456789abcdef") != std::string::npos)
    {
        throw std::invalid_argument("quality input digest must be 64 lowercase hex characters");
    }
}

void validate_identity(const boost::json::object &object)
{
    keys(object, {"algorithm", "sha256", "width", "height", "pixel_format", "row_origin"});
    if (object.at("algorithm") != sv::image_identity_algorithm ||
        object.at("pixel_format") != "RGB8" || object.at("row_origin") != "top_left")
    {
        throw std::invalid_argument("unsupported quality input image identity");
    }
    (void)integer(object.at("width"), 2, 4096);
    (void)integer(object.at("height"), 2, 2160);
    digest_string(object.at("sha256"));
}

std::string read_manifest(const std::filesystem::path &path)
{
    constexpr size_t limit = 1024 * 1024;
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("cannot open quality reference manifest");
    }
    std::string text(limit + 1, '\0');
    file.read(text.data(), text.size());
    text.resize(static_cast<size_t>(file.gcount()));
    if (file.bad() || text.size() > limit)
    {
        throw std::runtime_error("quality reference manifest exceeds 1 MiB or read failed");
    }
    return text;
}
} // namespace

struct QualityReference::Impl
{
    struct FrameReference
    {
        Image rgb, mask;
        boost::json::array inputs;
    };

    unsigned version = 1, width = 0, height = 0;
    boost::json::object view, description;
    std::vector<FrameReference> frames;
    std::optional<QualityCriteria> criteria;
};

QualityReference::QualityReference(const std::filesystem::path &manifest)
    : impl_(std::make_unique<Impl>())
{
    const auto text = read_manifest(manifest);
    const auto document = sv::parse_json(text);
    const auto &object = document.as_object();
    keys(object,
         {"schema_version", "kind", "dataset_id", "width", "height", "view", "frames", "criteria"});
    impl_->version = integer(object.at("schema_version"), 1, 2);
    if (const auto *criteria = object.if_contains("criteria"))
    {
        impl_->criteria = parse_quality_criteria(criteria->as_object());
    }
    const auto kind = string(object.at("kind"), 64);
    if (kind != "independent_ground_truth" && kind != "regression")
    {
        throw std::invalid_argument(
            "quality reference kind must be independent_ground_truth or regression");
    }
    impl_->width = integer(object.at("width"), 2, 4096);
    impl_->height = integer(object.at("height"), 2, 2160);
    impl_->view = object.at("view").as_object();
    keys(impl_->view,
         {"azimuth_rad", "elevation_rad", "distance_m", "fov_y_rad", "clip_m", "target_m"});
    for (const char *key : {"azimuth_rad", "elevation_rad", "distance_m", "fov_y_rad"})
    {
        if (!std::isfinite(impl_->view.at(key).to_number<double>()))
        {
            throw std::invalid_argument("non-finite quality reference view");
        }
        impl_->view[key] = impl_->view.at(key).to_number<double>();
    }
    for (const char *key : {"clip_m", "target_m"})
    {
        auto &coordinates = impl_->view.at(key).as_array();
        if (coordinates.size() != (std::string_view(key) == "clip_m" ? 2U : 3U))
        {
            throw std::invalid_argument("quality reference view vector length");
        }
        for (auto &coordinate : coordinates)
        {
            if (!std::isfinite(coordinate.to_number<double>()))
            {
                throw std::invalid_argument("non-finite quality reference view coordinate");
            }
            coordinate = coordinate.to_number<double>();
        }
    }
    const auto &clips = impl_->view.at("clip_m").as_array();
    const auto fov = impl_->view.at("fov_y_rad").to_number<double>();
    if (!(impl_->view.at("distance_m").to_number<double>() > 0 && fov > 0 &&
          fov < 3.141592653589793 && clips[0].to_number<double>() > 0 &&
          clips[1].to_number<double>() > clips[0].to_number<double>()))
    {
        throw std::invalid_argument("invalid quality reference view projection");
    }
    const auto &frames = object.at("frames").as_array();
    if (frames.empty() || frames.size() > 256 ||
        uint64_t(impl_->width) * impl_->height * 6 * frames.size() > 128 * 1024 * 1024)
    {
        throw std::invalid_argument(
            "quality references exceed frame count or 128 MiB pixel budget");
    }
    impl_->description = {
        {"schema_version", impl_->version},
        {"algorithm", "masked_rgb8_v1"},
        {"kind", kind},
        {"dataset_id", string(object.at("dataset_id"), 128)},
        {"manifest_sha256", sha256(text.data(), text.size())},
        {"width", impl_->width},
        {"height", impl_->height},
        {"view", impl_->view},
        {"frames", boost::json::array{}},
        {"independence", "declared by reference author; not verified"},
        {"input_guard", impl_->version == 2
                            ? "decoded RGB8 content and numeric camera model, IDs and timestamps"
                            : "legacy metadata only: calibration IDs and scenario timestamps"}};
    if (impl_->criteria)
    {
        impl_->description["criteria"] = quality_criteria_json(*impl_->criteria);
    }
    for (const auto &value : frames)
    {
        const auto &fields = value.as_object();
        keys(fields, {"rgb", "mask", "inputs"});
        Impl::FrameReference reference;
        reference.inputs = fields.at("inputs").as_array();
        if (reference.inputs.size() != 4)
        {
            throw std::invalid_argument("quality reference requires four ordered input guards");
        }
        for (unsigned camera = 0; camera < 4; ++camera)
        {
            const auto &guard = reference.inputs[camera].as_object();
            if (impl_->version == 2)
            {
                keys(guard, {"camera_id", "calibration_id", "source_timestamp_ns", "image_identity",
                             "camera_model_sha256"});
                validate_identity(guard.at("image_identity").as_object());
                digest_string(guard.at("camera_model_sha256"));
            }
            else
            {
                keys(guard, {"camera_id", "calibration_id", "source_timestamp_ns"});
            }
            (void)integer(guard.at("camera_id"), camera, camera);
            (void)string(guard.at("calibration_id"), 256);
            const auto stamp = string(guard.at("source_timestamp_ns"), 20);
            if (std::to_string(sv::parse_decimal_u64(stamp)) != stamp)
            {
                throw std::invalid_argument("quality timestamp must be canonical decimal");
            }
        }
        const auto rgb_path = string(fields.at("rgb"));
        const auto mask_path = string(fields.at("mask"));
        reference.rgb = read_ppm(manifest.parent_path() / rgb_path);
        reference.mask = read_ppm(manifest.parent_path() / mask_path);
        for (const auto *image : {&reference.rgb, &reference.mask})
        {
            if (image->width != int(impl_->width) || image->height != int(impl_->height))
            {
                throw std::invalid_argument(
                    "quality reference image dimensions differ from manifest");
            }
        }
        // Validate every mask before connecting, without allocating an extra RGBA image.
        uint64_t selected = 0;
        for (size_t i = 0; i < reference.mask.pixels.size(); i += 3)
        {
            const auto m = reference.mask.pixels[i];
            if ((m != 0 && m != 255) || reference.mask.pixels[i + 1] != m ||
                reference.mask.pixels[i + 2] != m)
            {
                throw std::invalid_argument("quality mask must be binary RGB (0 or 255)");
            }
            selected += m != 0;
        }
        if (!selected)
        {
            throw std::invalid_argument("quality mask selects no pixels");
        }
        impl_->description.at("frames").as_array().push_back(boost::json::object{
            {"rgb", rgb_path},
            {"mask", mask_path},
            {"inputs", reference.inputs},
            {"selected_pixels", selected},
            {"rgb_pixels_sha256", sha256(reference.rgb.pixels.data(), reference.rgb.pixels.size())},
            {"mask_pixels_sha256",
             sha256(reference.mask.pixels.data(), reference.mask.pixels.size())}});
        impl_->frames.push_back(std::move(reference));
    }
}

QualityReference::~QualityReference() = default;

bool QualityReference::requires_content_identity() const
{
    return impl_->version == 2;
}

const std::optional<QualityCriteria> &QualityReference::criteria() const
{
    return impl_->criteria;
}

void QualityReference::validate(const Scenario &scenario) const
{
    if (!scenario.reset_replay || scenario.frames != impl_->frames.size())
    {
        throw std::invalid_argument(
            "quality reference requires reset_replay and one reference per frame");
    }
    for (const auto &variant : scenario.variants)
    {
        if (variant.fusion.diagnostic != "color")
        {
            throw std::invalid_argument("quality reference requires color diagnostic");
        }
    }
}

boost::json::object QualityReference::description() const
{
    return impl_->description;
}

boost::json::object QualityReference::evaluate(const Sample &sample) const
{
    if (!sample.frame || sample.frame_index >= impl_->frames.size())
    {
        throw std::invalid_argument("quality reference sample frame missing/out of range");
    }
    const auto &header = sample.frame->header;
    const auto &reference = impl_->frames[sample.frame_index];
    if (header.at("width").to_number<unsigned>() != impl_->width ||
        header.at("height").to_number<unsigned>() != impl_->height ||
        header.at("stride_bytes").to_number<unsigned>() != impl_->width * 4 ||
        header.at("row_origin") != "top_left" || header.at("pixel_format") != "RGBA8" ||
        header.at("health") != "READY" || header.at("source_type") != "replay" ||
        header.at("output_product") != "final_frame" || header.at("diagnostic_view") != "color" ||
        header.at("view") != impl_->view)
    {
        throw std::runtime_error("quality reference output/view guard mismatch");
    }
    const auto &inputs = header.at("inputs").as_array();
    if (inputs.size() != 4)
    {
        throw std::runtime_error("quality reference input guard mismatch");
    }
    for (size_t camera = 0; camera < 4; ++camera)
    {
        const auto &actual = inputs[camera].as_object();
        if (!actual.at("used").as_bool())
        {
            throw std::runtime_error("quality reference requires all four input cameras");
        }
        for (const auto &guard : reference.inputs[camera].as_object())
        {
            if (actual.at(guard.key()) != guard.value())
            {
                throw std::runtime_error("quality reference input guard mismatch");
            }
        }
    }
    auto result =
        masked_rgb_metrics(sample.frame->payload, reference.rgb.pixels, reference.mask.pixels);
    if (impl_->criteria)
    {
        result["acceptance"] = check_quality_criteria(result, *impl_->criteria);
    }
    return result;
}
} // namespace sv::research
