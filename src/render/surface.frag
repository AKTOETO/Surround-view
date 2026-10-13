#version 300 es
precision highp float;
precision highp int;
in vec3 world;
layout(location = 0) out vec4 color;
uniform sampler2D input0, input1, input2, input3;
uniform mat4 pose[4];
uniform vec4 intrinsics[4], distortion[4];
uniform vec2 sizes[4], limits[4];
uniform ivec4 available;
uniform vec2 vehicle;
uniform int surface_mode;
uniform float dome_radius;
uniform int fusion_mode, diagnostic_mode, fallback_hatch;
uniform int sample_camera;
uniform int inspection_regions;
uniform float reference_height;
uniform int inspection_camera;
uniform float edge_width_px, angle_power;
uniform int source_occlusion_enabled, source_occlusion_policy, source_guard_texels;
uniform vec3 source_box_min, source_box_max, camera_origin[4];
uniform float source_occlusion_tolerance, source_support_tolerance;
uniform float source_uncertainty_radius;
uniform int source_ambiguity_policy;
uniform int source_require_facing;

// Finite segment / opaque AABB. Parameter t is a fraction of camera-to-carrier distance.
bool box_segment_occluded(vec3 origin, vec3 endpoint, float inset_m, bool closed_degenerate)
{
    vec3 direction = endpoint - origin;
    float distance_m = length(direction);
    if (distance_m <= source_occlusion_tolerance) { return false; }
    vec3 lo = source_box_min + vec3(inset_m), hi = source_box_max - vec3(inset_m);
    if (any(greaterThan(lo, hi)) || (!closed_degenerate && any(equal(lo, hi)))) { return false; }
    float entry = 0.0, exit = 1.0;
    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(direction[axis]) < 1e-8)
        {
            if (origin[axis] < lo[axis] || origin[axis] > hi[axis])
            {
                return false;
            }
        }
        else
        {
            float a = (lo[axis] - origin[axis]) / direction[axis];
            float b = (hi[axis] - origin[axis]) / direction[axis];
            entry = max(entry, min(a, b));
            exit = min(exit, max(a, b));
        }
    }
    return entry <= exit && entry < 1.0 - source_occlusion_tolerance / distance_m;
}

bool segment_occluded(vec3 origin, vec3 endpoint, float inset_m)
{
    // Preserve the historical nominal/footprint empty-interior convention.
    return box_segment_occluded(origin, endpoint, inset_m, false);
}

// Known supplied box surface with an explicit, independently configured support band.
bool on_source_box_surface(vec3 point)
{
    float epsilon_m = source_support_tolerance;
    if (any(lessThan(point, source_box_min - vec3(epsilon_m))) ||
        any(greaterThan(point, source_box_max + vec3(epsilon_m)))) { return false; }
    vec3 face_distance = min(abs(point - source_box_min), abs(point - source_box_max));
    return min(face_distance.x, min(face_distance.y, face_distance.z)) <= epsilon_m;
}

bool source_face_faces_camera(vec3 origin, vec3 point)
{
    // Caller has checked finite face-band membership. Retain ALL near faces at corners.
    for (int axis = 0; axis < 3; ++axis)
    {
        if (abs(point[axis] - source_box_min[axis]) <= source_support_tolerance &&
            origin[axis] < source_box_min[axis]) { return true; }
        if (abs(point[axis] - source_box_max[axis]) <= source_support_tolerance &&
            origin[axis] > source_box_max[axis]) { return true; }
    }
    return false;
}

// Neighbouring rays terminate at the central carrier radial range.
// Policy2 preserves a visible carrier endpoint on the supplied box itself.
// Parser restricts this inverse to zero-distortion equidistant optics.
int source_central_rejection(int camera)
{
    if (source_occlusion_enabled == 0) { return 0; }
    vec3 origin = camera_origin[camera];
    if (source_ambiguity_policy == 0 || source_uncertainty_radius == 0.0)
    {
        if (segment_occluded(origin, world, 0.0)) { return 7; }
    }
    else
    {
        // Sufficient certificates for supplied face-coordinate error. Fixed carrier and poses.
        if (box_segment_occluded(origin, world, source_uncertainty_radius, true)) { return 7; }
        bool ambiguous = box_segment_occluded(origin, world, -source_uncertainty_radius, true);
        if (ambiguous && source_ambiguity_policy == 2) { return 8; }
    }
    return 0;
}

