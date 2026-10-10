#pragma once
#include "sv/protocol.hpp"
#include <functional>
#include <memory>
#include <string>

namespace sv::client
{
struct Endpoint
{
    enum class Transport
    {
        Unix,
        Tcp
    };
    Transport transport = Transport::Unix;
    std::string directory = "/tmp/sv-prototype", host = "127.0.0.1";
    uint16_t control_port = 0, data_port = 0;
};

struct Options
{
    Endpoint endpoint;
    unsigned timeout_ms = 2000, reconnect_ms = 1000, max_retries = 10;
    size_t command_capacity = 64;
};

struct FusionSettings
{
    std::string mode = "edge_feather", diagnostic = "color";
    double edge_width_px = 24, angle_power = 2, smoothness_weight = .1;
    unsigned pyramid_levels = 4;
    std::string pyramid_boundary = "zero";
};

struct Event
{
    // Callbacks run on the library worker; marshal GUI work to its own thread.
    enum class Kind
    {
        State,
        Message,
        Error
    };
    Kind kind = Kind::State;
    std::string detail;
    std::shared_ptr<const sv::Message> message;
};

class Client
{
    struct Impl;
    std::unique_ptr<Impl> impl_;

  public:

    using Handler = std::function<void(Event)>;
    explicit Client(Options, Handler);
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    // Concurrent submissions are ordered by ID; IDs belong to this instance.
    // Pending commands are never replayed. Exhaustion throws without wrapping.
    uint64_t command(std::string type, boost::json::object parameters = {});

    uint64_t orbit(double azimuth_delta_rad, double elevation_delta_rad)
    {
        return command("orbit", {{"azimuth_delta_rad", azimuth_delta_rad},
                                 {"elevation_delta_rad", elevation_delta_rad}});
    }

    uint64_t zoom(double distance_delta_m)
    {
        return command("zoom", {{"distance_delta_m", distance_delta_m}});
    }

    uint64_t preset(std::string name)
    {
        return command("preset", {{"name", std::move(name)}});
    }

    uint64_t pause(std::string lease_id = {})
    {
        return command("pause", {{"lease_id", std::move(lease_id)}});
    }

    uint64_t resume(std::string lease_id = {})
    {
        return command("resume", {{"lease_id", std::move(lease_id)}});
    }

    uint64_t step(std::string lease_id = {})
    {
        return command("step", lease_id.empty()
                                   ? boost::json::object{}
                                   : boost::json::object{{"lease_id", std::move(lease_id)}});
    }

    uint64_t state()
    {
        return command("state");
    }

    uint64_t fusion_catalog()
    {
        return command("fusion_catalog");
    }

    // Temporary experiment configuration; only the server publishes its snapshot.
    uint64_t configure_fusion(uint64_t base_config_revision, const FusionSettings &fusion,
                              std::string lease_id = {})
    {
        boost::json::object fields{{"mode", fusion.mode},
                                   {"diagnostic", fusion.diagnostic},
                                   {"edge_width_px", fusion.edge_width_px},
                                   {"angle_power", fusion.angle_power},
                                   {"smoothness_weight", fusion.smoothness_weight},
                                   {"pyramid_levels", fusion.pyramid_levels}};
        // Omit the historical default for compatibility with older servers.
        if (fusion.pyramid_boundary != "zero")
        {
            fields["pyramid_boundary"] = fusion.pyramid_boundary;
        }
        return command("configure_fusion",
                       {{"base_config_revision", std::to_string(base_config_revision)},
                        {"lease_id", std::move(lease_id)},
                        {"fusion", std::move(fields)}});
    }

    uint64_t surface_catalog()
    {
        return command("surface_catalog");
    }

    uint64_t configure_surface(uint64_t base_config_revision, boost::json::object surface,
                               std::string lease_id = {})
    {
        return command("configure_surface",
                       {{"base_config_revision", std::to_string(base_config_revision)},
                        {"surface", std::move(surface)},
                        {"lease_id", std::move(lease_id)}});
    }

    uint64_t acquire_experiment(unsigned ttl_ms = 5000)
    {
        return command("experiment_acquire", {{"ttl_ms", ttl_ms}});
    }

    uint64_t renew_experiment(std::string lease_id)
    {
        return command("experiment_renew", {{"lease_id", std::move(lease_id)}});
    }

    // ACK starts restoration; poll state.experiment_lease until idle or failed.
    uint64_t release_experiment(std::string lease_id)
    {
        return command("experiment_release", {{"lease_id", std::move(lease_id)}});
    }

    // Release a delivered frame after consuming/copying it; old-session releases are ignored.
    // Uses an independent bounded submission budget, so queued commands cannot starve release.
    void release(const boost::json::object &frame);
    // Blocking shutdown; call from the owning thread, never from a callback.
    void stop();
};
} // namespace sv::client
