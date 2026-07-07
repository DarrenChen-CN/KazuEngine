#version 450

layout(location = 0) in vec3 fragWorldPos;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform sampler2D albedoSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;
layout(set = 0, binding = 2) uniform sampler2D metallicRoughnessSampler;
layout(set = 0, binding = 3) uniform sampler2D aoSampler;

layout(push_constant) uniform PushConstants {
    mat4 mvp;
    mat4 model;
    mat4 normalMatrix;
    vec4 baseColorFactor;
    vec4 materialParams; // x = metallic, y = roughness, z = ao
} pc;

// MRT outputs: Albedo / Normal / Material
layout(location = 0) out vec4 outAlbedo;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec4 outMaterial;

mat3 cotangentFrame(vec3 normal, vec3 position, vec2 uv) {
    vec3 dp1 = dFdx(position);
    vec3 dp2 = dFdy(position);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    vec3 dp2perp = cross(dp2, normal);
    vec3 dp1perp = cross(normal, dp1);
    vec3 tangent = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 bitangent = dp2perp * duv1.y + dp1perp * duv2.y;

    float invMax = inversesqrt(max(dot(tangent, tangent), dot(bitangent, bitangent)));
    return mat3(tangent * invMax, bitangent * invMax, normal);
}

void main() {
    vec2 uv = fragTexCoord;
    int textureFlags = int(pc.materialParams.w + 0.5);
    bool hasNormalMap = (textureFlags & 1) != 0;
    bool hasMetallicRoughnessMap = (textureFlags & 2) != 0;
    bool hasAoMap = (textureFlags & 4) != 0;
    bool flipV = (textureFlags & 8) != 0;
    if (flipV) {
        uv.y = 1.0 - uv.y;
    }

    // Albedo: sample diffuse texture and apply material factor.
    outAlbedo = texture(albedoSampler, uv) * pc.baseColorFactor;

    vec3 normal = normalize(fragNormal);
    if (hasNormalMap) {
        vec3 tangentNormal = texture(normalSampler, uv).xyz * 2.0 - 1.0;
        normal = normalize(cotangentFrame(normal, fragWorldPos, uv) * tangentNormal);
    }
    outNormal = vec4(normal * 0.5 + 0.5, 1.0);

    // Material: Metallic (R), Roughness (G), AO (B)
    float metallic = clamp(pc.materialParams.x, 0.0, 1.0);
    float roughness = clamp(pc.materialParams.y, 0.04, 1.0);
    float ao = clamp(pc.materialParams.z, 0.0, 1.0);
    if (hasMetallicRoughnessMap) {
        vec3 mr = texture(metallicRoughnessSampler, uv).rgb;
        roughness = clamp(mr.g, 0.04, 1.0);
        metallic = clamp(mr.b, 0.0, 1.0);
    }
    if (hasAoMap) {
        ao = clamp(texture(aoSampler, uv).r, 0.0, 1.0);
    }
    outMaterial = vec4(
        metallic,
        roughness,
        ao,
        1.0);
}
