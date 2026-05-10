#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    vec4 lightPos;
    vec4 viewPos;
    int displayMode;
} pc;

void main() {
    // Depth visualization mode: press D to toggle
    if (pc.displayMode == 1) {
        float depth = gl_FragCoord.z;
        // Perspective depth is non-linear (most values near 1.0).
        // Use pow() to stretch the visible range for debugging.
        float visualDepth = pow(depth, 20.0);
        outColor = vec4(vec3(visualDepth), 1.0);
        return;
    }

    vec3 albedo = texture(texSampler, fragTexCoord).rgb;

    vec3 N = normalize(fragNormal);
    vec3 L = normalize(pc.lightPos.xyz - fragWorldPos);
    vec3 V = normalize(pc.viewPos.xyz - fragWorldPos);
    vec3 H = normalize(L + V);

    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);

    vec3 ambient = 0.1 * albedo;
    vec3 diffuse = diff * albedo;
    vec3 specular = spec * vec3(0.3);

    outColor = vec4(ambient + diffuse + specular, 1.0);
}
