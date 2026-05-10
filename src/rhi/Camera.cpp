// ============================================================================
// KazuEngine - RHI: Camera (implementation)
// ============================================================================

#include "Camera.h"
#include <glm/gtc/matrix_transform.hpp>

namespace kazu {

Camera::Camera() = default;

void Camera::setPosition(const glm::vec3& pos) { m_position = pos; }
void Camera::setTarget(const glm::vec3& target) { m_target = target; }
void Camera::setUp(const glm::vec3& up) { m_up = up; }
void Camera::setFov(float fovDeg) { m_fovDeg = fovDeg; }

void Camera::orbit(float deltaYaw, float deltaPitch) {
    glm::vec3 offset = m_position - m_target;

    // Yaw: rotate offset around world-up (m_up)
    glm::mat4 yaw = glm::rotate(glm::mat4(1.0f), deltaYaw, m_up);
    offset = glm::vec3(yaw * glm::vec4(offset, 1.0f));

    // Pitch: rotate offset around right axis
    glm::vec3 right = glm::normalize(glm::cross(offset, m_up));
    glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), deltaPitch, right);
    offset = glm::vec3(pitch * glm::vec4(offset, 1.0f));

    m_position = m_target + offset;
}

void Camera::pan(float dx, float dy) {
    glm::vec3 right = glm::normalize(glm::cross(m_target - m_position, m_up));
    glm::vec3 up = glm::normalize(m_up);

    // dx > 0: mouse dragged right -> camera moves right
    // dy > 0: mouse dragged down (GLFW Y+) -> camera moves down (world Y-)
    glm::vec3 delta = right * dx - up * dy;
    m_position += delta;
    m_target += delta;
}

void Camera::zoom(float delta) {
    glm::vec3 dir = m_position - m_target;
    float dist = glm::length(dir);
    // Clamp to avoid flipping through target or going behind far plane
    dist = glm::clamp(dist - delta, m_near + 0.01f, m_far - 1.0f);
    m_position = m_target + glm::normalize(dir) * dist;
}

glm::mat4 Camera::getViewMatrix() const {
    return glm::lookAt(m_position, m_target, m_up);
}

glm::mat4 Camera::getProjectionMatrix(float aspect) const {
    glm::mat4 proj = glm::perspective(glm::radians(m_fovDeg), aspect, m_near, m_far);
    proj[1][1] *= -1.0f; // Vulkan Y-flip
    return proj;
}

} // namespace kazu
