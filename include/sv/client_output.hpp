#pragma once
#include "sv/output_product.hpp"
#include "sv/render_support.hpp"
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
} // namespace sv::client
