#version 400
#extension GL_ARB_separate_shader_objects : enable
layout (location = 0) in vec4 fragColor;
layout (location = 0) out vec4 uFragColor;

void main() {
   uFragColor = fragColor;
}
