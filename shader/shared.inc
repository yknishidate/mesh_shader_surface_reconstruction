#ifdef __cplusplus
#pragma once
using glm::vec3;
using uint = uint32_t;
#endif

// Cell
const int N = 128; // cell resolution of entire area
const uint maxParticlesPerCell = 16;

const int K = 4; // resolution of block
const int M = N / K;
const int KC = K * K * K;             // K^3:          number of cells
const int KE = 3 * K * (K+1) *(K+1);  // 3K(K+1)(K+1): number of edges
const int KV = (K+1) * (K+1) * (K+1); // (K+1)^3:      number of vertices

// padding 0.5
const vec3 areaSize = vec3(16.0);
const vec3 areaOrigin = -areaSize / vec3(2.0);
const vec3 cellSize = areaSize / vec3(N);
const uint numCells = N * N * N;
const uint numBlocks = numCells / KC;
const uint numVertices = (N+1) * (N+1) * (N+1);

// Block
const vec3 blockSize = areaSize / vec3(M);

const float PI = 3.14159265f;

// Indirect commands
const uint densityCommandIndex = 0;
const uint marchingCubesCommandIndex = 1;        // div(surfaceCells, 32)
const uint surfaceCellWithBlockCommandIndex = 2; // surfaceBlocks * 2

#ifdef __cplusplus
struct PushConstants
{
    glm::mat4 viewProj{1};
    glm::vec4 color{1, 0, 0, 1};
    glm::vec4 cameraPos{0};
    glm::ivec2 resolution{1};
    float pointSize{2.0f};
    float kernelRadius{cellSize.x * 0.99f};
    float kernelScale{15.0f};
    float isoValue{0.03f};
    uint32_t maxParticleCount{0};
    uint32_t polygonMode{0};
};
#else
layout(push_constant) uniform PushConstants {
    mat4 viewProj;
    vec4 color;
    vec4 cameraPos;
    ivec2 resolution;
    float pointSize;
    float kernelRadius;
    float kernelScale;
    float isoValue;
    uint maxParticleCount;
    uint polygonMode;
} pushConstants;
#endif
