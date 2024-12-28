#version 460
#include "shared.glsl"

// in
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// out
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

void main() {
    outNormal = vec3(0.0);

    // Use computed surfaceBlocks[]
    if(gl_InstanceIndex >= surfaceBlockCount){
        gl_Position = vec4(0);
        outColor = vec4(0);
        return;
    }

    uint blockIndex = surfaceBlocks[gl_InstanceIndex];
    uvec3 blockIndices = to3D(blockIndex, M);
    vec3 blockPos = areaOrigin + areaSize * (blockIndices / vec3(M)) + blockSize / 2.0;

    vec4 worldPos = vec4((inPosition * blockSize * 0.5 + blockPos), 1);
    gl_Position = pushConstants.viewProj * worldPos;
    outColor = vec4(0, 1, 0, 1);
}
