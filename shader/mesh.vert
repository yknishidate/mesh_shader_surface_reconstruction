#version 460
#include "shared.glsl"

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec4 outPos;

void main() {
    vec4 worldPos = vec4(inPosition, 1);
    gl_Position = pushConstants.viewProj * worldPos;
    outColor = vec4(1.0);
    outNormal = inNormal;
    outPos = worldPos;
}
