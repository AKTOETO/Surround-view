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
    if (abs(world.x) <= vehicle.x && abs(world.y) <= vehicle.y)
    {
        color = vec4(.2, .22, .24, 1);
        return;
    }
    vec3 sum = vec3(0);
    float total = 0.0;
    for (int i = 0; i < 4; i++)
    {
        if (available[i] == 0)
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
        float w = clamp(edge / 24.0, 0.0, 1.0);
        sum += w * linearize(x);
        total += w;
    }
    color = total > 1e-6 ? vec4(encode(sum / total), 1) : vec4(.86, .91, .95, 0);
}
