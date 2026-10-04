#pragma once
#include "sv/frame.hpp"
#include <memory>

namespace sv
{
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
    size_t triangles() const;
};
} // namespace sv
