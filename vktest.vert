#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 color;
layout (location = 2) in ivec3 instance_pos;

layout (location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 matrix;
} ubuf;

void main() {
    fragColor = color;
    vec4 p = pos - vec4(instance_pos * 2, 0);
    gl_Position = ubuf.matrix * p;
}
