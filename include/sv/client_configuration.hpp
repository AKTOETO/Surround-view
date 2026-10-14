#pragma once
#include "sv/protocol.hpp"
#include <string>

namespace sv::client
{
struct ConfigurationSnapshot
{
    uint64_t revision = 0;
    std::string server_instance_id;
    boost::json::object active, persisted;
    bool has_runtime_changes = false, persistence_configured = false;
};

// Owning result; validates the ACK envelope and snapshot contract, not full Config semantics.
ConfigurationSnapshot parse_configuration_snapshot(const Message &);

struct ConfigurationCatalog
{
    std::vector<std::string> operations, replaceable_groups;
    boost::json::object fusion, surface;
    bool apply_supported = false;
};

struct ConfigurationValidation
{
    uint64_t base_revision = 0;
    boost::json::object changes;
};

ConfigurationCatalog parse_configuration_catalog(const Message &);
ConfigurationValidation parse_configuration_validation(const Message &);

struct ConfigurationApplication
{
    uint64_t base_revision = 0, revision = 0;
    bool persisted = false;
};

ConfigurationApplication parse_configuration_application(const Message &);

struct ConfigurationOperation
{
    std::string server_instance_id, operation_id, state, reason;
    uint64_t base_revision = 0, revision = 0;
    bool replayed = false, persisted = false;
};

// Also accepts rejected ACKs carrying a retained terminal operation result.
ConfigurationOperation parse_configuration_operation(const Message &);
} // namespace sv::client
