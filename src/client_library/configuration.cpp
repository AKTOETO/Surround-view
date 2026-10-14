#include "sv/client_configuration.hpp"
#include <stdexcept>

namespace sv::client
{
ConfigurationSnapshot parse_configuration_snapshot(const Message &message)
{
    const auto &header = message.header;
    if (message.type != 21 || !message.payload.empty() || !header.at("accepted").as_bool())
    {
        throw std::invalid_argument("configuration snapshot requires accepted ACK");
    }
    (void)parse_decimal_u64(std::string(header.at("command_id").as_string()));
    const auto &fields = header.at("configuration").as_object();
    if (!fields.at("schema_version").is_int64() || fields.at("schema_version").as_int64() != 1 ||
        fields.at("scope").as_string() != "config_store")
    {
        throw std::invalid_argument("unsupported configuration snapshot schema");
    }
    ConfigurationSnapshot result;
    result.revision = parse_decimal_u64(std::string(fields.at("revision").as_string()));
    if (result.revision != parse_decimal_u64(std::string(header.at("config_revision").as_string())))
    {
        throw std::invalid_argument("configuration snapshot revision mismatch");
    }
    result.active = fields.at("active").as_object();
    result.persisted = fields.at("persisted").as_object();
    result.has_runtime_changes = fields.at("has_runtime_changes").as_bool();
    result.persistence_configured = fields.at("persistence_configured").as_bool();
    if (result.has_runtime_changes != (result.active != result.persisted))
    {
        throw std::invalid_argument("configuration snapshot change flag mismatch");
    }
    return result;
}
} // namespace sv::client
