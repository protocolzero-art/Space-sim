#pragma once
#include "imgui.h"
#include "../physics/AstrophysicsCore.hpp"
#include "../physics/CosmicDirector.hpp"
#include <functional>

namespace aetheria {

struct SimulationParams {
    float timeScale          = 1.0f;
    bool  paused             = false;
    float bhMassSolar        = 4.3e6f;
    float kerrSpin           = 0.96f;
    float accretionRate      = 1.25f;
    float dopplerStrength    = 1.0f;
    float jetPower           = 0.85f;
    float darkMatterHaloMass = 2.2e7f;
    float nebulaDensity      = 0.35f;
    float nebulaTurbulence   = 0.85f;
    bool  mobilePerfMode     = false;
    int   activeBodyCount    = 16384;
};

class SpaceGUI {
public:
    static void applySciFiTheme(bool isAndroid) {
        ImGuiStyle& s = ImGui::GetStyle();
        s.WindowRounding    = 10.0f;
        s.FrameRounding     = 6.0f;
        s.GrabRounding      = 6.0f;
        s.WindowBorderSize  = 1.0f;
        if (isAndroid) {
            s.ScaleAllSizes(2.1f);
            ImGui::GetIO().FontGlobalScale = 2.0f;
        }
        auto& c = s.Colors;
        c[ImGuiCol_WindowBg]      = ImVec4(0.03f, 0.05f, 0.09f, 0.84f);
        c[ImGuiCol_Border]        = ImVec4(0.18f, 0.56f, 0.92f, 0.55f);
        c[ImGuiCol_Header]        = ImVec4(0.12f, 0.35f, 0.65f, 0.75f);
        c[ImGuiCol_SliderGrab]    = ImVec4(0.22f, 0.78f, 1.00f, 1.00f);
        c[ImGuiCol_Button]        = ImVec4(0.10f, 0.28f, 0.52f, 0.85f);
        c[ImGuiCol_ButtonHovered] = ImVec4(0.18f, 0.52f, 0.88f, 1.00f);
        c[ImGuiCol_Text]          = ImVec4(0.90f, 0.96f, 1.00f, 1.00f);
    }

    static void renderHUD(
        SimulationParams& p,
        CosmicDirector& director,
        float fps,
        float gpuMs,
        const std::function<void(CosmicScenePreset)>& onResetScene,
        const std::function<void()>& onSpawnRogueBH,
        const std::function<void()>& onTriggerSupernova
    ) {
        ImGui::SetNextWindowPos(ImVec2(14, 14), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(370, 560), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("AETHERIA-GR // Relativistic Cosmos")) {
            ImGui::TextColored(ImVec4(0.25f, 0.85f, 1.0f, 1.0f),
                "GPU: %.2f ms | %.0f FPS | Bodies: %d", gpuMs, fps, p.activeBodyCount);
            ImGui::Separator();

            if (ImGui::CollapsingHeader("Cosmic Warp Jumps", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (int i = 0; i < static_cast<int>(director.presets.size()); ++i) {
                    if (ImGui::Button(director.presets[i].name.c_str(), ImVec2(-1, 0))) {
                        director.triggerEpicTransition(i);
                        onResetScene(director.presets[i].preset);
                    }
                }
            }

            if (ImGui::CollapsingHeader("Kerr Black Hole Metric", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderFloat("Spin Parameter (a*)", &p.kerrSpin, 0.0f, 0.998f, "%.3f");
                ImGui::SliderFloat("Accretion Rate", &p.accretionRate, 0.1f, 3.0f);
                ImGui::SliderFloat("Doppler Beaming", &p.dopplerStrength, 0.0f, 1.5f);
                ImGui::SliderFloat("Polar Jet Power", &p.jetPower, 0.0f, 2.0f);

                double rS   = AstrophysicsSolver::schwarzschildRadiusMeters(p.bhMassSolar) / 1000.0;
                double isco = AstrophysicsSolver::kerrISCO(p.kerrSpin);
                double tH   = AstrophysicsSolver::hawkingTemperatureK(p.bhMassSolar);
                ImGui::Text("Event Horizon r_s: %.2e km", rS);
                ImGui::Text("Prograde ISCO:     %.3f r_g", isco);
                ImGui::Text("Hawking Temp:      %.2e K", tH);
            }

            if (ImGui::CollapsingHeader("Galactic Dynamics & Nebulae")) {
                int requestedBodies = p.activeBodyCount;
                const int maxBodies = p.mobilePerfMode ? 16384 : 65536;
                if (ImGui::SliderInt("N-body count (reload scene)", &requestedBodies, 256, maxBodies)) {
                    p.activeBodyCount = requestedBodies;
                    onResetScene(director.activePreset);
                }
                ImGui::TextWrapped("Direct gravity scales O(N^2). Start low on mobile and raise the count gradually.");
                ImGui::SliderFloat("Time Dilation (dt)", &p.timeScale, 0.0f, 5.0f);
                ImGui::SliderFloat("NFW Dark Matter", &p.darkMatterHaloMass, 0.0f, 8.0e7f, "%.2e");
                ImGui::SliderFloat("Nebula Density", &p.nebulaDensity, 0.0f, 2.0f);
                ImGui::SliderFloat("MRI Turbulence", &p.nebulaTurbulence, 0.1f, 2.0f);
                ImGui::Checkbox("Pause Simulation", &p.paused);
                ImGui::Checkbox("Mali-G615 Mobile Profile (60 FPS)", &p.mobilePerfMode);
            }

            ImGui::Separator();
            if (ImGui::Button("Spawn Rogue Black Hole", ImVec2(-1, 0))) onSpawnRogueBH();
            if (ImGui::Button("Trigger Core-Collapse Supernova", ImVec2(-1, 0))) onTriggerSupernova();
        }
        ImGui::End();
    }
};

} // namespace aetheria
