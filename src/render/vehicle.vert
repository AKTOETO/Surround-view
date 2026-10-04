#version 300 es
precision highp float;
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 vertex_color;
uniform mat4 mvp;
out vec3 paint;

void main()
{
    gl_Position = mvp * vec4(position, 1);
    paint = vertex_color;
}
