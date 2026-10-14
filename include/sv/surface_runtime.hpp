#pragma once
#include <boost/json.hpp>

namespace sv
{
inline boost::json::object surface_geometry_fields()
{
    auto cells = [](int minimum, int maximum)
    {
        return boost::json::object{{"type", "integer"}, {"minimum", minimum}, {"maximum", maximum}};
    };
    const boost::json::object length{{"type", "number"}, {"unit", "m"}, {"finite", true}};
    return {
        {"rectangular_bowl_v1",
         boost::json::object{
             {"required",
              boost::json::array{"flat_half_length_m", "flat_half_width_m", "outer_half_length_m",
                                 "outer_half_width_m", "corner_height_m", "uniform_cells"}},
             {"length_fields",
              boost::json::array{"flat_half_length_m", "flat_half_width_m", "outer_half_length_m",
                                 "outer_half_width_m", "corner_height_m"}},
             {"length_type", length},
             {"uniform_cells",
              boost::json::object{{"type", "array"}, {"length", 2}, {"items", cells(2, 512)}}},
             {"constraints",
              boost::json::array{"outer > flat > vehicle half extent + mask margin on both axes",
                                 "corner_height_m >= 0",
                                 "current and configured virtual views must clear support"}}}},
        {"dome_floor_v1",
         boost::json::object{
             {"required", boost::json::array{"dome_radius_m", "dome_latitude_cells",
                                             "dome_longitude_cells", "floor_radial_cells"}},
             {"dome_radius_m", length},
             {"dome_latitude_cells", cells(8, 256)},
             {"dome_longitude_cells", cells(16, 512)},
             {"floor_radial_cells", cells(4, 256)},
             {"constraints",
              boost::json::array{"radius > vehicle masked half diagonal",
                                 "virtual views inside dome with 0.25m clearance"}}}},
        {"cylinder_floor_v1",
         boost::json::object{
             {"required", boost::json::array{"radius_m", "height_m", "vertical_cells",
                                             "angular_cells", "floor_radial_cells"}},
             {"radius_m", length},
             {"height_m", length},
             {"vertical_cells", cells(2, 256)},
             {"angular_cells", cells(16, 512)},
             {"floor_radial_cells", cells(4, 256)},
             {"constraints",
              boost::json::array{"radius > vehicle masked half diagonal", "height_m > 0.5",
                                 "virtual views inside enclosure with 0.25m clearance"}}}},
        {"cube_floor_v1",
         boost::json::object{
             {"required", boost::json::array{"half_extent_m", "height_m", "face_cells"}},
             {"half_extent_m", length},
             {"height_m", length},
             {"face_cells", cells(2, 128)},
             {"constraints",
              boost::json::array{"half_extent > vehicle masked half diagonal", "height_m > 0.5",
                                 "virtual views inside enclosure with 0.25m clearance"}}}}};
}

inline boost::json::object surface_catalog()
{
    return boost::json::object{
        {"geometry_fields", surface_geometry_fields()},
        {"types", boost::json::array{"rectangular_bowl_v1", "dome_floor_v1", "cylinder_floor_v1",
                                     "cube_floor_v1"}},
        {"plane", "rectangular_bowl_v1 with corner_height_m=0"},
        {"rectangular_reference_height",
         boost::json::object{{"field", "reference_height_m"},
                             {"unit", "m"},
                             {"minimum", 0.},
                             {"maximum", 5.},
                             {"default", 0.},
                             {"meaning", "assumed support height; not estimated depth"}}},
        {"rectangular_local_support",
         boost::json::object{
             {"field", "local_support_patch"},
             {"fields", boost::json::array{"center_x_m", "center_y_m", "half_length_m",
                                           "half_width_m", "height_m", "transition_m"}},
             {"frame", "vehicle"},
             {"meaning", "supplied local additive height; omit to disable"},
             {"minimum_extent_and_transition_m", .01},
             {"height_range_m", boost::json::array{0., 5.}},
             {"bounds", "plateau and transition must fit inside carrier"}}},
        {"rectangular_source_occlusion",
         boost::json::object{
             {"field", "source_occlusion_box"},
             {"fields", boost::json::array{"min_m", "max_m", "tolerance_m"}},
             {"frame", "vehicle"},
             {"meaning", "one supplied opaque AABB; omit to disable"},
             {"coordinate_range_m", boost::json::array{-100., 100.}},
             {"tolerance_range_m", boost::json::array{0., .1}},
             {"optional_fields", boost::json::array{"mode", "guard_texels", "support_tolerance_m",
                                                    "uncertainty", "require_facing_surface"}},
             {"modes",
              boost::json::array{"center", "bilinear_radial_v1", "bilinear_box_surface_v1"}},
             {"guard_texels_range", boost::json::array{0, 1}},
             {"default_mode", "center"},
             {"default_support_tolerance_m", 1e-5},
             {"support_tolerance_range_m", boost::json::array{0., .05}},
             {"default_require_facing_surface", false},
             {"facing_scope", "box-surface only; any near finite face with "
                              "camera strictly outside its plane"},
             {"uncertainty",
              boost::json::object{
                  {"required_fields", boost::json::array{"radius_m", "ambiguous_policy"}},
                  {"radius_range_m", boost::json::array{0., .05}},
                  {"policies", boost::json::array{"accept", "reject"}},
                  {"scope", "central rays only; footprint unchanged; omit for "
                            "nominal"}}},
             {"footprint_optics", "zero-distortion equidistant only"}}},
        {"apply", "prepare buffers then publish between render calls"}};
}
} // namespace sv
