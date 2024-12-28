#extension GL_ARB_separate_shader_objects : enable
#extension GL_KHR_vulkan_glsl : enable
#extension GL_EXT_debug_printf : enable

#include "shared.inc"

//#define OUTPUT_MESHLET_INDEX

layout(binding = 1) buffer ParticlePositions
{
    vec4 particlePositions[];
};

layout(binding = 2) buffer BottomGridParticleCounts
{
    uint bottomParticleCounts[];
};

layout(binding = 3) buffer BottomGridParticleIndices
{
    uint bottomParticleIndices[];
};

layout(binding = 4) buffer SurfaceCounts
{
    uint verticesCount;
    uint surfaceCellCount;
    uint surfaceParticleCount;
    uint surfaceVertexCount;
    uint densityCount;
    uint surfaceBlockCount;
};

struct Vertex
{
    vec4 position;
    vec4 normal;
};

layout(binding = 7) buffer TopGridValidCellCounts
{
    uint topValidCellCounts[];
};

layout(binding = 8) buffer SurfaceCells
{
    uint surfaceCells[];
};

layout(binding = 10) buffer SurfaceVertices
{
    uint surfaceVertices[];
};

layout(binding = 11) buffer CompressedVertices
{
    uint compressedVertices[];
};

layout(binding = 12) buffer Density
{
    float densities[];
};

layout(binding = 13) buffer CellVertexNormals
{
    vec4 cellVertexNormals[];
};

layout(binding = 15) buffer DispatchIndirectCommands
{
    uvec4 counts[];
}
dispatchCommand;

layout(binding = 16) buffer SurfaceBlocks
{
    uint surfaceBlocks[];
};

layout(binding = 19) uniform samplerCube envRadianceImage;

layout(binding = 20) uniform sampler2D posImage;

layout(binding = 21) uniform sampler2D colorImage;

layout(binding = 22) uniform samplerCube envIrradianceImage;

float cubic(float x)
{
    return x * x * x;
}

uvec3 to3D(in uint index, in uint num)
{
    uvec3 indices;
    indices.x = index % num;
    indices.y = (index % (num * num)) / num;
    indices.z = index / (num * num);
    return indices;
}

uint to1D(in uvec3 indices, in uint num)
{
    return (num * num * indices.z) + (num * indices.y) + (indices.x);
}

bool isOutOfRange(in ivec3 indices, in uint num)
{
    return any(lessThanEqual(indices, ivec3(-1))) || any(greaterThanEqual(indices, ivec3(num)));
}

bool isBoundary(in uvec3 cellIndices, in uint num)
{
    return any(equal(cellIndices, uvec3(0))) || any(equal(cellIndices, uvec3(num - 1)));
}

const ivec3 neighborCellOffsets[] = ivec3[](ivec3(-1, -1, -1),
                                            ivec3(0, -1, -1),
                                            ivec3(1, -1, -1),
                                            ivec3(-1, 0, -1),
                                            ivec3(0, 0, -1),
                                            ivec3(1, 0, -1),
                                            ivec3(-1, 1, -1),
                                            ivec3(0, 1, -1),
                                            ivec3(1, 1, -1),

                                            ivec3(-1, -1, 0),
                                            ivec3(0, -1, 0),
                                            ivec3(1, -1, 0),
                                            ivec3(-1, 0, 0),
                                            // uvec3( 0,  0,  0),
                                            ivec3(1, 0, 0),
                                            ivec3(-1, 1, 0),
                                            ivec3(0, 1, 0),
                                            ivec3(1, 1, 0),

                                            ivec3(-1, -1, 1),
                                            ivec3(0, -1, 1),
                                            ivec3(1, -1, 1),
                                            ivec3(-1, 0, 1),
                                            ivec3(0, 0, 1),
                                            ivec3(1, 0, 1),
                                            ivec3(-1, 1, 1),
                                            ivec3(0, 1, 1),
                                            ivec3(1, 1, 1));

