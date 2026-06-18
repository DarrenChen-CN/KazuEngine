// ============================================================================
// KazuEngine - RHI Layer: Bounds
//
// Axis-aligned bounding box (AABB) helper used for scene and mesh bounds.
// ============================================================================

#pragma once

#include <glm/glm.hpp>
#include <limits>

namespace kazu {

struct Bounds {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 extent() const { return max - min; }
    float radius() const { return glm::length(extent()) * 0.5f; }

    void expand(const glm::vec3& p) {
        min = glm::min(min, p);
        max = glm::max(max, p);
    }

    void expand(const Bounds& other) {
        if (!other.isValid()) return;
        expand(other.min);
        expand(other.max);
    }
};

} // namespace kazu
