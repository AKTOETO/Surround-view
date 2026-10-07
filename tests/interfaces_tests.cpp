#include "sv/interfaces.hpp"
#include "sv/config_store.hpp"
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
    check(sys_clock.now_ns() > 0, "system clock monotonic value");

    sv::MockClock mock_clock(5000000ULL);
    check(mock_clock.now_ns() == 5000000ULL, "mock clock initial value");

    mock_clock.advance_ms(10);
    check(mock_clock.now_ns() == 15000000ULL, "mock clock advance");

    mock_clock.set_time_ns(100ULL);
    check(mock_clock.now_ns() == 100ULL, "mock clock set");

    sv::Config cfg;
    cfg.width = 640;
    sv::ConfigStore store(cfg);

    sv::IConfigStore &iconfig = store;
    (void)iconfig;
    check(store.active()->width == 640, "initial config snapshot");
    check(store.revision() == 0, "initial config revision");

    sv::Config new_cfg = cfg;
    new_cfg.width = 1280;
    std::string err;
    check(store.update(new_cfg, err), "config update accepted");
    check(store.active()->width == 1280, "updated config snapshot");
    check(store.revision() == 1, "updated config revision");

    MockProductSink sink;
    sv::IProductSink &isink = sink;
    (void)isink;
    sv::Message msg{11, {{"test", true}}, {}};
    check(sink.publish(msg), "mock sink accepts message");
    check(sink.published_messages.size() == 1, "mock sink stores message");

    std::cout << "All interfaces and MockClock tests passed successfully.\n";
    return 0;
}
