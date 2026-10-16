#include "opencv_capture.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <opencv2/videoio.hpp>
#include <stdexcept>

namespace sv
{
namespace
{
class OpenCvDevice final : public VideoDevice
{
  public:

    bool is_open() const override
    {
        return capture_.isOpened();
    }

    bool open(const std::string &path, int backend) override
    {
        return capture_.open(path, backend);
    }

    bool set(int property, double value) override
    {
        return capture_.set(property, value);
    }

    double get(int property) const override
    {
        return capture_.get(property);
    }

    bool read(cv::Mat &image) override
    {
        return capture_.read(image);
    }

    void close() override
    {
        capture_.release();
    }

  private:

    cv::VideoCapture capture_;
};

class OpenCvCapture final : public CameraCapture
{
  public:

    OpenCvCapture(CameraCaptureSettings settings, std::unique_ptr<VideoDevice> device)
        : settings_(std::move(settings)), device_(std::move(device))
    {
    }

    bool is_open() const override
    {
        return device_->is_open();
    }

    std::string open_failure() const override
    {
        return failure_;
    }

    bool open(const std::string &path, int width, int height) override
    {
        failure_.clear();
        mode_.reset();
        if (!device_->open(path, cv::CAP_V4L2))
        {
            return false;
        }
        // FOURCC first: changing it can reset size/FPS. Read back after all requests.
        if (!settings_.fourcc.empty())
        {
            const auto &code = settings_.fourcc;
            device_->set(cv::CAP_PROP_FOURCC,
                         cv::VideoWriter::fourcc(code[0], code[1], code[2], code[3]));
        }
        device_->set(cv::CAP_PROP_FRAME_WIDTH, width);
        device_->set(cv::CAP_PROP_FRAME_HEIGHT, height);
        if (settings_.fps)
        {
            device_->set(cv::CAP_PROP_FPS, *settings_.fps);
        }
        if (settings_.buffer_count)
        {
            device_->set(cv::CAP_PROP_BUFFERSIZE, *settings_.buffer_count);
        }
        device_->set(cv::CAP_PROP_CONVERT_RGB, 1);
        if (!matches("width", cv::CAP_PROP_FRAME_WIDTH, width) ||
            !matches("height", cv::CAP_PROP_FRAME_HEIGHT, height) ||
            !matches("convert_rgb", cv::CAP_PROP_CONVERT_RGB, 1))
        {
            return false;
        }
        if (!settings_.fourcc.empty())
        {
            const auto &code = settings_.fourcc;
            if (!matches("fourcc", cv::CAP_PROP_FOURCC,
                         cv::VideoWriter::fourcc(code[0], code[1], code[2], code[3])))
            {
                return false;
            }
        }
        double actual_fps = 0;
        if (settings_.fps && !matches("fps", cv::CAP_PROP_FPS, *settings_.fps,
                                      std::max(.01, *settings_.fps * .01), &actual_fps))
        {
            return false;
        }
        if (settings_.buffer_count &&
            !matches("buffer_count", cv::CAP_PROP_BUFFERSIZE, *settings_.buffer_count))
        {
            return false;
        }
        CameraCaptureMode mode;
        mode.width = unsigned(width);
        mode.height = unsigned(height);
        mode.fps = settings_.fps ? std::optional<double>(actual_fps) : std::nullopt;
        mode.fourcc =
            settings_.fourcc.empty() ? std::nullopt : std::optional<std::string>(settings_.fourcc);
        mode.buffer_count = settings_.buffer_count;
        CameraAutomaticProperties automatic;
        if (!settings_.fps)
        {
            const auto value = observe(cv::CAP_PROP_FPS);
            if (value && *value > 0)
            {
                automatic.fps = *value;
            }
        }
        if (settings_.fourcc.empty())
        {
            const auto value = observe_integer(cv::CAP_PROP_FOURCC);
            if (value)
            {
                std::string code;
                for (unsigned byte = 0; byte < 4; ++byte)
                {
                    code.push_back(char((*value >> (byte * 8)) & 0xff));
                }
                if (std::all_of(code.begin(), code.end(),
                                [](unsigned char c) { return c >= 32 && c <= 126; }))
                {
                    automatic.fourcc = std::move(code);
                }
            }
        }
        if (!settings_.buffer_count)
        {
            automatic.buffer_count = observe_integer(cv::CAP_PROP_BUFFERSIZE);
        }
        mode.automatic_properties = std::move(automatic);
        mode_ = std::move(mode);
        return true;
    }

    bool read(cv::Mat &image) override
    {
        return device_->read(image);
    }

    void close() override
    {
        mode_.reset();
        device_->close();
    }

    std::optional<CameraCaptureMode> negotiated_mode() const override
    {
        return mode_;
    }

  private:

    std::optional<double> observe(int property) const
    {
        try
        {
            const double value = device_->get(property);
            return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
        }
        catch (const cv::Exception &)
        {
            // Optional inventory must not reject an otherwise valid capture profile.
            return {};
        }
    }

    std::optional<unsigned> observe_integer(int property) const
    {
        const auto value = observe(property);
        if (!value || *value < 1 || *value > std::numeric_limits<uint32_t>::max() ||
            std::floor(*value) != *value)
        {
            return {};
        }
        return static_cast<unsigned>(*value);
    }

    bool matches(const char *name, int property, double requested, double tolerance = 0,
                 double *readback = nullptr)
    {
        const double actual = device_->get(property);
        if (!std::isfinite(actual) || std::abs(actual - requested) > tolerance)
        {
            failure_ = std::string("camera_configuration_rejected:") + name +
                       ":requested=" + std::to_string(requested) +
                       ":actual=" + std::to_string(actual);
            return false;
        }
        if (readback)
        {
            *readback = actual;
        }
        return true;
    }

    CameraCaptureSettings settings_;
    std::unique_ptr<VideoDevice> device_;
    std::string failure_;
    std::optional<CameraCaptureMode> mode_;
};
} // namespace

std::unique_ptr<CameraCapture> make_opencv_capture(const CameraCaptureSettings &settings,
                                                   std::unique_ptr<VideoDevice> device)
{
    if ((!settings.fourcc.empty() &&
         (settings.fourcc.size() != 4 ||
          !std::all_of(settings.fourcc.begin(), settings.fourcc.end(),
                       [](unsigned char c) { return c >= 32 && c <= 126; }))) ||
        (settings.fps &&
         (!std::isfinite(*settings.fps) || *settings.fps < .1 || *settings.fps > 240)) ||
        (settings.buffer_count && (*settings.buffer_count < 1 || *settings.buffer_count > 32)))
    {
        throw std::invalid_argument("invalid camera capture settings");
    }
    return std::make_unique<OpenCvCapture>(settings, device ? std::move(device)
                                                            : std::make_unique<OpenCvDevice>());
}
} // namespace sv
