#pragma once
#include "camera_capture.hpp"
#include "sv/camera_capture_config.hpp"

namespace sv
{
// Internal property seam. Production delegates every operation to cv::VideoCapture.
class VideoDevice
{
  public:

    virtual ~VideoDevice() = default;
    virtual bool is_open() const = 0;
    virtual bool open(const std::string &, int backend) = 0;
    virtual bool set(int property, double value) = 0;
    virtual double get(int property) const = 0;
    virtual bool read(cv::Mat &) = 0;
    virtual void close() = 0;
};

std::unique_ptr<CameraCapture> make_opencv_capture(const CameraCaptureSettings &,
                                                   std::unique_ptr<VideoDevice> = {});
} // namespace sv
