#pragma once
#include "sv/frame.hpp"
#include "sv/seam_optimizer.hpp"
#include <memory>

namespace sv
{
struct RenderInspection;

struct RenderTiming
{
    double fusion_cpu_ms = 0, layer_readback_cpu_ms = 0;
    double upload_cpu_ms = 0;
    double readback_copy_cpu_ms = 0;
    std::optional<double> gpu_draw_ms;
    std::string gpu_timer_status = "extension_unavailable";
    std::optional<seam::Summary> seam_optimization;
};

class Renderer
{
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:

    explicit Renderer(const Config &);
    ~Renderer();
    Renderer(const Renderer &) = delete;
    Renderer &operator=(const Renderer &) = delete;
    // Render-thread only; caller must validate and prepare settings before publishing.
    void set_fusion(Fusion) noexcept;
    void set_surface(Surface); // Prepare mesh buffers before committing; render-thread only.
    Image render(const FrameSet &, const View &, RenderInspection *inspection = nullptr);
    std::vector<Pixel> project_points(const Camera &, const std::vector<Vec3> &);
    std::string vendor() const;
    std::string device() const;
    uint64_t uploads() const;
    uint64_t mesh_builds() const;
    size_t triangles() const;
    boost::json::object mesh_resources() const;
    RenderTiming last_timing() const;
    boost::json::object capabilities() const;
};
} // namespace sv
