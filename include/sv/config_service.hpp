#pragma once
#include "sv/config_store.hpp"
#include "sv/configuration_catalog.hpp"
#include "sv/output_product.hpp"
#include "sv/protocol.hpp"

namespace sv
{
// Read/discovery/validation only. Mutations remain server-owned.
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

    boost::json::object catalog() const
    {
        return configuration_catalog();
    }

    boost::json::object validate(uint64_t base_revision, const boost::json::object &changes,
                                 const View &current_view, OutputProduct product) const
    {
        const auto state = store_.configuration_state();
        if (base_revision != state.revision)
        {
            throw std::invalid_argument("stale_config_revision");
        }
        if (changes.empty())
        {
            throw std::invalid_argument("configuration_changes_empty");
        }
        auto effective = state.active->effective;
        for (const auto &field : changes)
        {
            if (field.key() != "fusion" && field.key() != "surface")
            {
                throw std::invalid_argument("configuration_group_unsupported");
            }
            effective.as_object()[field.key()] = field.value().as_object();
        }
        const auto candidate = parse_config(effective);
        if (!safe_view(candidate.surface, current_view))
        {
            throw std::invalid_argument("view_clearance");
        }
        if (product != OutputProduct::FinalFrame && candidate.fusion.diagnostic != "color")
        {
            throw std::invalid_argument(product == OutputProduct::SourceVisibility
                                            ? "source_visibility_requires_color"
                                        : product == OutputProduct::SourceDisagreement
                                            ? "source_disagreement_requires_color"
                                            : "render_support_requires_color");
        }
        // Return complete validated groups; no renderer preparation, publication or disk I/O.
        auto validated = changes;
        if (validated.contains("fusion"))
        {
            validated["fusion"] = fusion_settings(candidate.fusion);
        }
        return {{"schema_version", 1},
                {"scope", "config_store_groups"},
                {"base_config_revision", std::to_string(state.revision)},
                {"valid", true},
                {"changes", std::move(validated)},
                {"applied", false},
                {"renderer_prepared", false}};
    }
};
} // namespace sv
