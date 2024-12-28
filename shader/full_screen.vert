#version 460

vec3 positions[] = vec3[](vec3(-1, -1, 0), vec3(+3, -1, 0), vec3(-1, +3, 0));

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 1);
}
