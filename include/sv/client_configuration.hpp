#pragma once
#include "sv/protocol.hpp"

namespace sv::client
{
struct ConfigurationSnapshot
{
    uint64_t revision = 0;
    boost::json::object active, persisted;
    bool has_runtime_changes = false, persistence_configured = false;
};

// Owning result; validates the ACK envelope and snapshot contract, not full Config semantics.
ConfigurationSnapshot parse_configuration_snapshot(const Message &);
} // namespace sv::client
