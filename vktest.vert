#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable
layout (location = 0) in vec4 pos;
layout (location = 1) in vec4 color;
layout (location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    float angle;
} ubuf;

void main() {
    fragColor = color;
    mat2 rot = mat2(cos(ubuf.angle), -sin(ubuf.angle), sin(ubuf.angle), cos(ubuf.angle));
    gl_Position = vec4(rot * pos.xy, pos.z, pos.w);
}
