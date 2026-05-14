#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

// MRT outputs: Albedo / Normal / Material
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;

void main() {
    // Albedo: sample diffuse texture
    outAlbedo = texture(texSampler, fragTexCoord);

    // Normal: pack [-1,1] to [0,1] for RGBA8 storage
    outNormal = vec4(normalize(fragNormal) * 0.5 + 0.5, 1.0);

    // Material: Metallic (R), Roughness (G), AO (B)
    // Current OBJ scene has no metallic/roughness map, use defaults
    outMaterial = vec4(0.0, 0.5, 1.0, 1.0);
}
