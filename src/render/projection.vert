#version 300 es
precision highp float;
layout(location = 0) in vec3 point;
uniform mat4 pose;
uniform vec4 intrinsic, k;
uniform vec2 size, limits;
uniform float count;
flat out vec4 answer;

void main()
{
    vec3 p = (pose * vec4(point, 1)).xyz;
    answer = vec4(0);
    if (p.z > limits.y)
    {
        vec2 a = p.xy / p.z;
        float r = length(a), t = atan(r), t2 = t * t;
        if (t <= limits.x)
        {
            float td = t * (1.0 + t2 * (k.x + t2 * (k.y + t2 * (k.z + t2 * k.w))));
            vec2 px = intrinsic.xy * a * (r > 1e-12 ? td / r : 1.0) + intrinsic.zw;
            answer = vec4(
                px, 0,
                float(all(greaterThanEqual(px, vec2(0))) && all(lessThanEqual(px, size - 1.0))));
        }
    }
    gl_Position = vec4(2.0 * (float(gl_VertexID) + .5) / count - 1.0, 0, 0, 1);
    gl_PointSize = 1.0;
}
