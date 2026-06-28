// ============================================================================
// KazuEngine - Scene Layer: Shadow Camera Helpers
// ============================================================================

#pragma once

#include "scene/Light.h"
#include "rhi/Bounds.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace kazu {

struct ShadowCamera {
    bool valid = false;
    LightType lightType = LightType::Directional;
    glm::mat4 view = glm::mat4(1.0f);
    glm::mat4 proj = glm::mat4(1.0f);
    glm::mat4 viewProj = glm::mat4(1.0f);
    glm::vec3 lightDirection = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 1.0f;
};

inline glm::vec3 stableShadowUp(const glm::vec3& dir) {
    return glm::abs(glm::dot(dir, glm::vec3(0.0f, 1.0f, 0.0f))) > 0.95f
        ? glm::vec3(0.0f, 0.0f, 1.0f)
        : glm::vec3(0.0f, 1.0f, 0.0f);
}

inline ShadowCamera buildDirectionalShadowCamera(const DirectionalLight& light,
                                                 const Bounds& bounds) {
    ShadowCamera camera{};
    camera.valid = light.castsShadow;
    camera.lightType = LightType::Directional;
    camera.lightDirection = glm::normalize(light.direction);
    camera.color = light.color;
    camera.intensity = light.intensity;

    glm::vec3 center = bounds.isValid() ? bounds.center() : glm::vec3(0.0f);
    float radius = bounds.isValid() ? bounds.radius() : 10.0f;
    radius = glm::max(radius, 1.0f);

    float lightDistance = radius * 2.0f;
    glm::vec3 eye = center - camera.lightDirection * lightDistance;
    camera.view = glm::lookAt(eye, center, stableShadowUp(camera.lightDirection));
    camera.proj = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 4.0f);
    camera.viewProj = camera.proj * camera.view;
    return camera;
}

inline ShadowCamera buildPointShadowCamera(const PointLight& light,
                                           const Bounds& bounds) {
    ShadowCamera camera{};
    camera.valid = light.castsShadow;
    camera.lightType = LightType::Point;
    camera.color = light.color;
    camera.intensity = light.intensity;
    if (!camera.valid) {
        return camera;
    }

    glm::vec3 center = bounds.isValid() ? bounds.center() : glm::vec3(0.0f);
    glm::vec3 toCenter = center - light.position;
    if (glm::length(toCenter) < 0.0001f) {
        toCenter = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    camera.lightDirection = glm::normalize(toCenter);

    float sceneRadius = bounds.isValid() ? bounds.radius() : 10.0f;
    float lightDist = glm::length(light.position - center);
    float zNear = glm::max(0.01f, 0.1f * lightDist);
    float zFar = glm::max(lightDist + sceneRadius, 2.0f * lightDist);
    camera.view = glm::lookAt(light.position, center, stableShadowUp(camera.lightDirection));
    camera.proj = glm::perspective(glm::radians(90.0f), 1.0f, zNear, zFar);
    camera.viewProj = camera.proj * camera.view;
    return camera;
}

inline ShadowCamera buildAreaShadowCamera(const AreaLight& light,
                                          const Bounds& bounds) {
    ShadowCamera camera{};
    camera.valid = light.castsShadow;
    camera.lightType = LightType::Area;
    camera.color = light.color;
    camera.intensity = light.intensity;
    if (!camera.valid) {
        return camera;
    }

    glm::vec3 center = bounds.isValid() ? bounds.center() : glm::vec3(0.0f);
    glm::vec3 toCenter = center - light.position;
    if (glm::length(toCenter) < 0.0001f) {
        toCenter = light.direction;
    }
    if (glm::length(toCenter) < 0.0001f) {
        toCenter = glm::vec3(0.0f, -1.0f, 0.0f);
    }
    camera.lightDirection = glm::normalize(toCenter);

    float sceneRadius = bounds.isValid() ? bounds.radius() : 10.0f;
    float lightDist = glm::length(light.position - center);
    float zNear = glm::max(0.01f, 0.1f * lightDist);
    float zFar = glm::max(lightDist + sceneRadius, 2.0f * lightDist);
    camera.view = glm::lookAt(light.position, center, stableShadowUp(camera.lightDirection));
    camera.proj = glm::perspective(glm::radians(90.0f), 1.0f, zNear, zFar);
    camera.viewProj = camera.proj * camera.view;
    return camera;
}

inline ShadowCamera selectShadowCamera(const DirectionalLight& directional,
                                       const std::vector<PointLight>& pointLights,
                                       const std::vector<AreaLight>& areaLights,
                                       const Bounds& bounds) {
    if (directional.castsShadow) {
        return buildDirectionalShadowCamera(directional, bounds);
    }

    for (const auto& point : pointLights) {
        if (point.castsShadow) {
            return buildPointShadowCamera(point, bounds);
        }
    }

    for (const auto& area : areaLights) {
        if (area.castsShadow) {
            return buildAreaShadowCamera(area, bounds);
        }
    }

    return ShadowCamera{};
}

} // namespace kazu
