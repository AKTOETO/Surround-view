#version 300 es
precision highp float;
layout(location = 0) in vec3 point;
uniform mat4 mvp;
out vec3 world;

void main()
{
    world = point;
    gl_Position = mvp * vec4(point, 1.0);
}
