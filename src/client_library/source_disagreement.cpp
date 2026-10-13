#include "sv/client_output.hpp"
#include <stdexcept>

namespace sv::client
{
SourceDisagreementView::SourceDisagreementView(std::shared_ptr<const Message> message)
    : message_(std::move(message))
{
    if (!message_ || validate_output_product(*message_) != OutputProduct::SourceDisagreement)
    {
        throw std::invalid_argument("source disagreement message required");
    }
    width_ = static_cast<int>(message_->header.at("width").as_int64());
    height_ = static_cast<int>(message_->header.at("height").as_int64());
}

int SourceDisagreementView::width() const noexcept
{
    return width_;
}

int SourceDisagreementView::height() const noexcept
{
    return height_;
}

SourceDisagreementPixel SourceDisagreementView::at(int x, int y) const
{
    if (x < 0 || y < 0 || x >= width_ || y >= height_)
    {
        throw std::out_of_range("source disagreement coordinate");
    }
    const auto *p = message_->payload.data() + (size_t(y) * width_ + x) * 4;
    return {p[0], p[1], p[2] != 0, p[3] != 0};
}

const Message &SourceDisagreementView::message() const noexcept
{
    return *message_;
}
} // namespace sv::client
