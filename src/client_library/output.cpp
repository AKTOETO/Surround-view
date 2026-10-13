#include "sv/client_output.hpp"
#include <stdexcept>

namespace sv::client
{
OutputProduct validate_output_product(const Message &message)
{
    const auto *value = message.header.if_contains("output_product");
    const auto product =
        value ? parse_output_product(std::string(value->as_string())) : OutputProduct::FinalFrame;
    if (product == OutputProduct::FinalFrame)
    {
        if (message.header.contains("support_schema") ||
            message.header.contains("source_visibility_schema"))
        {
            throw std::invalid_argument("support schema on final frame");
        }
        return product;
    }
    const bool visibility = product == OutputProduct::SourceVisibility;
    const auto &h = message.header;
    const auto width = h.at("width").as_int64(), height = h.at("height").as_int64();
    if (message.type != 11 || h.at("pixel_format").as_string() != "RGBA8" ||
        h.at("row_origin").as_string() != "top_left" || width < 2 || width > 4096 || height < 2 ||
        height > 2160 || h.at("stride_bytes").as_int64() != width * 4 ||
        message.payload.size() != size_t(width * height * 4) ||
        (visibility ? h.at("source_visibility_schema") != source_visibility_schema()
                    : h.at("support_schema") != render_support_schema()) ||
        h.contains(visibility ? "support_schema" : "source_visibility_schema") ||
        h.at("session_id").as_string().empty() || h.at("buffer_token").as_string().empty() ||
        h.at("diagnostic_view").as_string() != "color")
    {
        throw std::invalid_argument("invalid render support metadata");
    }
    for (const auto *key :
         {"frame_id", "frame_set_id", "state_revision", "config_revision", "applied_command_id"})
    {
        parse_decimal_u64(std::string(h.at(key).as_string()));
    }
    const auto &inputs = h.at("inputs").as_array();
    unsigned cameras = 0, used_cameras = 0;
    if (inputs.size() != 4)
    {
        throw std::invalid_argument("invalid render support inputs");
    }
    for (const auto &input : inputs)
    {
        const auto &fields = input.as_object();
        const auto camera = fields.at("camera_id").as_int64();
        if (camera < 0 || camera > 3 || (cameras & (1u << camera)) ||
            fields.at("calibration_id").as_string().empty())
        {
            throw std::invalid_argument("invalid render support camera identity");
        }
        cameras |= 1u << camera;
        if (fields.at("used").as_bool())
        {
            used_cameras |= 1u << camera;
            for (const auto *key : {"sequence_id", "release_timestamp_ns"})
            {
                parse_decimal_u64(std::string(fields.at(key).as_string()));
            }
        }
    }
    for (size_t offset = 0; offset < message.payload.size(); offset += 4)
    {
        const auto *p = message.payload.data() + offset;
        if (visibility)
        {
            for (int camera = 0; camera < 4; ++camera)
            {
                if (p[camera] > 12 ||
                    ((p[camera] == 0 || p[camera] == 6) &&
                     (p[0] != p[camera] || p[1] != p[camera] || p[2] != p[camera] ||
                      p[3] != p[camera])) ||
                    (!(used_cameras & (1u << camera)) && p[camera] != 0 && p[camera] != 6 &&
                     p[camera] != 2))
                {
                    throw std::invalid_argument("invalid source visibility pixel");
                }
            }
            continue;
        }
        const auto path = static_cast<RenderSupportPath>(p[2]);
        if (p[0] > 4 || p[1] > p[0] || p[2] > 6 ||
            p[3] != (path == RenderSupportPath::background ? 0 : 255) ||
            (p[2] <= 2 && (p[0] || p[1])) ||
            (path == RenderSupportPath::gpu_fusion && (!p[0] || !p[1])) ||
            (path == RenderSupportPath::hybrid_fusion && !p[0]) ||
            (path == RenderSupportPath::hybrid_fallback && (p[0] || p[1])))
        {
            throw std::invalid_argument("invalid render support pixel");
        }
    }
    return product;
}

RenderSupportView::RenderSupportView(std::shared_ptr<const Message> message)
    : message_(std::move(message))
{
    if (!message_ || validate_output_product(*message_) != OutputProduct::RenderSupport)
    {
        throw std::invalid_argument("render support message required");
    }
    width_ = static_cast<int>(message_->header.at("width").as_int64());
    height_ = static_cast<int>(message_->header.at("height").as_int64());
}

int RenderSupportView::width() const noexcept
{
    return width_;
}

int RenderSupportView::height() const noexcept
{
    return height_;
}

SupportPixel RenderSupportView::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        throw std::out_of_range("render support pixel coordinate");
    }
    const auto *pixel = message_->payload.data() + (size_t(y) * width_ + x) * 4;
    return {pixel[0], pixel[1], static_cast<RenderSupportPath>(pixel[2]), pixel[3] != 0};
}

const Message &RenderSupportView::message() const noexcept
{
    return *message_;
}

SourceVisibilityView::SourceVisibilityView(std::shared_ptr<const Message> message)
    : message_(std::move(message))
{
    if (!message_ || validate_output_product(*message_) != OutputProduct::SourceVisibility)
    {
        throw std::invalid_argument("source visibility message required");
    }
    width_ = static_cast<int>(message_->header.at("width").as_int64());
    height_ = static_cast<int>(message_->header.at("height").as_int64());
}

int SourceVisibilityView::width() const noexcept
{
    return width_;
}

int SourceVisibilityView::height() const noexcept
{
    return height_;
}

SourceVisibilityStatus SourceVisibilityView::at(int x, int y, int camera) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || camera < 0 || camera > 3)
    {
        throw std::out_of_range("source visibility coordinate/camera");
    }
    return static_cast<SourceVisibilityStatus>(
        message_->payload[(size_t(y) * width_ + x) * 4 + camera]);
}

const Message &SourceVisibilityView::message() const noexcept
{
    return *message_;
}
} // namespace sv::client
