#pragma once
#include "sv/frame.hpp"
#include <functional>
#include <memory>
#include <vector>

namespace sv
{
enum class SourceAction
{
    Pause,
    Resume,
    Step
};

struct SourceEvent
{
    enum class Kind
    {
        Frames,
        Control,
        Failure,
        Status
    };
    Kind kind = Kind::Frames;
    uint64_t request_id = 0, batch_id = 0;
    bool paused = false;
    std::array<std::optional<Frame>, 4> frames;
    std::string reason;
    int camera_id = -1;
};

struct SourceStats
{
    uint64_t decoded = 0, dropped = 0, received = 0;
    uint64_t rejected = 0;
    size_t queued_batches = 0;
};

class FrameSource
{
  public:

    virtual ~FrameSource() = default;
    // Nonblocking submission; false means the bounded control queue is full/stopped.
    virtual bool request(SourceAction, uint64_t request_id) = 0;
    virtual std::vector<SourceEvent> poll() = 0;
    virtual SourceStats stats() const = 0;
    virtual void stop() = 0;
};

using ImageLoader = std::function<Image(const std::filesystem::path &)>;
std::unique_ptr<FrameSource> make_replay_source(const Config &, const std::filesystem::path &,
                                                bool loop, ImageLoader loader = {});
std::unique_ptr<FrameSource> make_socket_source(const Config &);
} // namespace sv
