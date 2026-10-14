#pragma once
#include "sv/fusion_runtime.hpp"
#include "sv/surface_runtime.hpp"

namespace sv
{
inline boost::json::object configuration_catalog()
{
    return {{"schema_version", 1},
            {"scope", "config_store_groups"},
            {"operations", boost::json::array{"config_read", "config_catalog", "config_validate"}},
            {"replaceable_groups", boost::json::array{"fusion", "surface"}},
            {"changes_semantics", "replace_each_supplied_group"},
            {"omitted_groups", "keep_active"},
            {"base_config_revision_required", true},
            {"validation_scope", "parse_config_and_current_session"},
            {"prepares_renderer", false},
            {"reserves_revision", false},
            {"generic_apply_supported", false},
            {"persistence", "none"},
            {"fusion", fusion_catalog()},
            {"surface", surface_catalog()}};
}
} // namespace sv
