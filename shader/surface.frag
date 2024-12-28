#version 460
#include "shared.glsl"

vec3 color = vec3(0.7, 0.9, 1);

vec3 getMeshletColor(uint index)
{
    float r = fract(sin(float(index) * 12.9898) * 43758.5453);
    float g = fract(sin(float(index) * 78.233) * 43758.5453);
    float b = fract(sin(float(index) * 43.853) * 43758.5453);
    return vec3(r, g, b);
}

vec3 radiance(vec3 dir)
{
    return textureLod(envRadianceImage, dir, 1).xyz;
}

float fresnelSchlick(float VdotH, float F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

bool intersectAABB(in vec3 origin, in vec3 direction, 
                   in vec3 aabbMin, in vec3 aabbMax,
                   out float tMin, out float tMax)
{
    vec3 t1 = (aabbMin - origin) / direction;
    vec3 t2 = (aabbMax - origin) / direction;
    vec3 min1 = min(t1, t2);
    vec3 max1 = max(t1, t2);
    tMin = max(max(min1.x, min1.y), min1.z);
    tMax = min(min(max1.x, max1.y), max1.z);
    return 0 <= tMax && tMin <= tMax;
}

float remapFrom01(float value, float minValue, float maxValue) {
    return minValue + value * (maxValue - minValue);
}

float remapTo01(float value, float minValue, float maxValue) {
    return (value - minValue) / (maxValue - minValue);
}

#extension GL_EXT_mesh_shader : require
layout (location = 0) in VertexInput {
    vec4 normal;
    vec4 pos;
#ifdef OUTPUT_MESHLET_INDEX
    flat uint meshletIndex;
#endif
} vertexInput;

layout(location = 0) out vec4 outColor;

void main_mesh_shader() {
    vec3 normal = normalize(vertexInput.normal.xyz);
    
#ifdef OUTPUT_MESHLET_INDEX
    outColor = vec4(getMeshletColor(vertexInput.meshletIndex) * (computeLighting(normal)), 1);
    return;
#endif

    // refract
    vec3 pos = vertexInput.pos.xyz;
    vec3 dir = normalize(pos - pushConstants.cameraPos.xyz);
    
    vec4 ndcPos = worldToNDC(pos);
    vec2 uv = (ndcPos.xy / ndcPos.w) * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;
    vec3 meshPos = texture(posImage, uv).xyz;

    float ior = 1.33;
    float eta = 1.0 / ior;

    vec3 reflectDir = reflect(dir, normal);
    vec3 h = normalize(-dir + reflectDir);
    vec3 reflection = radiance(reflectDir);
    float f0 = ((1.0 - ior) / (1.0 + ior)) * ((1.0 - ior) / (1.0 + ior));
    float fr = fresnelSchlick(dot(-dir, h), f0);

    vec3 refractDir = refract(dir, normal, eta);
    vec3 refraction;

    if(meshPos == vec3(0.0)) {
        float tMin, tMax;
        if(intersectAABB(pos, refractDir, 
                         areaOrigin, areaOrigin + areaSize,
                         tMin, tMax)){
            float dist = tMax;
            vec3 trans = clamp(exp(-remapTo01(dist, 1.0, 5.0)), 0.0, 1.0) * color;
            refraction = trans * radiance(refractDir);
        }
    } else {
        float dist = distance(pos, meshPos);
        vec3 trans = clamp(exp(-remapTo01(dist, 1.0, 5.0)), 0.0, 1.0) * color;
        refraction = trans * texture(colorImage, uv).xyz;
    }

    vec3 color = refraction * (1.0 - fr) + reflection * fr;
    outColor = vec4(gammaCorrect(color), 1.0);
}
