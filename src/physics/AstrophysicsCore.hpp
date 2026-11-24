#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vector>
#include <cmath>
#include <cstdint>
#include <random>
#include <algorithm>

namespace aetheria {

namespace constants {
    constexpr double G                   = 6.67430e-11;
    constexpr double C                   = 299792458.0;
    constexpr double C2                  = C * C;
    constexpr double HBAR                = 1.054571817e-34;
    constexpr double K_B                 = 1.380649e-23;
    constexpr double SIGMA_SB            = 5.670374419e-8;
    constexpr double SOLAR_MASS          = 1.98847e30;
    constexpr double SOLAR_RAD           = 6.957e8;
    constexpr double PARSEC              = 3.085677581e16;
    constexpr double CHANDRASEKHAR_LIMIT = 1.44;
    constexpr double TOV_LIMIT           = 2.17;
}

enum class BodyClass : uint32_t {
    MainSequenceStar = 0,
    RedSupergiant    = 1,
    WhiteDwarf       = 2,
    NeutronPulsar    = 3,
    Magnetar         = 4,
    StellarBlackHole = 5,
    SupermassiveBH   = 6,
    RockyPlanet      = 7,
    GasGiant         = 8,
    SupernovaRemnant = 9
};

struct alignas(16) GPUBody {
    glm::vec4 posMass;
    glm::vec4 velTemp;
    glm::vec4 accRadius;
    glm::vec4 meta;
};

struct alignas(16) KerrUniforms {
    glm::mat4 invViewProj;
    glm::vec4 cameraPosTime;
    glm::vec4 bhParams;
    glm::vec4 diskVisuals;
    glm::vec4 nebulaParams;
    glm::ivec4 screenProfile;
};

class AstrophysicsSolver {
public:
    static double schwarzschildRadiusMeters(double massSolar) {
        return (2.0 * constants::G * (massSolar * constants::SOLAR_MASS)) / constants::C2;
    }

    static double kerrHorizonRadius(double spinA) {
        double a = std::clamp(spinA, 0.0, 0.998);
        return 1.0 + std::sqrt(1.0 - a * a);
    }

    static double kerrISCO(double spinA) {
        double a  = std::clamp(spinA, 0.0, 0.998);
        double z1 = 1.0 + std::cbrt(1.0 - a * a) * (std::cbrt(1.0 + a) + std::cbrt(1.0 - a));
        double z2 = std::sqrt(3.0 * a * a + z1 * z1);
        return 3.0 + z2 - std::sqrt((3.0 - z1) * (3.0 + z1 + 2.0 * z2));
    }

    static double hawkingTemperatureK(double massSolar) {
        double m = massSolar * constants::SOLAR_MASS;
        return (constants::HBAR * constants::C2 * constants::C) /
               (8.0 * glm::pi<double>() * constants::G * m * constants::K_B);
    }

    static double rocheLimit(double primaryRadius, double primaryDensity, double secondaryDensity) {
        return 2.44 * primaryRadius * std::cbrt(primaryDensity / std::max(secondaryDensity, 1e-6));
    }

    static double sedovTaylorRadius(double blastEnergyJoules, double timeSec, double ismDensity) {
        return std::pow((blastEnergyJoules * timeSec * timeSec) / std::max(ismDensity, 1e-24), 0.2);
    }

    static void computeStellarProperties(double massSolar, float& tempK, float& radiusSolar, float& luminositySolar) {
        if (massSolar < 0.43) {
            luminositySolar = static_cast<float>(0.23 * std::pow(massSolar, 2.3));
        } else if (massSolar < 2.0) {
            luminositySolar = static_cast<float>(std::pow(massSolar, 4.0));
        } else if (massSolar < 55.0) {
            luminositySolar = static_cast<float>(1.4 * std::pow(massSolar, 3.5));
        } else {
            luminositySolar = static_cast<float>(32000.0 * massSolar);
        }
        radiusSolar = static_cast<float>(std::pow(massSolar, massSolar > 1.0 ? 0.57 : 0.8));
        tempK = static_cast<float>(5778.0 * std::pow(luminositySolar / (radiusSolar * radiusSolar), 0.25));
    }

