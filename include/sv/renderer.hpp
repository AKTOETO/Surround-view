#pragma once
#include "sv/frame.hpp"
#include <memory>

namespace sv
{
struct RenderTiming
{
    double upload_cpu_ms = 0;
    double readback_copy_cpu_ms = 0;
    std::optional<double> gpu_draw_ms;
    std::string gpu_timer_status = "extension_unavailable";
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
    Image render(const FrameSet &, const View &);
    std::vector<Pixel> project_points(const Camera &, const std::vector<Vec3> &);
    std::string vendor() const;
    std::string device() const;
    uint64_t uploads() const;
    uint64_t mesh_builds() const;
    size_t triangles() const;
    RenderTiming last_timing() const;
    boost::json::object capabilities() const;
};
} // namespace sv
