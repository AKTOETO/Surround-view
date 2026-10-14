#pragma once
#include "sv/fusion_runtime.hpp"
#include "sv/surface_runtime.hpp"

namespace sv
{
inline boost::json::object configuration_catalog()
{
    return {{"schema_version", 2},
            {"scope", "config_store_groups"},
            {"operations", boost::json::array{"config_read", "config_catalog", "config_validate",
                                              "config_apply"}},
            {"replaceable_groups", boost::json::array{"fusion", "surface"}},
            {"changes_semantics", "replace_each_supplied_group"},
            {"omitted_groups", "keep_active"},
            {"base_config_revision_required", true},
            {"validation_scope", "parse_config_and_current_session"},
            {"prepares_renderer", false},
            {"reserves_revision", false},
            {"generic_apply_supported", true},
            {"persistence", "explicit_runtime_or_snapshot"},
            {"apply_policy", boost::json::object{{"between_render_calls", true},
                                                 {"prepares_before_persistence", true},
                                                 {"allowed_during_experiment", false},
                                                 {"operation_ids_supported", false},
                                                 {"automatic_retry", false}}},
            {"fusion", fusion_catalog()},
            {"surface", surface_catalog()}};
}
} // namespace sv
