#include "socket_source_diagnostics.hpp"
#include "sv/protocol.hpp"
#include <algorithm>
#include <stdexcept>

namespace sv::client
{
namespace
{
void keys(const boost::json::object &object, std::initializer_list<const char *> expected)
{
    if (object.size() != expected.size())
    {
        throw std::invalid_argument("invalid socket diagnostics fields");
    }
    for (const auto *key : expected)
    {
        if (!object.contains(key))
        {
            throw std::invalid_argument("missing socket diagnostics field");
        }
    }
}
} // namespace

std::optional<SocketSourceDiagnosticsSet> parse_socket_source_diagnostics(
    const boost::json::value &value)
{
    if (value.is_null())
    {
        return {};
    }
    const auto &array = value.as_array();
    if (array.size() != 4)
    {
        throw std::invalid_argument("socket diagnostics requires four cameras");
    }
    SocketSourceDiagnosticsSet result;
    for (unsigned id = 0; id < 4; ++id)
    {
        const auto &camera = array[id].as_object();
        keys(camera, {"camera_id", "counters", "connection"});
        const auto &index = camera.at("camera_id");
        if ((!index.is_int64() && !index.is_uint64()) || index.to_number<uint64_t>() != id)
        {
            throw std::invalid_argument("invalid socket diagnostics camera identity");
        }
        const auto &counts = camera.at("counters").as_object();
        keys(counts, {"accepted", "refused", "handshakes", "closed", "rejected", "timeouts",
                      "received", "dropped"});
        auto number = [&](const char *key)
        { return parse_decimal_u64(std::string(counts.at(key).as_string())); };
        auto &c = result[id].counters;
        c = {number("accepted"), number("refused"),  number("handshakes"), number("closed"),
             number("rejected"), number("timeouts"), number("received"),   number("dropped")};
        const auto &fields = camera.at("connection").as_object();
        keys(fields, {"state", "session_id", "last_close_reason"});
        auto &s = result[id].connection;
        s.state = std::string(fields.at("state").as_string());
        if (s.state != "disconnected" && s.state != "handshaking" && s.state != "ready")
        {
            throw std::invalid_argument("invalid socket connection state");
        }
        if (!fields.at("session_id").is_null())
        {
            s.session_id = std::string(fields.at("session_id").as_string());
            if (s.session_id->empty() || s.session_id->size() > 128 ||
                s.session_id->find('\0') != std::string::npos)
            {
                throw std::invalid_argument("invalid socket session identity");
            }
        }
        if (!fields.at("last_close_reason").is_null())
        {
            s.last_close_reason = std::string(fields.at("last_close_reason").as_string());
            const auto &reason = *s.last_close_reason;
            if (reason != "peer_disconnected" && reason != "handshake_write_failed" &&
                reason != "protocol_rejected" && reason != "message_timeout" &&
                reason != "source_stopped")
            {
                throw std::invalid_argument("unknown socket close reason");
            }
        }
        if (c.closed > c.accepted ||
            c.accepted - c.closed != (s.state == "disconnected" ? 0U : 1U) ||
            c.handshakes > c.accepted || (s.state == "ready" && !c.handshakes) ||
            bool(s.session_id) != (s.state == "ready") ||
            bool(s.last_close_reason) != bool(c.closed) || c.rejected > c.closed ||
            c.timeouts > c.closed - c.rejected || c.dropped > c.received)
        {
            throw std::invalid_argument("inconsistent socket diagnostics counters/state");
        }
    }
    return result;
}
} // namespace sv::client
