#version 400
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

const vec3 vertices[] = {
    vec3( 0, -1.0, 0),
    vec3(-1, -1.0, 0),
    vec3( 0, -0.5, 0),
    vec3(-1, -0.5, 0),
};

void main() {
    gl_Position = vec4(vertices[gl_VertexIndex], 1) ;
}
