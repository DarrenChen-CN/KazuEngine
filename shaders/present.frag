#version 450

layout(location = 0) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D sceneColorSampler;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 hdrColor = texture(sceneColorSampler, fragTexCoord).rgb;
    outColor = vec4(clamp(hdrColor, vec3(0.0), vec3(1.0)), 1.0);
}
