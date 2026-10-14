#pragma once
#include "sv/config_store.hpp"
#include "sv/config_transaction.hpp"
#include "sv/configuration_catalog.hpp"
#include "sv/output_product.hpp"
#include "sv/protocol.hpp"

namespace sv
{
// Server-owned configuration preparation and publication.
class ConfigService
{
    ConfigStore &store_;

    Config checked_candidate(uint64_t base_revision, const boost::json::object &changes,
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
        auto candidate = parse_config(effective);
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
        return candidate;
    }

  public:

    explicit ConfigService(ConfigStore &store) : store_(store)
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
        const auto candidate = checked_candidate(base_revision, changes, current_view, product);
        // Return complete validated groups; no renderer preparation, publication or disk I/O.
        auto validated = changes;
        if (validated.contains("fusion"))
        {
            validated["fusion"] = fusion_settings(candidate.fusion);
        }
        return {{"schema_version", 1},
                {"scope", "config_store_groups"},
                {"base_config_revision", std::to_string(base_revision)},
                {"valid", true},
                {"changes", std::move(validated)},
                {"applied", false},
                {"renderer_prepared", false}};
    }

    boost::json::object apply(uint64_t base_revision, const boost::json::object &changes,
                              bool persist_snapshot, const View &current_view,
                              OutputProduct product, const PrepareConfiguration &prepare)
    {
        const auto candidate = checked_candidate(base_revision, changes, current_view, product);
        if (persist_snapshot && !store_.configuration_state().persistence_configured)
        {
            throw std::runtime_error("configuration_persistence_unavailable");
        }
        if (base_revision == std::numeric_limits<uint64_t>::max())
        {
            throw std::overflow_error("config_revision_exhausted");
        }
        // Allocate the receipt before committing; success publication has no fallible steps.
        boost::json::object result{{"schema_version", 1},
                                   {"scope", "config_store_groups"},
                                   {"base_config_revision", std::to_string(base_revision)},
                                   {"revision", std::to_string(base_revision + 1)},
                                   {"applied", true},
                                   {"persisted", persist_snapshot}};
        auto prepared = prepare(candidate);
        if (!prepared)
        {
            throw std::runtime_error("configuration_prepare_empty");
        }
        std::string error;
        if (!store_.commit_prepared_if_revision(candidate, base_revision, persist_snapshot, error,
                                                [&]() noexcept { prepared->publish(); }))
        {
            throw std::runtime_error(error);
        }
        return result;
    }
};
} // namespace sv
