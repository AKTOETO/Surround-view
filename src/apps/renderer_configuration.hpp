#pragma once
#include "sv/config_transaction.hpp"
#include "sv/renderer.hpp"
#include "sv/telemetry.hpp"

namespace sv
{
class RendererConfigurationUpdate final : public PreparedConfiguration
{
    Renderer &renderer_;
    Fusion fusion_;
    std::unique_ptr<Renderer::SurfaceUpdate> surface_;
    std::unique_ptr<TelemetryCollector::Update> telemetry_;

  public:

    RendererConfigurationUpdate(Renderer &renderer, const Config &candidate, bool change_surface,
                                TelemetryCollector *telemetry = nullptr,
                                bool change_telemetry = false)
        : renderer_(renderer), fusion_(candidate.fusion)
    {
        if (change_telemetry && telemetry)
        {
            telemetry_ = telemetry->prepare(candidate.telemetry);
        }
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
        if (telemetry_)
        {
            telemetry_->commit();
        }
    }
};
} // namespace sv