// Assume that the cell contains particles <- If the particle count is zero, it could still be a
// surface. Assume that the cell is not a boundary
bool isSurface(in uvec3 cellIndices, in uint num)
{
    int offsetSize = int(pushConstants.kernelRadius / cellSize.x);
    int offsetMin = -offsetSize - 1;
    int offsetMax = offsetSize + 1;

    ivec3 neiMins = clamp(ivec3(cellIndices) + ivec3(offsetMin), ivec3(0), ivec3(num - 1));
    ivec3 neiMaxs = clamp(ivec3(cellIndices) + ivec3(offsetMax), ivec3(0), ivec3(num - 1));

    bool allEmpty = true;
    bool allNotEmpty = true;
    for (int x = neiMins.x; x <= neiMaxs.x; x++) {
        for (int y = neiMins.y; y <= neiMaxs.y; y++) {
            for (int z = neiMins.z; z <= neiMaxs.z; z++) {
                ivec3 neighborCellIndices = ivec3(x, y, z);
                uint index = to1D(uvec3(neighborCellIndices), num);
                uint count = bottomParticleCounts[index];
                allEmpty = allEmpty && count == 0u;
                allNotEmpty = allNotEmpty && count > 0u;
            }
        }
    }
    return !(allEmpty || allNotEmpty);
}

bool isSurfaceBlock(uint numCellsContainingParticles)
{
    return numCellsContainingParticles != 0
           && numCellsContainingParticles != (K + 2) * (K + 2) * (K + 2);
}

float rand(in float i)
{
    return mod(4000. * sin(23464.345 * i + 45.345), 1.);
}

bool isOutOfArea(in vec3 worldPos)
{
    return any(lessThan(worldPos, areaOrigin + 1e-4))
           || any(greaterThan(worldPos, areaOrigin + areaSize - 1e-4));
}

// Assume that worldPos in area
uvec3 worldPosToCellIndices(in vec3 worldPos)
{
    return uvec3((worldPos - areaOrigin) * uvec3(N) / areaSize);
}

uint divRoundUp(in uint num, in uint den)
{
    return (num + den - 1) / den;
}

bool isBoundaryVertex(in uvec3 vertexIndices, in uint num)
{
    return any(equal(vertexIndices, uvec3(0))) || any(equal(vertexIndices, uvec3(num)));
}

const int axisTable[12] = int[12](0, 1, 0, 1, 0, 1, 0, 1, 2, 2, 2, 2);
const uvec3 edgeIndicesTable[12] = uvec3[12](uvec3(0, 0, 0),
                                             uvec3(1, 0, 0),
                                             uvec3(0, 1, 0),
                                             uvec3(0, 0, 0),
                                             uvec3(0, 0, 1),
                                             uvec3(1, 0, 1),
                                             uvec3(0, 1, 1),
                                             uvec3(0, 0, 1),
                                             uvec3(0, 0, 0),
                                             uvec3(1, 0, 0),
                                             uvec3(1, 1, 0),
                                             uvec3(0, 1, 0));
uint toSharedEdgeIndex(in uvec3 localCellIndices, in int edgeIndex)
{
    int axis = axisTable[edgeIndex];
    uvec3 edgeIndices = edgeIndicesTable[edgeIndex];

    // localEdgeIndex, Indices: in block
    uvec3 localEdgeIndices;
    localEdgeIndices[0] = localCellIndices[0] + edgeIndices[0];
    localEdgeIndices[1] = localCellIndices[1] + edgeIndices[1];
    localEdgeIndices[2] = localCellIndices[2] + edgeIndices[2];

    uint localEdgeIndex = ((K + 1) * K * localEdgeIndices[(axis + 2) % 3])
                          + (K * localEdgeIndices[(axis + 1) % 3])
                          + (localEdgeIndices[(axis + 0) % 3]) + axis * (K * (K + 1) * (K + 1));

    return localEdgeIndex;
}

vec3 computeNormal(in vec3 v0, in vec3 v1, in vec3 v2)
{
    return normalize(cross(v1 - v0, v2 - v0));
}

const vec3 lightDirection = normalize(vec3(-1, -1.5, 0));
float computeLighting(in vec3 normal)
{
    return max(dot(normal, lightDirection), 0.0) * 0.5 + 0.5;
}

vec4 worldToNDC(vec3 position)
{
    return pushConstants.viewProj * vec4(position, 1);
}

uint getParticleCount(uint cellIndex)
{
    return min(bottomParticleCounts[cellIndex], maxParticlesPerCell);
}

uint getParticleIndex(uint cellIndex, uint localIndex)
{
    return bottomParticleIndices[cellIndex * maxParticlesPerCell + localIndex];
}

vec3 getParticlePosition(uint particleIndex)
{
    return particlePositions[particleIndex].xyz;
}

vec3 gammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0 / 2.2));
}
