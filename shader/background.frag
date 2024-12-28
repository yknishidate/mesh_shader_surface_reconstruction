#version 460

layout(location = 0) out vec4 outColor;

layout(binding = 0) uniform samplerCube envRadianceImage;

layout(push_constant) uniform BackgroundConstants {
    mat4 invView;
    mat4 invProj;
    ivec2 resolution;
} constants;

const float PI = 3.14159265;

vec3 radiance(vec3 dir)
{
    return textureLod(envRadianceImage, dir, 0).xyz;
}

vec3 gammaCorrect(vec3 color)
{
    return pow(color, vec3(1.0 / 2.2));
}

void main()
{
    vec2 uv = gl_FragCoord.xy / constants.resolution * 2.0 - 1.0;
    vec4 origin = constants.invView * vec4(0, 0, 0, 1);
    vec4 target = constants.invProj * vec4(uv.x, 1.0 - uv.y, 1, 1);
    vec4 direction = constants.invView * vec4(normalize(target.xyz), 0);
    outColor = vec4(gammaCorrect(radiance(direction.xyz)), 1.0);
}