    static std::vector<GPUBody> generateCollidingGalaxies(uint32_t totalStars, bool dualCollision = true) {
        std::vector<GPUBody> bodies;
        bodies.reserve(totalStars);
        std::mt19937_64 rng(0xAE74E81AULL);
        std::uniform_real_distribution<double> uni01(0.0, 1.0);
        std::normal_distribution<double> gauss(0.0, 1.0);

        auto spawnGalaxy = [&](glm::vec3 center, glm::vec3 bulkVel, glm::vec3 axisTilt,
                               uint32_t count, float smbhMass, float diskRadius) {
            GPUBody smbh{};
            smbh.posMass   = glm::vec4(center, smbhMass);
            smbh.velTemp   = glm::vec4(bulkVel, 1.5e7f);
            smbh.accRadius = glm::vec4(0.0f, 0.0f, 0.0f, 2.2f);
            smbh.meta      = glm::vec4(static_cast<float>(BodyClass::SupermassiveBH), 12.5f, 0.94f, 1e9f);
            bodies.push_back(smbh);

            glm::vec3 up = glm::normalize(axisTilt);
            glm::vec3 right = glm::normalize(glm::cross(up, std::abs(up.y) < 0.99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0)));
            glm::vec3 forward = glm::cross(up, right);

            constexpr double G_SIM = 0.0044985;
            constexpr int NUM_ARMS = 4;

            for (uint32_t i = 1; i < count; ++i) {
                bool isBulge = (uni01(rng) < 0.22);
                double r = isBulge
                    ? std::abs(gauss(rng)) * (diskRadius * 0.14)
                    : diskRadius * (0.08 + 0.92 * std::pow(uni01(rng), 0.72));
                r = std::max(r, 0.45);

                int arm = i % NUM_ARMS;
                double armAngle = (2.0 * glm::pi<double>() * arm) / NUM_ARMS;
                double spiralTwist = 2.65 * std::log(1.0 + r * 0.42);
                double scatter = isBulge ? uni01(rng) * 6.28318 : gauss(rng) * (0.28 + 0.12 * (r / diskRadius));
                double theta = armAngle + spiralTwist + scatter;

                double heightScale = isBulge ? (diskRadius * 0.11 * std::exp(-r / (diskRadius * 0.2)))
                                             : (diskRadius * 0.022 * std::exp(-r / (diskRadius * 0.65)));
                double z = gauss(rng) * heightScale;

                glm::vec3 localPos = right * float(r * std::cos(theta)) +
                                     forward * float(r * std::sin(theta)) +
                                     up * float(z);

                double r_s = diskRadius * 0.35;
                double x = r / r_s;
                double nfwHaloMass = smbhMass * 4.5 * (std::log(1.0 + x) - x / (1.0 + x));
                double vCirc = std::sqrt(G_SIM * (smbhMass + nfwHaloMass) / r);

                glm::vec3 tangent = -right * float(std::sin(theta)) + forward * float(std::cos(theta));
                glm::vec3 vel = bulkVel + tangent * float(vCirc);

                double massStar = 0.2 + std::pow(uni01(rng), 4.2) * 32.0;
                float tempK = 5778.0f, rad = 1.0f, lum = 1.0f;
                computeStellarProperties(massStar, tempK, rad, lum);
                if (isBulge) tempK = std::min(tempK, 4800.0f);

                GPUBody star{};
                star.posMass   = glm::vec4(center + localPos, float(massStar));
                star.velTemp   = glm::vec4(vel, tempK);
                star.accRadius = glm::vec4(0.0f, 0.0f, 0.0f, std::clamp(rad * 0.08f, 0.06f, 0.65f));
                star.meta      = glm::vec4(
                    massStar > 18.0 ? float(BodyClass::RedSupergiant) : float(BodyClass::MainSequenceStar),
                    float(uni01(rng) * 10.0), 0.0f, lum
                );
                bodies.push_back(star);
            }
        };

        if (dualCollision) {
            uint32_t half = totalStars / 2;
            spawnGalaxy(glm::vec3(-42.0f,  8.0f, -15.0f), glm::vec3( 2.6f, -0.4f,  0.9f),
                        glm::vec3(0.25f, 1.0f, 0.15f), half, 4.3e6f, 34.0f);
            spawnGalaxy(glm::vec3( 44.0f, -9.0f,  18.0f), glm::vec3(-2.4f,  0.5f, -1.0f),
                        glm::vec3(-0.45f, 0.85f, 0.35f), totalStars - half, 6.2e6f, 38.0f);
        } else {
            spawnGalaxy(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(0.12f, 1.0f, 0.08f),
                        totalStars, 5.0e6f, 45.0f);
        }
        return bodies;
    }
};

} // namespace aetheria
