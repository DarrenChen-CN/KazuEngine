// ============================================================================
// KazuEngine - RHI: Transform
//
// TRS (Translate-Rotate-Scale) transform for scene objects.
// Computes model matrix on demand.
// ============================================================================

#pragma once

#include <glm/glm.hpp>

namespace kazu {

class Transform {
public:
    Transform();

    void setPosition(const glm::vec3& pos);
    void setRotation(const glm::vec3& eulerDeg); // XYZ Euler angles in degrees
    void setScale(const glm::vec3& scale);
    void setScale(float uniformScale);

    // Returns T * R * S matrix
    glm::mat4 getMatrix() const;

    const glm::vec3& position() const { return m_position; }

private:
    glm::vec3 m_position{0.0f};
    glm::vec3 m_eulerDeg{0.0f};
    glm::vec3 m_scale{1.0f};
};

} // namespace kazu
