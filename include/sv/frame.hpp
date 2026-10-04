#pragma once
#include "sv/config.hpp"
#include <deque>
#include <memory>
#include <optional>

namespace sv
{
struct Image
{
    int width = 0, height = 0, channels = 0;
    std::vector<unsigned char> pixels;
};

Image read_ppm(const std::filesystem::path &);
void write_ppm(const std::filesystem::path &, const Image &);

struct Frame
{
    int camera_id = 0;
    uint64_t sequence = 0, release_ns = 0, scenario_ns = 0;
    std::shared_ptr<const Image> image;
};

struct FrameSet
{
    std::array<std::optional<Frame>, 4> frames;
    std::string health = "NO_INPUT";
    uint64_t skew_ns = 0;
};

class Synchronizer
{
    std::array<std::deque<Frame>, 4> queues_;
    std::array<std::optional<uint64_t>, 4> last_sequence_, last_time_;
    size_t capacity_;
    uint64_t skew_, age_;

  public:

    uint64_t dropped = 0, duplicate = 0, out_of_order = 0;

    explicit Synchronizer(const Config &c)
        : capacity_(c.queue_size), skew_(c.skew_ns), age_(c.age_ns)
    {
    }

    bool push(Frame);
    FrameSet select(uint64_t now) const;

    size_t size(int id) const
    {
        return queues_.at(id).size();
    }
};

struct ReplayRow
{
    uint64_t scenario_ns = 0;
    std::array<std::filesystem::path, 4> paths;
    std::array<int64_t, 4> offset_ns{};
};

std::vector<ReplayRow> load_manifest(const std::filesystem::path &, const Config &);
} // namespace sv