int source_rejection(int camera, vec2 px)
{
    if (source_occlusion_enabled == 0) { return 0; }
    int central = source_central_rejection(camera);
    if (central != 0) { return central; }
    vec3 origin = camera_origin[camera];
    if (source_occlusion_policy == 0) { return 0; }
    // Membership only bypasses the footprint stage, after the selected central policy.
    if (source_occlusion_policy == 2 && on_source_box_surface(world))
    {
        if (source_require_facing != 0 && !source_face_faces_camera(origin, world)) { return 9; }
        return 0;
    }
    vec2 base = floor(px), fraction = px - base;
    float distance_m = length(world - origin);
    for (int y = -1; y <= 2; ++y)
    {
        for (int x = -1; x <= 2; ++x)
        {
            if (x < -source_guard_texels || x > 1 + source_guard_texels ||
                y < -source_guard_texels || y > 1 + source_guard_texels) { continue; }
            if (source_guard_texels == 0)
            {
                float weight = (x == 0 ? 1.0 - fraction.x : fraction.x) *
                               (y == 0 ? 1.0 - fraction.y : fraction.y);
                if (weight <= 0.0) { continue; }
            }
            vec2 texel = clamp(base + vec2(x, y), vec2(0), sizes[camera] - 1.0);
            vec2 ab = (texel - intrinsics[camera].zw) / intrinsics[camera].xy;
            float theta = length(ab);
            float scale = theta > 1e-12 ? sin(theta) / theta : 1.0;
            vec3 ray = transpose(mat3(pose[camera])) * vec3(ab * scale, cos(theta));
            if (segment_occluded(origin, origin + ray * distance_m, 1e-5)) { return 7; }
        }
    }
    return 0;
}

vec3 camera_color(int i)
{
    if (i == 0)
    {
        return vec3(1, 0, 0);
    }
    if (i == 1)
    {
        return vec3(0, 1, 0);
    }
    if (i == 2)
    {
        return vec3(0, 0, 1);
    }
    return vec3(1, 1, 0);
}

vec3 linearize(vec3 x)
{
    return mix(x / 12.92, pow((x + .055) / 1.055, vec3(2.4)), step(vec3(.04045), x));
}

vec3 encode(vec3 x)
{
    return mix(x * 12.92, 1.055 * pow(max(x, vec3(0)), vec3(1.0 / 2.4)) - .055,
               step(vec3(.0031308), x));
}

// Shared by normal rendering and diagnostic captures: no second projection model.
int source_projection(int i, out vec2 px, out float theta)
{
    px = vec2(0);
    theta = 0.0;
    if (available[i] == 0) { return 2; }
    vec3 p = (pose[i] * vec4(world, 1)).xyz;
    if (p.z <= limits[i].y)
    {
        return 3;
    }
    vec2 ab = p.xy / p.z;
    float r = length(ab);
    theta = atan(r);
    if (theta > limits[i].x)
    {
        return 4;
    }
    float t2 = theta * theta;
    vec4 k = distortion[i];
    float td = theta * (1.0 + t2 * (k.x + t2 * (k.y + t2 * (k.z + t2 * k.w))));
    px = intrinsics[i].xy * ab * (r > 1e-12 ? td / r : 1.0) + intrinsics[i].zw;
    if (any(lessThan(px, vec2(0))) || any(greaterThan(px, sizes[i] - 1.0)))
    {
        return 5;
    }
    int rejection = source_rejection(i, px);
    return rejection == 0 ? 1 : rejection;
}

vec4 pack_float(float value)
{
    uint bits = floatBitsToUint(value);
    return vec4(uvec4(bits, bits >> 8u, bits >> 16u, bits >> 24u) & uvec4(255u)) / 255.0;
}

