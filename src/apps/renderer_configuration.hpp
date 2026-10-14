#pragma once
#include "sv/config_transaction.hpp"
#include "sv/renderer.hpp"

namespace sv
{
class RendererConfigurationUpdate final : public PreparedConfiguration
{
    Renderer &renderer_;
    Fusion fusion_;
    std::unique_ptr<Renderer::SurfaceUpdate> surface_;

  public:

    RendererConfigurationUpdate(Renderer &renderer, const Config &candidate, bool change_surface)
        : renderer_(renderer), fusion_(candidate.fusion)
    {
        if (change_surface)
        {
            surface_ = renderer_.prepare_surface(candidate.surface);
        }
    }

    void publish() noexcept override
    {
        if (surface_)
        {
            surface_->commit();
        }
        renderer_.set_fusion(std::move(fusion_));
    }
};
} // namespace sv
