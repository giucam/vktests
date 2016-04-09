#version 400
#extension GL_ARB_separate_shader_objects : enable
layout (location = 0) out vec4 uFragColor;

void main() {
   uFragColor = vec4(0.8,0.0,0.8,0.6);
}
