#version 300 es
precision highp float;
flat in vec4 answer;
layout(location=0) out vec4 color;
void main(){color=answer;}
