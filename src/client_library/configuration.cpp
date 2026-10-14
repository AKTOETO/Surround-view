#include "sv/client_configuration.hpp"
#include <algorithm>
#include <stdexcept>

namespace sv::client
{
namespace
{
const boost::json::object &reply_fields(const Message &message, const char *key, const char *scope,
                                        int version = 1)
{
    if (message.type != 21 || !message.payload.empty() || !message.header.at("accepted").as_bool())
    {
        throw std::invalid_argument("configuration result requires accepted ACK");
    }
    (void)parse_decimal_u64(std::string(message.header.at("command_id").as_string()));
    const auto &fields = message.header.at(key).as_object();
    if (!fields.at("schema_version").is_int64() ||
        fields.at("schema_version").as_int64() != version ||
        fields.at("scope").as_string() != scope)
    {
        throw std::invalid_argument("unsupported configuration result schema");
    }
    return fields;
}

std::vector<std::string> strings(const boost::json::value &value)
{
    std::vector<std::string> result;
    for (const auto &item : value.as_array())
    {
        const std::string text(item.as_string());
        if (text.empty() || std::find(result.begin(), result.end(), text) != result.end())
        {
            throw std::invalid_argument("invalid configuration catalog list");
        }
        result.push_back(text);
    }
    if (result.empty())
    {
        throw std::invalid_argument("empty configuration catalog list");
    }
    return result;
}
} // namespace

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

ConfigurationCatalog parse_configuration_catalog(const Message &message)
{
    const auto version =
        message.header.at("configuration_catalog").as_object().at("schema_version").as_int64();
    if (version != 1 && version != 2)
    {
        throw std::invalid_argument("unsupported configuration catalog version");
    }
    const auto &fields =
        reply_fields(message, "configuration_catalog", "config_store_groups", int(version));
    if (fields.at("changes_semantics").as_string() != "replace_each_supplied_group" ||
        fields.at("omitted_groups").as_string() != "keep_active" ||
        !fields.at("base_config_revision_required").as_bool() ||
        fields.at("validation_scope").as_string() != "parse_config_and_current_session" ||
        fields.at("prepares_renderer").as_bool() || fields.at("reserves_revision").as_bool() ||
        fields.at("generic_apply_supported").as_bool() != (version == 2) ||
        fields.at("persistence").as_string() !=
            (version == 2 ? "explicit_runtime_or_snapshot" : "none"))
    {
        throw std::invalid_argument("unsupported configuration catalog policy");
    }
    if (version == 2)
    {
        const auto &policy = fields.at("apply_policy").as_object();
        if (!policy.at("between_render_calls").as_bool() ||
            !policy.at("prepares_before_persistence").as_bool() ||
            policy.at("allowed_during_experiment").as_bool() ||
            policy.at("operation_ids_supported").as_bool() ||
            policy.at("automatic_retry").as_bool())
        {
            throw std::invalid_argument("unsupported configuration apply policy");
        }
    }
    return {strings(fields.at("operations")), strings(fields.at("replaceable_groups")),
            fields.at("fusion").as_object(), fields.at("surface").as_object(), version == 2};
}

ConfigurationValidation parse_configuration_validation(const Message &message)
{
    const auto &fields = reply_fields(message, "configuration_validation", "config_store_groups");
    ConfigurationValidation result;
    result.base_revision =
        parse_decimal_u64(std::string(fields.at("base_config_revision").as_string()));
    if (result.base_revision !=
            parse_decimal_u64(std::string(message.header.at("config_revision").as_string())) ||
        !fields.at("valid").as_bool() || fields.at("applied").as_bool() ||
        fields.at("renderer_prepared").as_bool())
    {
        throw std::invalid_argument("inconsistent configuration validation result");
    }
    result.changes = fields.at("changes").as_object();
    if (result.changes.empty())
    {
        throw std::invalid_argument("empty validated configuration changes");
    }
    for (const auto &field : result.changes)
    {
        if ((field.key() != "fusion" && field.key() != "surface") || !field.value().is_object())
        {
            throw std::invalid_argument("invalid validated configuration group");
        }
    }
    return result;
}

ConfigurationApplication parse_configuration_application(const Message &message)
{
    const auto &fields = reply_fields(message, "configuration_application", "config_store_groups");
    ConfigurationApplication result;
    result.base_revision =
        parse_decimal_u64(std::string(fields.at("base_config_revision").as_string()));
    result.revision = parse_decimal_u64(std::string(fields.at("revision").as_string()));
    result.persisted = fields.at("persisted").as_bool();
    if (!fields.at("applied").as_bool() || result.base_revision == UINT64_MAX ||
        result.revision != result.base_revision + 1 ||
        result.revision !=
            parse_decimal_u64(std::string(message.header.at("config_revision").as_string())))
    {
        throw std::invalid_argument("inconsistent configuration application result");
    }
    return result;
}
} // namespace sv::client
