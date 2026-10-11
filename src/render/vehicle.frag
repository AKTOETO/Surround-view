#version 300 es
precision highp float;
in vec3 paint;
layout(location = 0) out vec4 color;
uniform int inspection_regions;

void main()
{
    color = inspection_regions != 0 ? vec4(5.0 / 255.0, 0, 0, 1) : vec4(paint, 1);
}
