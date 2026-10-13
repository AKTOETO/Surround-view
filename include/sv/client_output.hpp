#pragma once
#include "sv/output_product.hpp"
#include "sv/render_support.hpp"
#include "sv/source_visibility.hpp"
#include <memory>

namespace sv::client
{
using sv::OutputProduct;

// Missing product means legacy final_frame. Unknown/malformed products are rejected.
OutputProduct validate_output_product(const Message &);

struct SupportPixel
{
    unsigned char eligible_cameras = 0, positive_local_weights = 0;
    RenderSupportPath path = RenderSupportPath::background;
    bool geometry = false;
};

// Owns the immutable Message, including provenance. No OpenCV/GUI dependency or payload copy.
class RenderSupportView
{
    std::shared_ptr<const Message> message_;
    int width_ = 0, height_ = 0;

  public:

    explicit RenderSupportView(std::shared_ptr<const Message>);
    int width() const noexcept;
    int height() const noexcept;
    SupportPixel at(int x, int y) const;
    const Message &message() const noexcept;
};

// Four status bytes per pixel; channel3 is camera3, not opacity.
class SourceVisibilityView
{
    std::shared_ptr<const Message> message_;
    int width_ = 0, height_ = 0;

  public:

    explicit SourceVisibilityView(std::shared_ptr<const Message>);
    int width() const noexcept;
    int height() const noexcept;
    SourceVisibilityStatus at(int x, int y, int camera) const;
    const Message &message() const noexcept;
};

struct SourceDisagreementPixel
{
    unsigned char encoded_rgb_range = 0, eligible_cameras = 0;
    bool defined = false, carrier = false;
};

// Undefined with fewer than two eligible cameras; zero is not a confidence certificate.
class SourceDisagreementView
{
    std::shared_ptr<const Message> message_;
    int width_ = 0, height_ = 0;

  public:

    explicit SourceDisagreementView(std::shared_ptr<const Message>);
    int width() const noexcept;
    int height() const noexcept;
    SourceDisagreementPixel at(int x, int y) const;
    const Message &message() const noexcept;
};
} // namespace sv::client
