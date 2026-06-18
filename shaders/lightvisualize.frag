#version 450

layout(location = 0) in vec4 fragColor;

layout(location = 0) out vec4 outColor;

void main() {
    // Boost brightness so the light source clearly stands out.
    outColor = fragColor * 2.0;
}
