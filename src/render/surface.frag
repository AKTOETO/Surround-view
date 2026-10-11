#version 300 es
precision highp float;
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
uniform int fusion_mode, diagnostic_mode;
uniform int sample_camera;
uniform int inspection_regions;
uniform float edge_width_px, angle_power;

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

void main()
{
    if (inspection_regions != 0)
    {
        int region = surface_mode == 1 ? 2 : (world.z > 1e-6 ? 4 : 1);
        if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
        {
            region = 3;
        }
        color = vec4(float(region) / 255.0, 0, 0, 1);
        return;
    }
    if (surface_mode == 0 && abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
    {
        if (sample_camera >= 0) { color = vec4(0); return; }
        color = diagnostic_mode == 0 ? vec4(.2, .22, .24, 1) : vec4(1, 0, 1, 1);
        return;
    }
    vec3 sum = vec3(0);
    float total = 0.0;
    float best = -1.0;
    int winner = -1, observed = 0;
    vec3 selected = vec3(0), contributions = vec3(0);
    for (int i = 0; i < 4; i++)
    {
        if (available[i] == 0 || (sample_camera >= 0 && i != sample_camera))
        {
            continue;
        }
        vec3 p = (pose[i] * vec4(world, 1)).xyz;
        if (p.z <= limits[i].y)
        {
            continue;
        }
        vec2 ab = p.xy / p.z;
        float r = length(ab), theta = atan(r);
        if (theta > limits[i].x)
        {
            continue;
        }
        float t2 = theta * theta;
        vec4 k = distortion[i];
        float td = theta * (1.0 + t2 * (k.x + t2 * (k.y + t2 * (k.z + t2 * k.w))));
        vec2 px = intrinsics[i].xy * ab * (r > 1e-12 ? td / r : 1.0) + intrinsics[i].zw;
        if (any(lessThan(px, vec2(0))) || any(greaterThan(px, sizes[i] - 1.0)))
        {
            continue;
        }
        vec2 uv = (px + .5) / sizes[i];
        observed++;
        vec3 x;
        if (i == 0)
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
        sum += w * linearize(x);
        contributions += w * camera_color(i);
        total += w;
    }
    if (sample_camera >= 0) { color = vec4(0); return; }
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
