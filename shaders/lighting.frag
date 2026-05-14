#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D albedoSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;

layout(push_constant) uniform PushConstants {
    vec4 lightPos;
    vec4 viewPos;
    int displayMode;
} pc;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 albedo = texture(albedoSampler, fragTexCoord).rgb;
    vec3 normalEncoded = texture(normalSampler, fragTexCoord).rgb;
    vec3 normal = normalize(normalEncoded * 2.0 - 1.0);

    if (pc.displayMode == 1) {
        outColor = vec4(albedo, 1.0);
        return;
    }
    if (pc.displayMode == 2) {
        outColor = vec4(normalEncoded, 1.0);
        return;
    }

    vec3 lightDir = normalize(pc.lightPos.xyz);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 ambient = albedo * 0.1;
    vec3 diffuse = albedo * diff;
    outColor = vec4(ambient + diffuse, 1.0);
}
