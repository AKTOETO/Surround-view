#pragma once
#include "sv/config_store.hpp"
#include "sv/experiment_lease.hpp"
#include "sv/fusion_runtime.hpp"
#include "sv/renderer.hpp"
#include "sv/source.hpp"

namespace sv
{
struct ExperimentRecoveryContext
{
    ConfigStore &config;
    Renderer &renderer;
    FrameSource &source;
    bool paused;
    uint64_t &source_request;
    bool source_pending;
};

// Render-thread recovery coordinator; source controls complete asynchronously.
class ExperimentRecovery
{
  public:

    void reset()
    {
        config_done_ = false;
        request_ = deadline_ = 0;
    }

    void start(ExperimentLease &lease, uint64_t now)
    {
        lease.start_restore();
        deadline_ = now + 5000000000ULL;
    }

    bool handles(const SourceEvent &event) const
    {
        return request_ && event.request_id == request_;
    }

    void accept(ExperimentLease &lease, const SourceEvent &event)
    {
        request_ = 0;
        // A late source ACK must not clear a previously failed restoration.
        if (lease.state() == ExperimentLease::State::Failed)
        {
            return;
        }
        if (!event.reason.empty() || event.paused != lease.baseline_paused())
        {
            lease.fail("source_restore_failed:" + event.reason);
        }
        else
        {
            lease.complete();
        }
    }

    void tick(ExperimentLease &lease, ExperimentRecoveryContext context, uint64_t now)
    {
        if (lease.state() != ExperimentLease::State::Restoring)
        {
            return;
        }
        if (now >= deadline_)
        {
            lease.fail("experiment_restore_timeout");
            return;
        }
        if (context.source_pending)
        {
            return;
        }
        try
        {
            if (!config_done_)
            {
                auto effective = context.config.active()->effective;
                effective.as_object()["fusion"] = fusion_settings(lease.baseline().fusion);
                const auto baseline_surface = lease.baseline().effective.as_object().at("surface");
                const bool changed_surface =
                    effective.as_object().at("surface") != baseline_surface;
                effective.as_object()["surface"] = baseline_surface;
                auto fusion = parse_config(effective).fusion;
                std::string error;
                if (!context.config.update_runtime_if_revision(
                        effective, context.config.revision(), error,
                        [&](const Config &prepared)
                        {
                            if (changed_surface)
                            {
                                context.renderer.set_surface(prepared.surface);
                            }
                            context.renderer.set_fusion(std::move(fusion));
                        }))
                {
                    throw std::runtime_error(error);
                }
                config_done_ = true;
            }
            if (!request_)
            {
                if (context.paused == lease.baseline_paused())
                {
                    lease.complete();
                }
                else
                {
                    const auto action =
                        lease.baseline_paused() ? SourceAction::Pause : SourceAction::Resume;
                    const auto request = ++context.source_request;
                    if (context.source.request(action, request))
                    {
                        request_ = request;
                    }
                }
            }
        }
        catch (const std::exception &error)
        {
            lease.fail(error.what());
        }
    }

  private:

    bool config_done_ = false;
    uint64_t request_ = 0, deadline_ = 0;
};
} // namespace sv
