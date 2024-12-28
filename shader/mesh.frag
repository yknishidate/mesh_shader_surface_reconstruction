#version 460
#include "shared.glsl"

// in
layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec4 inPos;

// out
layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outColor2;
layout(location = 2) out vec4 outPos;

vec3 irradiance(vec3 dir)
{
    return textureLod(envIrradianceImage, dir, 0).xyz;
}

void main() {
    vec3 dir = normalize(inPos.xyz - pushConstants.cameraPos.xyz);
    vec3 normal = normalize(inNormal);
    normal.x = -normal.x;
    normal.z = -normal.z;

    vec3 color = inColor.xyz * irradiance(normal);
    color = gammaCorrect(color);
    outColor = vec4(color, 1.0);
    outColor2 = vec4(color, 1.0);
    outPos = inPos;
}
