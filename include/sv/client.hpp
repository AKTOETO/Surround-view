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
    // IDs are scoped to this Client instance; pending commands are never replayed.
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

    uint64_t pause()
    {
        return command("pause");
    }

    uint64_t resume()
    {
        return command("resume");
    }

    uint64_t step()
    {
        return command("step");
    }

    uint64_t state()
    {
        return command("state");
    }

    // Release a delivered frame after consuming/copying it; old-session releases are ignored.
    void release(const boost::json::object &frame);
    // Blocking shutdown; call from the owning thread, never from a callback.
    void stop();
};
} // namespace sv::client
