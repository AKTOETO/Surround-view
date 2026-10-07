#include "sv/source.hpp"
#include <cassert>
#include <iostream>

int main()
{
    sv::Config config;
    for (int i = 0; i < 4; ++i)
    {
        config.cameras[i].id = i;
        config.cameras[i].width = 640;
        config.cameras[i].height = 480;
    }

    auto source = sv::make_camera_source(config);
    assert(source != nullptr);

    auto stats = source->stats();
    (void)stats;
    assert(stats.queued_batches == 0);

    assert(source->request(sv::SourceAction::Pause, 1));
    assert(source->request(sv::SourceAction::Resume, 2));

    auto events = source->poll();
    assert(events.size() == 2);

    source->stop();
    std::cout << "All make_camera_source hardware adapter unit tests passed successfully.\n";
    return 0;
}
