#include "sv/config_store.hpp"
#include "sv/server_session.hpp"
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
    sv::Config config;
    config.width = 1280;
    config.height = 720;
    config.profile_id = "test-profile";

    sv::ConfigStore store(config);
    check(store.revision() == 0, "initial config revision");
    check(store.active()->width == 1280, "initial config snapshot");

    sv::Config updated = config;
    updated.width = 1920;
    std::string err;
    check(store.update(updated, err), "config update accepted");
    check(store.revision() == 1, "config revision increment");
    check(store.active()->width == 1920, "updated config snapshot");

    sv::SessionRegistry registry;
    sv::View default_view;
    default_view.azimuth = 1.0;

    auto session = registry.create_session("sess-123", "tok-456", default_view);
    check(session != nullptr, "session created");
    check(session->session_id == "sess-123", "session id preserved");
    check(session->is_control_only, "session begins control-only");
    check(registry.count() == 1, "session registered");

    check(registry.attach_data_channel("sess-123", "tok-456"), "valid data token accepted");
    check(!session->is_control_only, "data channel attached");

    check(!registry.attach_data_channel("sess-123", "wrong-token"), "invalid token rejected");
    check(!registry.attach_data_channel("unknown", "tok-456"), "unknown session rejected");

    registry.remove_session("sess-123");
    check(registry.count() == 0, "removed session absent");

    std::cout << "All ConfigStore and SessionRegistry tests passed successfully.\n";
    return 0;
}
