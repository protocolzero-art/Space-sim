#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>

namespace aetheria {

enum class CosmicScenePreset : int {
    GargantuaKerrBH   = 0,
    GalaxyMerger      = 1,
    PillarsNebula     = 2,
    BinaryPulsarMerge = 3,
    SupernovaCollapse = 4
};

struct CosmicCameraBookmark {
    std::string name;
    CosmicScenePreset preset;
    glm::dvec3 targetPos;
    double distance;
    float yaw;
    float pitch;
    float kerrSpin;
    float nebulaDensity;
};

class CosmicDirector {
public:
    glm::dvec3 originOffset{0.0};
    glm::dvec3 targetPos{0.0};
    double distance = 22.0;
    float yaw       = 0.45f;
    float pitch     = 0.16f;
    float fovDeg    = 60.0f;

    bool transitioning      = false;
    float transitionAlpha   = 0.0f;
    float transitionDurSec  = 2.6f;
    float warpLensStrength  = 0.0f;

    CosmicCameraBookmark fromState{};
    CosmicCameraBookmark toState{};
    CosmicScenePreset activePreset = CosmicScenePreset::GargantuaKerrBH;

    std::vector<CosmicCameraBookmark> presets = {
        {"Gargantua Kerr Singularity", CosmicScenePreset::GargantuaKerrBH,   {0, 0, 0},    18.5, 0.35f,  0.14f, 0.96f, 0.35f},
        {"Andromeda-Milky Way Merger", CosmicScenePreset::GalaxyMerger,      {0, 0, 0},   115.0, 0.80f,  0.52f, 0.72f, 0.25f},
        {"Carina Ionization Nebula",   CosmicScenePreset::PillarsNebula,     {0, 2, 0},    42.0, 1.45f, -0.10f, 0.20f, 1.35f},
        {"Binary Pulsar Inspiral",     CosmicScenePreset::BinaryPulsarMerge, {0, 0, 0},    28.0, 2.10f,  0.38f, 0.88f, 0.50f},
        {"Hypernova Core Collapse",    CosmicScenePreset::SupernovaCollapse, {0, 0, 0},    36.0, 0.15f,  0.22f, 0.99f, 1.10f}
    };

    void triggerEpicTransition(int presetIndex) {
        if (presetIndex < 0 || presetIndex >= static_cast<int>(presets.size())) return;
        fromState = {"Current", activePreset, targetPos, distance, yaw, pitch, 0.9f, 0.5f};
        toState = presets[presetIndex];
        transitioning = true;
        transitionAlpha = 0.0f;
    }

    void update(float dt, float& outKerrSpin, float& outNebulaDensity) {
        if (!transitioning) {
            warpLensStrength = glm::mix(warpLensStrength, 0.0f, std::min(1.0f, dt * 6.0f));
            fovDeg = glm::mix(fovDeg, 60.0f, std::min(1.0f, dt * 6.0f));
            return;
        }

        transitionAlpha = std::min(1.0f, transitionAlpha + dt / transitionDurSec);
        float t = transitionAlpha;
        float smooth = t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);

        float warpBell = std::sin(t * glm::pi<float>());
        warpLensStrength = std::pow(warpBell, 1.6f);
        fovDeg = 60.0f + 42.0f * warpLensStrength;

        double logD0 = std::log(std::max(fromState.distance, 1.0));
        double logD1 = std::log(std::max(toState.distance, 1.0));
        double arcBoost = 2.4 * warpBell;
        distance = std::exp(glm::mix(logD0, logD1, double(smooth)) + arcBoost);

        targetPos = glm::mix(fromState.targetPos, toState.targetPos, double(smooth));
        yaw       = glm::mix(fromState.yaw, toState.yaw, smooth) + warpBell * 0.45f;
        pitch     = glm::mix(fromState.pitch, toState.pitch, smooth);

        outKerrSpin      = glm::mix(fromState.kerrSpin, toState.kerrSpin, smooth);
        outNebulaDensity = glm::mix(fromState.nebulaDensity, toState.nebulaDensity, smooth);

        if (t >= 0.5f && activePreset != toState.preset) {
            activePreset = toState.preset;
        }
        if (transitionAlpha >= 1.0f) {
            transitioning = false;
            warpLensStrength = 0.0f;
            fovDeg = 60.0f;
        }
    }

    glm::vec3 getLocalCameraPosition() const {
        double cp = std::cos(pitch);
        glm::dvec3 offset(
            distance * cp * std::sin(yaw),
            distance * std::sin(pitch),
            distance * cp * std::cos(yaw)
        );
        return glm::vec3((targetPos + offset) - originOffset);
    }

    glm::mat4 getViewProj(float aspect) const {
        glm::vec3 cam = getLocalCameraPosition();
        glm::vec3 tgt = glm::vec3(targetPos - originOffset);
        glm::mat4 view = glm::lookAt(cam, tgt, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 proj = glm::perspective(glm::radians(fovDeg), aspect, 0.1f, 50000.0f);
        proj[1][1] *= -1.0f;
        return proj * view;
    }
};

} // namespace aetheria
