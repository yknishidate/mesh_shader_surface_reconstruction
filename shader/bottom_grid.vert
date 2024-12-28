#version 460
#include "shared.glsl"
// for debug
#include "marching_cubes_table.glsl"

// in
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

// out
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

void main() {
    outNormal = vec3(0.0);
    if(gl_InstanceIndex >= surfaceCellCount){
        gl_Position = vec4(0);
        outColor = vec4(0);
        return;
    }

    uint cellIndex = surfaceCells[gl_InstanceIndex];
    uvec3 cellIndices = to3D(cellIndex, N);

    vec3 cellPos = areaOrigin + areaSize * (cellIndices / vec3(N)) + cellSize / 2.0;

    float scale = 0.95;
    vec4 worldPos = vec4((inPosition * cellSize * 0.5 * scale + cellPos), 1);

    uint mcCase = computeMarchingCubesCase(cellIndices);
    uint numVerts = vertexCounts[mcCase];
    uint numTris = triangleCounts[mcCase];
    if(numTris == 0){
        gl_Position = vec4(0);
        return;
    }

    gl_Position = pushConstants.viewProj * worldPos;
    outColor = vec4(0.5);
}
