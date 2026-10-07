#include "sv/config_store.hpp"
#include "sv/server_session.hpp"
#include <cassert>
#include <iostream>

int main()
{
    sv::Config config;
    config.width = 1280;
    config.height = 720;
    config.profile_id = "test-profile";

    sv::ConfigStore store(config);
    assert(store.revision() == 0);
    assert(store.active()->width == 1280);

    sv::Config updated = config;
    updated.width = 1920;
    std::string err;
    assert(store.update(updated, err));
    assert(store.revision() == 1);
    assert(store.active()->width == 1920);

    sv::SessionRegistry registry;
    sv::View default_view;
    default_view.azimuth = 1.0;

    auto session = registry.create_session("sess-123", "tok-456", default_view);
    assert(session != nullptr);
    assert(session->session_id == "sess-123");
    assert(session->is_control_only == true);
    assert(registry.count() == 1);

    assert(registry.attach_data_channel("sess-123", "tok-456"));
    assert(session->is_control_only == false);

    assert(!registry.attach_data_channel("sess-123", "wrong-token"));
    assert(!registry.attach_data_channel("unknown", "tok-456"));

    registry.remove_session("sess-123");
    assert(registry.count() == 0);

    std::cout << "All ConfigStore and SessionRegistry tests passed successfully.\n";
    return 0;
}
