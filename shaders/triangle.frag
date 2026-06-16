#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

void main() {
    // Vulkan texture origin is top-left; OBJ UV is bottom-left (OpenGL style).
    // Flip V to match.
    vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
    outColor = texture(texSampler, uv);
}
