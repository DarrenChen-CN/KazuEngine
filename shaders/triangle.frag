#version 450

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // Simple diffuse-like shading
    vec3 lightDir = normalize(vec3(0.5, 1.0, 0.5));
    float diff = max(dot(normalize(fragNormal), lightDir), 0.2);
    vec4 texColor = texture(texSampler, fragTexCoord);
    outColor = vec4(texColor.rgb * diff, texColor.a);
}
