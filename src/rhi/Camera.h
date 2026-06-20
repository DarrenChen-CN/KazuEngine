// ============================================================================
// KazuEngine - RHI: Camera
//
// Orbit camera: position orbits around a target point.
// Supports mouse-drag orbit and scroll-wheel zoom.
// ============================================================================

#pragma once

#include <glm/glm.hpp>

namespace kazu {

class Camera {
public:
    Camera();

    void setPosition(const glm::vec3& pos);
    void setTarget(const glm::vec3& target);
    void setUp(const glm::vec3& up);
    void setFov(float fovDeg);
    void setJitter(const glm::vec2& jitter) { m_jitter = jitter; }

    // Orbit around target: deltaYaw/deltaPitch in radians
    void orbit(float deltaYaw, float deltaPitch);

    // Pan: move camera and target along view plane
    // dx = rightward movement, dy = downward movement (screen space)
    void pan(float dx, float dy);

    // Zoom: positive = move closer, negative = move away
    void zoom(float delta);

    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspect) const;

    glm::mat4 getJitteredProjectionMatrix(float aspect) const;

    const glm::vec3& position() const { return m_position; }
    const glm::vec3& target() const { return m_target; }

private:
    glm::vec3 m_position{0.0f, 1.4f, 4.5f};
    glm::vec3 m_target{0.0f, 0.9f, 0.0f};
    glm::vec3 m_up{0.0f, 1.0f, 0.0f};
    float m_fovDeg = 45.0f;
    float m_near = 0.1f;
    float m_far = 100.0f;
    glm::vec2 m_jitter{0.0f};
};

} // namespace kazu
