#version 460
#include "shared.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

void main() {
    vec3 position = getParticlePosition(gl_VertexIndex);
    gl_Position = worldToNDC(position);
    gl_PointSize = pushConstants.pointSize;
    outColor = vec4(0.3, 0.3, 1.0, 1.0);
}
