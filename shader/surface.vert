#version 460
#include "shared.glsl"

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outPos;

void main() {
    vec4 worldPos = inPosition;
    vec4 normal = inNormal * -1.0;
    if(pushConstants.polygonMode == 1){
        worldPos += normal * 0.001;
    }
    gl_Position = pushConstants.viewProj * worldPos;
    gl_PointSize = pushConstants.pointSize;
    outPos = worldPos;
    outColor = worldPos;
    outNormal = normal;
}
