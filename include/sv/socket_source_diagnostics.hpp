#pragma once
#include <array>
#include <boost/json.hpp>
#include <cstdint>
#include <optional>
#include <string>

namespace sv
{
struct SocketSourceCounters
{
    uint64_t accepted = 0, refused = 0, handshakes = 0, closed = 0;
    uint64_t rejected = 0, timeouts = 0, received = 0, dropped = 0;

    uint64_t reconnects() const
    {
        return handshakes ? handshakes - 1 : 0;
    }
};

struct SocketSourceConnection
{
    std::string state = "disconnected";
    std::optional<std::string> session_id, last_close_reason;
};

struct SocketSourceDiagnostics
{
    SocketSourceCounters counters;
    SocketSourceConnection connection;
};

using SocketSourceDiagnosticsSet = std::array<SocketSourceDiagnostics, 4>;

inline boost::json::value socket_source_diagnostics_json(
    const std::optional<SocketSourceDiagnosticsSet> &value)
{
    if (!value)
    {
        return nullptr;
    }
    boost::json::array cameras;
    for (size_t id = 0; id < 4; ++id)
    {
        const auto &c = value->at(id).counters;
        const auto &s = value->at(id).connection;
        cameras.emplace_back(boost::json::object{
            {"camera_id", id},
            {"counters", boost::json::object{{"accepted", std::to_string(c.accepted)},
                                             {"refused", std::to_string(c.refused)},
                                             {"handshakes", std::to_string(c.handshakes)},
                                             {"closed", std::to_string(c.closed)},
                                             {"rejected", std::to_string(c.rejected)},
                                             {"timeouts", std::to_string(c.timeouts)},
                                             {"received", std::to_string(c.received)},
                                             {"dropped", std::to_string(c.dropped)}}},
            {"connection",
             boost::json::object{{"state", s.state},
                                 {"session_id", s.session_id ? boost::json::value(*s.session_id)
                                                             : boost::json::value(nullptr)},
                                 {"last_close_reason",
                                  s.last_close_reason ? boost::json::value(*s.last_close_reason)
                                                      : boost::json::value(nullptr)}}}});
    }
    return cameras;
}
} // namespace sv
