// ============================================================================
// KazuEngine - RHI: Transform (implementation)
// ============================================================================

#include "Transform.h"
#include <glm/gtc/matrix_transform.hpp>

namespace kazu {

Transform::Transform() = default;

void Transform::setPosition(const glm::vec3& pos) { m_position = pos; }
void Transform::setRotation(const glm::vec3& eulerDeg) { m_eulerDeg = eulerDeg; }
void Transform::setScale(const glm::vec3& scale) { m_scale = scale; }
void Transform::setScale(float uniformScale) { m_scale = glm::vec3(uniformScale); }

glm::mat4 Transform::getMatrix() const {
    glm::mat4 T = glm::translate(glm::mat4(1.0f), m_position);
    glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerDeg.x), glm::vec3(1,0,0))
                * glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerDeg.y), glm::vec3(0,1,0))
                * glm::rotate(glm::mat4(1.0f), glm::radians(m_eulerDeg.z), glm::vec3(0,0,1));
    glm::mat4 S = glm::scale(glm::mat4(1.0f), m_scale);
    return T * R * S;
}

} // namespace kazu