void main()
{
    if (inspection_regions == 9)
    {
        if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
        {
            color = vec4(6.0 / 255.0);
            return;
        }
        vec4 statuses = vec4(0);
        for (int camera = 0; camera < 4; ++camera)
        {
            vec2 px;
            float theta;
            int status = source_projection(camera, px, theta);
            if (status == 7 && source_central_rejection(camera) == 0) { status = 12; }
            if (status == 1 && source_occlusion_enabled != 0 &&
                source_ambiguity_policy != 0 && source_uncertainty_radius > 0.0)
            {
                bool ambiguous = box_segment_occluded(camera_origin[camera], world,
                                                      -source_uncertainty_radius, true);
                status = ambiguous ? 10 : 11;
            }
            statuses[camera] = float(status) / 255.0;
        }
        color = statuses;
        return;
    }
    if (inspection_regions >= 5 && inspection_regions != 8 && inspection_regions != 10)
    {
        vec2 px;
        float theta;
        int status = source_projection(inspection_camera, px, theta);
        if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
        {
            status = 6;
        }
        color = inspection_regions == 7 ? vec4(float(status) / 255.0, 0, 0, 1)
            : pack_float(status == 1 ? px[inspection_regions - 5] : uintBitsToFloat(0x7fc00000u));
        return;
    }
    if (inspection_regions >= 2 && inspection_regions != 8 && inspection_regions != 10)
    {
        // Pack one perspective-interpolated float into RGBA8. No float FBO extension.
        uint bits = floatBitsToUint(world[inspection_regions - 2]);
        uvec4 bytes = uvec4(bits, bits >> 8u, bits >> 16u, bits >> 24u) & uvec4(255u);
        color = vec4(bytes) / 255.0;
        return;
    }
    if (inspection_regions == 1)
    {
        int region = surface_mode == 1 ? 2 : (world.z - reference_height > 1e-6 ? 4 : 1);
        if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
        {
            region = 3;
        }
        color = vec4(float(region) / 255.0, 0, 0, 1);
        return;
    }
    if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
    {
        if (inspection_regions == 8) { color = vec4(0, 0, 1.0 / 255.0, 1); return; }
        if (inspection_regions == 10) { color = vec4(0); return; }
        if (sample_camera >= 0) { color = vec4(0); return; }
        color = diagnostic_mode == 0 ? vec4(.2, .22, .24, 1) : vec4(1, 0, 1, 1);
        return;
    }
    vec3 source_min = vec3(1), source_max = vec3(0);
    vec3 sum = vec3(0);
    float total = 0.0;
    float best = -1.0;
    int winner = -1, observed = 0, positive = 0;
    vec3 selected = vec3(0), contributions = vec3(0);
    for (int i = 0; i < 4; i++)
    {
        if (available[i] == 0 || (sample_camera >= 0 && i != sample_camera))
        {
            continue;
        }
        vec2 px;
        float theta;
        if (source_projection(i, px, theta) != 1) { continue; }
        vec2 uv = (px + .5) / sizes[i];
        observed++;
        vec3 x = vec3(0);
        if (inspection_regions == 8)
        {
            // Support is independent of image brightness; no texture fetch required.
        }
        else if (i == 0)
        {
            x = texture(input0, uv).rgb;
        }
        else if (i == 1)
        {
            x = texture(input1, uv).rgb;
        }
        else if (i == 2)
        {
            x = texture(input2, uv).rgb;
        }
        else
        {
            x = texture(input3, uv).rgb;
        }
        source_min = min(source_min, x);
        source_max = max(source_max, x);
        float edge = min(min(px.x, px.y), min(sizes[i].x - 1.0 - px.x, sizes[i].y - 1.0 - px.y));
        if (sample_camera >= 0)
        {
            // RGB8 sRGB samples; alpha=0 invalid, 1..255 packs valid edge weight.
            color = vec4(x, (1.0 + 254.0 * clamp(edge / edge_width_px, 0.0, 1.0)) / 255.0);
            return;
        }
        float angle_weight = pow(max(cos(theta), 0.0), angle_power);
        if (fusion_mode == 1)
        {
            // Strict comparison gives the lower camera ID a deterministic tie break.
            if (angle_weight > best)
            {
                best = angle_weight;
                winner = i;
                selected = x;
            }
            continue;
        }
        float w = clamp(edge / edge_width_px, 0.0, 1.0);
        if (fusion_mode == 2)
        {
            w *= angle_weight;
        }
        if (w > 0.0) { positive++; }
        sum += w * linearize(x);
        contributions += w * camera_color(i);
        total += w;
    }
    if (sample_camera >= 0) { color = vec4(0); return; }
    if (inspection_regions == 10)
    {
        vec3 spread = source_max - source_min;
        color = vec4(observed >= 2 ? max(spread.r, max(spread.g, spread.b)) : 0.0,
                     float(observed) / 255.0, observed >= 2 ? 1.0 : 0.0, 1.0);
        return;
    }
    if (inspection_regions == 8)
    {
        bool hard_selected = fusion_mode == 1 && winner >= 0;
        bool fused = hard_selected || total > 1e-6;
        color = vec4(float(observed) / 255.0,
                     float(hard_selected ? 1 : positive) / 255.0,
                     float(fused ? 4 : 3) / 255.0, 1);
        return;
    }
    if (diagnostic_mode == 1)
    {
        // Display source validity independently from blending weights and fallback.
        color = vec4(vec3(float(observed) / 4.0), 1);
        return;
    }
    if (fusion_mode == 1 && winner >= 0)
    {
        color = vec4(diagnostic_mode == 2 ? camera_color(winner) : selected, 1);
        return;
    }
    if (diagnostic_mode == 2)
    {
        color = vec4(total > 1e-6 ? contributions / total : vec3(0), 1);
        return;
    }
    if (total > 1e-6)
    {
        color = vec4(encode(sum / total), 1);
    }
    else if (fallback_hatch != 0)
    {
        // Explicit display of this branch only; not a scene observability test.
        float tile = mod(floor(gl_FragCoord.x / 8.0) + floor(gl_FragCoord.y / 8.0), 2.0);
        color = vec4(mix(vec3(1, 0, 1), vec3(.15, .05, .15), tile), 1);
    }
    else if (surface_mode == 1)
    {
        float elevation = clamp(world.z / max(dome_radius, 1e-6), 0.0, 1.0);
        color = vec4(mix(vec3(.66, .73, .78), vec3(.17, .27, .39),
                         smoothstep(.35, .97, elevation)), 1);
    }
    else
    {
        color = vec4(.23, .24, .24, 1);
    }
}
