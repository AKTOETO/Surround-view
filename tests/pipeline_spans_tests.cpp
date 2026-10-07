#include "sv/pipeline_spans.hpp"
#include <cassert>
#include <iostream>

int main()
{
    sv::PipelineSpan span{"projection_fusion", 1000000ULL, 3500000ULL};
    assert(span.duration_ms() == 2.5);

    sv::PipelineSpanTracker tracker(5);
    assert(tracker.count() == 0);
    assert(tracker.median_total_latency_ms() == 0.0);

    for (int i = 1; i <= 5; ++i)
    {
        sv::FrameTelemetry ft;
        ft.frame_id = i;
        ft.total_pipeline_ms = static_cast<double>(i * 2);
        tracker.record_frame(ft);
    }

    assert(tracker.count() == 5);
    // Latencies: 2.0, 4.0, 6.0, 8.0, 10.0 -> Median = 6.0
    assert(tracker.median_total_latency_ms() == 6.0);

    // Push 6th frame to test ring buffer eviction
    sv::FrameTelemetry ft6;
    ft6.frame_id = 6;
    ft6.total_pipeline_ms = 12.0;
    tracker.record_frame(ft6);

    assert(tracker.count() == 5);
    assert(tracker.history().front().frame_id == 2);

    auto json = ft6.to_json();
    assert(json.at("frame_id").as_string() == "6");
    assert(json.at("total_pipeline_ms").as_double() == 12.0);

    std::cout << "All PipelineSpanTracker unit tests passed successfully.\n";
    return 0;
}
