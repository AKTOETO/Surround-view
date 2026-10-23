#pragma once
#include "sv/camera_diagnostics.hpp"
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
    Step,
    ReplayReset
};

struct SourceEvent
{
    enum class Kind
    {
        Frames,
        Reset, // Replay boundary: discard synchronizer history before the next batch.
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
    std::optional<CameraDiagnosticsSet> camera_diagnostics = {};
};

class FrameSource
{
  public:

    virtual ~FrameSource() = default;
    virtual bool request(SourceAction, uint64_t request_id) = 0;
    virtual std::vector<SourceEvent> poll() = 0;
    virtual SourceStats stats() const = 0;
    // External callers may stop concurrently: every call waits for worker shutdown.
    // Object lifetime must cover all calls. A blocked backend/loader may still delay shutdown.
    virtual void stop() = 0;

    virtual void set_timing_session(std::shared_ptr<const SourceTimingSession>) noexcept
    {
    }
};

using ImageLoader = std::function<Image(const std::filesystem::path &)>;
std::unique_ptr<FrameSource> make_replay_source(
    const Config &, const std::filesystem::path &, bool loop, ImageLoader loader = {},
    std::shared_ptr<const SourceTimingSession> timing = {});
std::unique_ptr<FrameSource> make_socket_source(const Config &);
std::unique_ptr<FrameSource> make_camera_source(
    const Config &, std::shared_ptr<const SourceTimingSession> timing = {});
} // namespace sv
