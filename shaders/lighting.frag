#version 450

precision highp float;
precision highp int;

layout(location = 0) in vec2 fragTexCoord;
layout(location = 1) in vec2 fragNdc;

layout(set = 0, binding = 0) uniform sampler2D albedoSampler;
layout(set = 0, binding = 1) uniform sampler2D normalSampler;
layout(set = 0, binding = 2) uniform sampler2D depthSampler;
layout(set = 0, binding = 3) uniform sampler2D shadowMapSampler;

layout(push_constant) uniform PushConstants {
    mat4 invViewProj;
    mat4 lightViewProj;
    vec4 lightDirection;
    vec4 lightColorIntensity;
    vec4 viewPos;
    float shadowBias;
    float pcfFilterSize;
    float lightWidth;
    int pcfSampleCount;
    int shadowMode;
    int debugView;
    int lightingModel;
} pc;

layout(location = 0) out vec4 outColor;

#define MAX_PCF_SAMPLES 64
#define BLOCKER_SEARCH_NUM_SAMPLES MAX_PCF_SAMPLES
#define NUM_RINGS 10
#define PI 3.141592653589793
#define PI2 6.283185307179586
#define EPS 1e-3

vec3 reconstructWorldPos(vec2 ndc, float depth) {
    vec4 clip = vec4(ndc, depth, 1.0);
    vec4 world = pc.invViewProj * clip;
    return world.xyz / world.w;
}

highp float rand_1to1(highp float x) {
    return fract(sin(x) * 10000.0);
}

highp float rand_2to1(vec2 uv) {
    const highp float a = 12.9898, b = 78.233, c = 43758.5453;
    highp float dt = dot(uv.xy, vec2(a, b));
    highp float sn = mod(dt, PI);
    return fract(sin(sn) * c);
}

void poissonDiskSamples(const in vec2 randomSeed, out vec2 poissonDisk[MAX_PCF_SAMPLES]) {
    float angleStep = PI2 * float(NUM_RINGS) / float(MAX_PCF_SAMPLES);
    float invNumSamples = 1.0 / float(MAX_PCF_SAMPLES);
    float angle = rand_2to1(randomSeed) * PI2;
    float radius = invNumSamples;
    float radiusStep = radius;
    for (int i = 0; i < MAX_PCF_SAMPLES; ++i) {
        poissonDisk[i] = vec2(cos(angle), sin(angle)) * pow(radius, 0.75);
        radius += radiusStep;
        angle += angleStep;
    }
}

float PCF(sampler2D shadowMap, vec2 uv, float currentDepth, float bias, float filterSize, int sampleCount) {
    vec2 poissonDisk[MAX_PCF_SAMPLES];
    poissonDiskSamples(uv, poissonDisk);

    float shadow = 0.0;
    for (int i = 0; i < MAX_PCF_SAMPLES; ++i) {
        if (i >= sampleCount) break;
        vec2 offset = poissonDisk[i] * filterSize;
        float closestDepth = texture(shadowMap, uv + offset).r;
        if (currentDepth - bias > closestDepth) shadow += 1.0;
    }
    return shadow / float(sampleCount);
}

float findBlocker(sampler2D shadowMap, vec2 uv, float zReceiver) {
    float filterSize = 0.1;
    vec2 poissonDisk[MAX_PCF_SAMPLES];
    poissonDiskSamples(uv, poissonDisk);

    float blockerDepth = 0.0;
    float blockerNum = 0.0;
    for (int i = 0; i < BLOCKER_SEARCH_NUM_SAMPLES; ++i) {
        vec2 offset = poissonDisk[i] * filterSize;
        float depth = texture(shadowMap, uv + offset).r;
        if (depth < zReceiver - EPS) {
            blockerDepth += depth;
            blockerNum += 1.0;
        }
    }
    if (blockerNum == 0.0) return 1.0;
    return blockerDepth / blockerNum;
}

float PCSS(sampler2D shadowMap, vec2 uv, float currentDepth, float bias, int sampleCount) {
    float blockerDepth = findBlocker(shadowMap, uv, currentDepth);
    float wPenumbra = (currentDepth - blockerDepth) * pc.lightWidth / blockerDepth;
    return PCF(shadowMap, uv, currentDepth, bias, wPenumbra, sampleCount);
}

float computeShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (pc.shadowMode == 0) {
        return 0.0;
    }

    vec4 lightClip = pc.lightViewProj * vec4(worldPos, 1.0);
    vec3 lightNDC = lightClip.xyz / lightClip.w;

    // NDC.xy [-1,1] -> UV [0,1]
    vec2 shadowUV = lightNDC.xy * 0.5 + 0.5;

    // Outside shadow map frustum: no shadow
    if (shadowUV.x < 0.0 || shadowUV.x > 1.0 ||
        shadowUV.y < 0.0 || shadowUV.y > 1.0 ||
        lightNDC.z < 0.0 || lightNDC.z > 1.0) {
        return 0.0;
    }

    // Slope-scale bias: larger bias for surfaces grazing the light direction
    float bias = max(pc.shadowBias * (1.0 - dot(normal, lightDir)), pc.shadowBias * 0.2);
    float currentDepth = lightNDC.z;

    int sampleCount = clamp(pc.pcfSampleCount, 1, MAX_PCF_SAMPLES);
    if (pc.shadowMode == 1 || sampleCount <= 1) {
        float closestDepth = texture(shadowMapSampler, shadowUV).r;
        return (currentDepth - bias > closestDepth) ? 1.0 : 0.0;
    }

    if (pc.shadowMode == 3) {
        return PCSS(shadowMapSampler, shadowUV, currentDepth, bias, sampleCount);
    }
    return PCF(shadowMapSampler, shadowUV, currentDepth, bias, pc.pcfFilterSize, sampleCount);
}

void main() {
    vec3 albedo = texture(albedoSampler, fragTexCoord).rgb;
    vec3 normalEncoded = texture(normalSampler, fragTexCoord).rgb;
    vec3 normal = normalize(normalEncoded * 2.0 - 1.0);
    float depth = texture(depthSampler, fragTexCoord).r;

    if (pc.debugView == 1) {
        outColor = vec4(albedo, 1.0);
        return;
    }
    if (pc.debugView == 2) {
        outColor = vec4(normalEncoded, 1.0);
        return;
    }
    if (pc.debugView == 3) {
        // Debug: visualize shadow map
        vec2 uv = vec2(fragTexCoord.x, 1.0 - fragTexCoord.y);
        float d = texture(shadowMapSampler, uv).r;
        outColor = vec4(vec3(d), 1.0);
        return;
    }

    vec3 worldPos = reconstructWorldPos(fragNdc, depth);
    vec3 lightDir = normalize(-pc.lightDirection.xyz);
    float diff = max(dot(normal, lightDir), 0.0);

    float shadow = computeShadow(worldPos, normal, lightDir);

    vec3 ambient = albedo * 0.1;
    vec3 diffuse = albedo * pc.lightColorIntensity.rgb * pc.lightColorIntensity.a * diff * (1.0 - shadow);
    outColor = vec4(ambient + diffuse, 1.0);
}
