#pragma once
#include "sv/socket_source_diagnostics.hpp"

namespace sv::client
{
std::optional<SocketSourceDiagnosticsSet> parse_socket_source_diagnostics(
    const boost::json::value &);
}
