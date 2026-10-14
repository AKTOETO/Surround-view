#pragma once
#include "sv/protocol.hpp"
#include <string>

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

struct ConfigurationCatalog
{
    std::vector<std::string> operations, replaceable_groups;
    boost::json::object fusion, surface;
};

struct ConfigurationValidation
{
    uint64_t base_revision = 0;
    boost::json::object changes;
};

ConfigurationCatalog parse_configuration_catalog(const Message &);
ConfigurationValidation parse_configuration_validation(const Message &);
} // namespace sv::client
