#version 460
#include "shared.glsl"

// in
layout(location = 0) in vec4 inColor;
layout(location = 1) in vec3 inNormal;

// out
layout(location = 0) out vec4 outColor;

void main() {
    if(inNormal == vec3(0)){
        outColor = inColor;
    }else{
        float lighting = computeLighting(-inNormal);
        outColor = inColor * lighting;
    }
}
