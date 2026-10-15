#pragma once
#include "sv/source.hpp"
#include <opencv2/core/mat.hpp>

namespace sv
{
// Internal driver seam; keeps OpenCV conversion/queue code identical in real and fake capture
// tests.
class CameraCapture
{
  public:

    virtual ~CameraCapture() = default;
    virtual bool is_open() const = 0;
    virtual bool open(const std::string &, int width, int height) = 0;
    virtual bool read(cv::Mat &) = 0;
    virtual void close() = 0;

    virtual std::string open_failure() const
    {
        return {};
    }
};

using CameraCaptureFactory = std::function<std::unique_ptr<CameraCapture>(int camera_id)>;
std::unique_ptr<FrameSource> make_camera_source(const Config &,
                                                std::shared_ptr<const SourceTimingSession>,
                                                CameraCaptureFactory);
} // namespace sv
