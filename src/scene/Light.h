// ============================================================================
// KazuEngine - Scene Layer: Light
// ============================================================================

#pragma once

#include <glm/glm.hpp>

namespace kazu {

enum class LightType {
    Directional,
    Point
};

struct Light {
    virtual ~Light() = default;

    virtual LightType type() const = 0;

    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
    bool castsShadow = false;
    bool visualize = false;
};

struct DirectionalLight final : public Light {
    LightType type() const override { return LightType::Directional; }

    // Direction the light travels in world space.
    glm::vec3 direction = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
};

struct PointLight final : public Light {
    LightType type() const override { return LightType::Point; }

    glm::vec3 position = glm::vec3(0.0f);
    float range = 10.0f;
};

} // namespace kazu
