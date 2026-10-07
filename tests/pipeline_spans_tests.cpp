#include "sv/pipeline_spans.hpp"
#include <iostream>
#include <stdexcept>

namespace
{
void check(bool condition, const char *message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}
} // namespace

int main()
{
    sv::PipelineSpan span{"projection_fusion", 1000000ULL, 3500000ULL};
    check(span.duration_ms() == 2.5, "span duration");

    sv::PipelineSpanTracker tracker(5);
    check(tracker.count() == 0, "empty tracker count");
    check(tracker.median_total_latency_ms() == 0.0, "empty tracker median");

    for (int i = 1; i <= 5; ++i)
    {
        sv::FrameTelemetry ft;
        ft.frame_id = i;
        ft.total_pipeline_ms = static_cast<double>(i * 2);
        tracker.record_frame(ft);
    }

    check(tracker.count() == 5, "tracker retained five frames");
    // Latencies: 2.0, 4.0, 6.0, 8.0, 10.0 -> Median = 6.0
    check(tracker.median_total_latency_ms() == 6.0, "odd median");

    // Push 6th frame to test ring buffer eviction
    sv::FrameTelemetry ft6;
    ft6.frame_id = 6;
    ft6.total_pipeline_ms = 12.0;
    tracker.record_frame(ft6);

    check(tracker.count() == 5, "tracker capacity after eviction");
    check(tracker.history().front().frame_id == 2, "oldest frame evicted");

    auto json = ft6.to_json();
    check(json.at("frame_id").as_string() == "6", "frame id serialized");
    check(json.at("server_receive_to_render_ms").as_double() == 12.0,
          "latency field serialized");
    check(json.at("spans_ms").as_object().at("gpu_draw").is_null(),
          "unavailable GPU timer serialized as null");

    std::cout << "All PipelineSpanTracker unit tests passed successfully.\n";
    return 0;
}
