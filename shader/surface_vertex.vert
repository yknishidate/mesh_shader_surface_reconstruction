#version 460
#include "shared.glsl"

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

void main() {
    uint vertexIndex = gl_VertexIndex;
    if(vertexIndex >= (N+1) * (N+1) * (N+1)){
        return;
    }

    uvec3 vertexIndices = to3D(vertexIndex, N + 1);
    bool surface = surfaceVertices[vertexIndex] == 1;
    if(!surface) {
       gl_PointSize = 0;
       return;
    }
    
    vec3 position = areaOrigin + vertexIndices * cellSize;
    gl_Position = worldToNDC(position);
    gl_PointSize = pushConstants.pointSize;
    outColor = vec4(0.8, 0.8, 0.8, 1.0);

    float density = densities[vertexIndex];
    if(density > pushConstants.isoValue){
        outColor = vec4(1.0, 0.0, 0.0, 1.0);
    }
}
