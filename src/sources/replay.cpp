#include "sv/input_identity.hpp"
#include "sv/source.hpp"
#include "sv/vision.hpp"
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace sv
{
namespace
{
class ReplaySource final : public FrameSource
{
    struct Request
    {
        SourceAction action;
        uint64_t id;
    };

    Config config_;
    SourceTimingGate timing_;
    std::vector<ReplayRow> rows_;
    ImageLoader loader_;
    bool loop_;
    const std::string initial_session_ = "replay-" + std::to_string(now_ns());
    std::string session_ = initial_session_;
    uint64_t reset_epoch_ = 0;
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Request> requests_;
    std::deque<SourceEvent> events_;
    size_t batches_ = 0;
    std::atomic<bool> stopped_{false}, failed_{false};
    std::atomic<uint64_t> decoded_{0}, dropped_{0}, received_{0};
    std::thread worker_;
    std::array<std::filesystem::path, 4> cached_paths_;
    std::array<std::shared_ptr<const Image>, 4> cached_images_;
    std::array<std::string, 4> cached_hashes_;

    void publish(SourceEvent event)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        if (event.kind == SourceEvent::Kind::Frames)
        {
            if (batches_ == size_t(config_.queue_size))
            {
                auto old = std::find_if(events_.begin(), events_.end(), [](const auto &e)
                                        { return e.kind == SourceEvent::Kind::Frames; });
                events_.erase(old);
                --batches_;
                ++dropped_;
            }
            ++batches_;
        }
        else
        {
            wake_.wait(lock, [&]
                       { return stopped_ || events_.size() < size_t(config_.queue_size) + 64; });
        }
        if (!stopped_)
        {
            events_.push_back(std::move(event));
        }
    }

    SourceEvent decode(size_t row, uint64_t sequence, bool paused)
    {
        const auto &r = rows_[row];
        const auto timing = timing_.get();
        std::unique_ptr<std::array<SourceWorkTiming, 4>> work;
        if (timing)
        {
            work = std::make_unique<std::array<SourceWorkTiming, 4>>();
        }
        for (int k = 0; k < 4; ++k)
        {
            if (!r.paths[k].empty() && cached_paths_[k] != r.paths[k])
            {
                const auto load_start = timing ? now_ns() : 0;
                auto image = loader_(r.paths[k]);
                const auto load_done = timing ? now_ns() : 0;
                if (work)
                {
                    (*work)[k] = {k,
                                  "replay_load",
                                  timing->epoch,
                                  sequence,
                                  load_start,
                                  load_done,
                                  std::nullopt,
                                  double(load_done - load_start) / 1e6,
                                  std::nullopt,
                                  false};
                }
                if (image.width != config_.cameras[k].width ||
                    image.height != config_.cameras[k].height || image.channels != 3 ||
                    image.pixels.size() != size_t(image.width) * image.height * 3)
                {
                    throw std::runtime_error("replay image differs from calibration at camera " +
                                             std::to_string(k));
                }
                cached_hashes_[k] = rgb8_image_sha256(image);
                cached_images_[k] = std::make_shared<Image>(std::move(image));
                cached_paths_[k] = r.paths[k];
                ++decoded_;
            }
        }
        SourceEvent event;
        event.batch_id = sequence + 1;
        event.paused = paused;
        const auto delivered = now_ns();
        for (int k = 0; k < 4; ++k)
        {
            if (r.paths[k].empty())
            {
                continue;
            }
            const int64_t offset = r.offset_ns[k];
            if ((offset < 0 && delivered < uint64_t(-offset)) ||
                (offset > 0 && delivered > UINT64_MAX - uint64_t(offset)))
            {
                throw std::runtime_error("replay timestamp overflow");
            }
            const auto timestamp =
                offset < 0 ? delivered - uint64_t(-offset) : delivered + uint64_t(offset);
            event.frames[k] = Frame{k, sequence, timestamp, r.scenario_ns, cached_images_[k]};
            event.frames[k]->image_sha256 = cached_hashes_[k];
            event.frames[k]->source_session = session_;
            event.frames[k]->source_clock_domain = "scenario";
            event.frames[k]->source_sequence = sequence;
            event.frames[k]->source_timestamp_ns = r.scenario_ns;
            if (work)
            {
                auto record = std::make_shared<SourceWorkRecord>();
                record->session = timing;
                record->timing = (*work)[k];
                if (record->timing.kind.empty())
                {
                    record->timing = {
                        k,         "replay_load", timing->epoch, sequence,     delivered,
                        delivered, std::nullopt,  0.0,           std::nullopt, true};
                }
                event.frames[k]->source_work = std::move(record);
            }
            ++received_;
        }
        return event;
    }

    void run()
    {
        bool paused = false;
        size_t row = 0;
        uint64_t sequence = 0, next = now_ns();
        auto advance = [&]
        {
            const bool end = row + 1 == rows_.size();
            auto event = decode(row, sequence++, paused || (end && !loop_));
            publish(std::move(event));
            row = end ? 0 : row + 1;
            if (end && !loop_)
            {
                paused = true;
            }
            const auto interval =
                row ? rows_[row].scenario_ns - rows_[row - 1].scenario_ns
                    : (rows_.size() > 1 ? rows_[1].scenario_ns - rows_[0].scenario_ns : 33333333);
            const auto now = now_ns();
            if (interval > UINT64_MAX - now)
            {
                throw std::runtime_error("replay interval overflow");
            }
            next = now + interval;
        };
        try
        {
            while (!stopped_)
            {
                std::optional<Request> request;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    if (requests_.empty())
                    {
                        if (paused)
                        {
                            wake_.wait(lock, [&] { return stopped_ || !requests_.empty(); });
                        }
                        else
                        {
                            const auto now = now_ns();
                            if (now < next)
                            {
                                wake_.wait_for(lock,
                                               std::chrono::nanoseconds(
                                                   std::min<uint64_t>(next - now, 1000000000)),
                                               [&] { return stopped_ || !requests_.empty(); });
                            }
                        }
                    }
                    if (stopped_)
                    {
                        break;
                    }
                    if (!requests_.empty())
                    {
                        request = requests_.front();
                        requests_.pop_front();
                    }
                }
                if (request)
                {
                    if (request->action == SourceAction::Resume)
                    {
                        paused = false;
                        next = now_ns();
                    }
                    else
                    {
                        paused = true;
                    }
                    if (request->action == SourceAction::ReplayReset)
                    {
                        // Worker-owned state: commands cannot race with decode/cache writes.
                        row = 0;
                        cached_paths_ = {};
                        cached_images_ = {};
                        cached_hashes_ = {};
                        session_ = initial_session_ + "/reset-" + std::to_string(++reset_epoch_);
                        SourceEvent boundary;
                        boundary.kind = SourceEvent::Kind::Reset;
                        boundary.request_id = request->id;
                        boundary.paused = true;
                        publish(std::move(boundary));
                        // Sequence stays monotonic: renderer upload keys and old frame IDs
                        // cannot alias the new first row.
                        advance();
                    }
                    if (request->action == SourceAction::Step)
                    {
                        advance();
                    }
                    SourceEvent completed;
                    completed.kind = SourceEvent::Kind::Control;
                    completed.request_id = request->id;
                    completed.paused = paused;
                    publish(std::move(completed));
                }
                else if (!paused && now_ns() >= next)
                {
                    advance();
                }
            }
        }
        catch (const std::exception &e)
        {
            SourceEvent failure;
            failure.kind = SourceEvent::Kind::Failure;
            failure.reason = e.what();
            failed_ = true;
            publish(std::move(failure));
        }
    }

  public:

    ReplaySource(const Config &config, const std::filesystem::path &manifest, bool loop,
                 ImageLoader loader, std::shared_ptr<const SourceTimingSession> timing)
        : config_(config), rows_(load_manifest(manifest, config)), loader_(std::move(loader)),
          loop_(loop)
    {
        timing_.set(std::move(timing));
        if (!loader_)
        {
            loader_ = read_image;
        }
        worker_ = std::thread([this] { run(); });
    }

    ~ReplaySource() override
    {
        stop();
    }

    void set_timing_session(std::shared_ptr<const SourceTimingSession> timing) noexcept override
    {
        timing_.set(std::move(timing));
    }

    bool request(SourceAction action, uint64_t id) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopped_ || failed_ || requests_.size() == 64)
        {
            return false;
        }
        requests_.push_back({action, id});
        wake_.notify_all();
        return true;
    }

    std::vector<SourceEvent> poll() override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<SourceEvent> result;
        while (!events_.empty())
        {
            result.push_back(std::move(events_.front()));
            events_.pop_front();
        }
        batches_ = 0;
        wake_.notify_all();
        return result;
    }

    SourceStats stats() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return {decoded_.load(), dropped_.load(), received_.load(), 0, batches_};
    }

    void stop() override
    {
        stopped_ = true;
        wake_.notify_all();
        if (worker_.joinable())
        {
            worker_.join();
        }
    }
};
} // namespace

std::unique_ptr<FrameSource> make_replay_source(const Config &config,
                                                const std::filesystem::path &manifest, bool loop,
                                                ImageLoader loader,
                                                std::shared_ptr<const SourceTimingSession> timing)
{
    return std::make_unique<ReplaySource>(config, manifest, loop, std::move(loader),
                                          std::move(timing));
}
} // namespace sv
