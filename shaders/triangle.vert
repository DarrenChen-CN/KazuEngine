#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragWorldPos;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out vec2 fragTexCoord;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialParams;
} pc;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragWorldPos = (pc.model * vec4(inPosition, 1.0)).xyz;
    fragNormal = normalize((pc.normalMatrix * vec4(inNormal, 0.0)).xyz);
    fragTexCoord = inTexCoord;
}
