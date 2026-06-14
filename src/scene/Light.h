// ============================================================================
// KazuEngine - Scene Layer: Light
// ============================================================================

#pragma once

#include <glm/glm.hpp>

namespace kazu {

struct DirectionalLight {
    // Direction the light travels in world space.
    glm::vec3 direction = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
};

} // namespace kazu
