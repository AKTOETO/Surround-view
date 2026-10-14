#pragma once
#include "sv/config_store.hpp"
#include "sv/protocol.hpp"

namespace sv
{
// First ConfigService operation: coherent read only. Mutations remain server-owned.
class ConfigService
{
    const ConfigStore &store_;

  public:

    explicit ConfigService(const ConfigStore &store) : store_(store)
    {
    }

    boost::json::object read() const
    {
        const auto state = store_.configuration_state();
        boost::json::object result{
            {"schema_version", 1},
            {"scope", "config_store"},
            {"revision", std::to_string(state.revision)},
            {"active", state.active->effective},
            {"persisted", state.persisted->effective},
            {"has_runtime_changes", state.active->effective != state.persisted->effective},
            {"persistence_configured", state.persistence_configured}};
        // Reserve room for the ordinary ACK envelope; never truncate a configuration.
        if (boost::json::serialize(result).size() > max_header / 2)
        {
            throw std::length_error("configuration_snapshot_too_large");
        }
        return result;
    }
};
} // namespace sv
