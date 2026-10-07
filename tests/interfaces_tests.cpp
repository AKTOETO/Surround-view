#include "sv/interfaces.hpp"
#include "sv/config_store.hpp"
#include <cassert>
#include <iostream>

class MockProductSink : public sv::IProductSink
{
public:
    bool publish(sv::Message message) override
    {
        published_messages.push_back(std::move(message));
        return true;
    }

    std::vector<sv::Message> published_messages;
};

int main()
{
    sv::SystemClock sys_clock;
    assert(sys_clock.now_ns() > 0);

    sv::MockClock mock_clock(5000000ULL);
    assert(mock_clock.now_ns() == 5000000ULL);

    mock_clock.advance_ms(10);
    assert(mock_clock.now_ns() == 15000000ULL);

    mock_clock.set_time_ns(100ULL);
    assert(mock_clock.now_ns() == 100ULL);

    sv::Config cfg;
    cfg.width = 640;
    sv::ConfigStore store(cfg);

    sv::IConfigStore &iconfig = store;
    (void)iconfig;
    assert(store.active()->width == 640);
    assert(store.revision() == 0);

    sv::Config new_cfg = cfg;
    new_cfg.width = 1280;
    std::string err;
    assert(store.update(new_cfg, err));
    assert(store.active()->width == 1280);
    assert(store.revision() == 1);

    MockProductSink sink;
    sv::IProductSink &isink = sink;
    (void)isink;
    sv::Message msg{11, {{"test", true}}, {}};
    assert(sink.publish(msg));
    assert(sink.published_messages.size() == 1);

    std::cout << "All interfaces and MockClock tests passed successfully.\n";
    return 0;
}
