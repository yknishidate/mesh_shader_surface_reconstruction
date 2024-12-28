
// kernel
float P(float d, float h)
{
    if (d >= 0.0 && d < h) {
        float kernelNorm = 315.0 / (64.0 * PI * pow(h, 9.0));
        return max(0.0, kernelNorm * cubic(h * h - d * d));
    } else {
        return 0.0;
    }
}

float isotropicKernel(vec3 r, float h)
{
    r *= pushConstants.kernelScale;
    h *= pushConstants.kernelScale;
    float d = length(r);
    return P(d / h, h) / cubic(h);
}

float computeDensity(in uvec3 globalVertexIndices, in uint num)
{
    vec3 vertexPos = areaOrigin + cellSize * globalVertexIndices;
    float totalDensity = 0.0;

    //      -1     0
    //    -------------
    // -1 |     |     |
    //    |     |     |
    //    ------*------
    //  0 |     |     |
    //    |     |     |
    //    -------------
    int offsetSize = int(pushConstants.kernelRadius / cellSize.x);
    int offsetMin = -offsetSize - 1;
    int offsetMax = offsetSize;

    for (int x = offsetMin; x <= offsetMax; x++) {
        for (int y = offsetMin; y <= offsetMax; y++) {
            for (int z = offsetMin; z <= offsetMax; z++) {
                ivec3 neighborCellIndices = ivec3(globalVertexIndices) + ivec3(x, y, z);
                if (isOutOfRange(neighborCellIndices, num)) {
                    continue;
                }
                uint neighborCellIndex = to1D(uvec3(neighborCellIndices), num);

                // all particles
                uint particleCount = getParticleCount(neighborCellIndex);
                for (int i = 0; i < particleCount; i++) {
                    uint particleIndex = getParticleIndex(neighborCellIndex, i);
                    vec3 particlePos = getParticlePosition(particleIndex);
                    vec3 r = vertexPos - particlePos;
                    totalDensity += isotropicKernel(r, pushConstants.kernelRadius);
                }
            }
        }
    }

    return totalDensity;
}
