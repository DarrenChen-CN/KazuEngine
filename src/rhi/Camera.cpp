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

    // Clamp pitch before applying it, so we never cross the poles.
    const float maxPitch = glm::radians(85.0f);
    float currentPitch = glm::asin(glm::clamp(glm::dot(glm::normalize(offset), m_up), -1.0f, 1.0f));
    float newPitch = glm::clamp(currentPitch + deltaPitch, -maxPitch, maxPitch);
    float clampedDeltaPitch = newPitch - currentPitch;

    // Pitch: rotate offset around right axis.
    // If offset is nearly parallel to world-up, pick a fallback right axis
    // to avoid a degenerate cross product (this is not gimbal lock, but a
    // singularity in the orbit basis construction).
    glm::vec3 right = glm::cross(offset, m_up);
    float rightLen = glm::length(right);
    if (rightLen < 0.0001f) {
        right = glm::cross(glm::vec3(1.0f, 0.0f, 0.0f), m_up);
        rightLen = glm::length(right);
        if (rightLen < 0.0001f) {
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        } else {
            right /= rightLen;
        }
    } else {
        right /= rightLen;
    }

    glm::mat4 pitch = glm::rotate(glm::mat4(1.0f), clampedDeltaPitch, right);
    offset = glm::vec3(pitch * glm::vec4(offset, 1.0f));

    m_position = m_target + offset;
}

void Camera::pan(float dx, float dy) {
    glm::vec3 right = glm::normalize(glm::cross(m_target - m_position, m_up));
    glm::vec3 up = glm::normalize(m_up);

    // Grab behavior: dragging the view, not the camera.
    // Mouse right drag -> view moves right -> camera moves left.
    // Mouse down drag  -> view moves down  -> camera moves up.
    glm::vec3 delta = -right * dx + up * dy;
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
