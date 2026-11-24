#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "physics/AstrophysicsCore.hpp"
#include "physics/CosmicDirector.hpp"
#include "engine/VulkanEngine.hpp"
#include "ui/SpaceGUI.hpp"
#include <unordered_map>

struct AppState {
    SDL_Window* window = nullptr;
    aetheria::VulkanEngine engine;
    aetheria::CosmicDirector director;
    aetheria::SimulationParams params;
    uint64_t lastCounter = 0;
    float smoothedFps = 60.0f;
    bool dragging = false;
    bool isAndroid = false;

    std::unordered_map<SDL_FingerID, glm::vec2> activeFingers;
    float lastPinchDist = 0.0f;
};

static void loadPresetBodies(AppState* app, aetheria::CosmicScenePreset preset) {
#if AETHERIA_STAGE >= 2
    bool dual = (preset == aetheria::CosmicScenePreset::GalaxyMerger ||
                 preset == aetheria::CosmicScenePreset::BinaryPulsarMerge);
    auto bodies = aetheria::AstrophysicsSolver::generateCollidingGalaxies(
        static_cast<uint32_t>(app->params.activeBodyCount), dual);
    app->engine.uploadBodies(bodies);
#else
    (void)app; (void)preset;
#endif
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    (void)argc; (void)argv;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) return SDL_APP_FAILURE;

    auto* app = new AppState();
    *appstate = app;
#ifdef __ANDROID__
    app->isAndroid = true;
    app->params.mobilePerfMode  = true;
    app->params.activeBodyCount = 1024; // Start mobile Stage 2 at a practical O(N^2) workload.
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_FULLSCREEN;
    int w = 0, h = 0;
#else
    app->isAndroid = false;
    app->params.mobilePerfMode  = false;
    app->params.activeBodyCount = 8192; // Increase from the HUD after profiling desktop GPU time.
    Uint32 flags = SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;
    int w = 1600, h = 900;
#endif

    app->window = SDL_CreateWindow("AETHERIA-GR // Vulkan Relativistic Astrophysics", w, h, flags);
    if (!app->window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        delete app;
        *appstate = nullptr;
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    try {
        app->engine.init(app->window, app->isAndroid);
        aetheria::SpaceGUI::applySciFiTheme(app->isAndroid);
        loadPresetBodies(app, aetheria::CosmicScenePreset::GargantuaKerrBH);
    } catch (const std::exception& e) {
        SDL_Log("AETHERIA-GR Init Failed: %s", e.what());
        app->engine.cleanup();
        if (app->window) SDL_DestroyWindow(app->window);
        delete app;
        *appstate = nullptr;
        SDL_Quit();
        return SDL_APP_FAILURE;
    }

    app->lastCounter = SDL_GetPerformanceCounter();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    auto* app = static_cast<AppState*>(appstate);
    ImGui_ImplSDL3_ProcessEvent(event);

    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;

    if (event->type == SDL_EVENT_WILL_ENTER_BACKGROUND) {
        app->engine.suspendSurface();
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_DID_ENTER_FOREGROUND) {
        app->engine.resumeSurface(app->isAndroid);
        return SDL_APP_CONTINUE;
    }
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
        app->engine.needResize = true;
    }

    if (ImGui::GetIO().WantCaptureMouse) {
        if (event->type == SDL_EVENT_MOUSE_BUTTON_UP) app->dragging = false;
        return SDL_APP_CONTINUE;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN) app->dragging = true;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP)   app->dragging = false;
    if (event->type == SDL_EVENT_MOUSE_MOTION && app->dragging) {
        app->director.yaw   -= event->motion.xrel * 0.005f;
        app->director.pitch  = glm::clamp(app->director.pitch + event->motion.yrel * 0.005f, -1.45f, 1.45f);
    }
    if (event->type == SDL_EVENT_MOUSE_WHEEL) {
        app->director.distance = std::clamp(app->director.distance * (event->wheel.y > 0 ? 0.88 : 1.14), 3.4, 650.0);
    }

    if (event->type == SDL_EVENT_FINGER_DOWN) {
        app->activeFingers[event->tfinger.fingerID] = {event->tfinger.x, event->tfinger.y};
        app->lastPinchDist = 0.0f;
    } else if (event->type == SDL_EVENT_FINGER_UP || event->type == SDL_EVENT_FINGER_CANCELED) {
        app->activeFingers.erase(event->tfinger.fingerID);
        app->lastPinchDist = 0.0f;
    } else if (event->type == SDL_EVENT_FINGER_MOTION) {
        app->activeFingers[event->tfinger.fingerID] = {event->tfinger.x, event->tfinger.y};
        if (app->activeFingers.size() == 1) {
            app->director.yaw   -= event->tfinger.dx * 3.0f;
            app->director.pitch  = glm::clamp(app->director.pitch + event->tfinger.dy * 2.2f, -1.45f, 1.45f);
        } else if (app->activeFingers.size() >= 2) {
            auto it = app->activeFingers.begin();
            glm::vec2 p1 = it->second; ++it;
            glm::vec2 p2 = it->second;
            float dist = glm::length(p2 - p1);
            if (app->lastPinchDist > 0.005f && dist > 0.005f) {
                float ratio = app->lastPinchDist / dist;
                app->director.distance = std::clamp(app->director.distance * double(ratio), 3.4, 650.0);
            }
            app->lastPinchDist = dist;
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    auto* app = static_cast<AppState*>(appstate);
    if (app->engine.suspended) return SDL_APP_CONTINUE;

    uint64_t now = SDL_GetPerformanceCounter();
    float dt = std::clamp(float(now - app->lastCounter) / float(SDL_GetPerformanceFrequency()), 0.001f, 0.05f);
    app->lastCounter = now;
    app->smoothedFps = glm::mix(app->smoothedFps, 1.0f / dt, 0.08f);

    app->director.update(dt, app->params.kerrSpin, app->params.nebulaDensity);

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

#if AETHERIA_STAGE == 0
    ImGui::SetNextWindowPos(ImVec2(24, 24), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("AETHERIA-GR // Stage 0 Vulkan Verification")) {
        const auto& d = app->engine.diag;
        ImGui::TextColored(ImVec4(0.25f, 0.95f, 0.45f, 1.0f), "STATUS: VULKAN PIPELINE ACTIVE");
        ImGui::Separator();
        ImGui::Text("GPU:        %s", d.deviceName.c_str());
        ImGui::Text("Vulkan API: %u.%u", d.apiMajor, d.apiMinor);
        ImGui::Text("Swapchain:  %ux%u (%.0f FPS)",
                    app->engine.swapchainExtent.width, app->engine.swapchainExtent.height, app->smoothedFps);
        ImGui::Text("Shared Mem: %u bytes | Stage SSBOs: %u", d.maxSharedMemBytes, d.maxVertexSSBOs);
        ImGui::Text("HDR16F Storage: %s | Linear Filter: %s",
                    d.hdrStorageSupported ? "YES" : "NO",
                    d.hdrLinearFilterSupported ? "YES" : "NO");
        ImGui::Separator();
        ImGui::TextWrapped("Stage 0 passed! Re-run GitHub Actions with stage=1 (Kerr BH) or stage=2 (Full Cosmos).");
    }
    ImGui::End();
#else
    aetheria::SpaceGUI::renderHUD(
        app->params, app->director, app->smoothedFps, 1000.0f / std::max(app->smoothedFps, 1.0f),
        [app](aetheria::CosmicScenePreset preset) { loadPresetBodies(app, preset); },
        [app]() {
            aetheria::GPUBody rogue{};
            glm::vec3 cam = app->director.getLocalCameraPosition();
            glm::vec3 spawnPos = cam * 0.45f + glm::vec3(12.0f, 3.0f, -8.0f);
            rogue.posMass   = glm::vec4(spawnPos, 8.5e6f);
            rogue.velTemp   = glm::vec4(-glm::normalize(spawnPos) * 4.5f, 2.5e7f);
            rogue.accRadius = glm::vec4(0.0f, 0.0f, 0.0f, 2.8f);
            rogue.meta      = glm::vec4(float(aetheria::BodyClass::SupermassiveBH), 9.0f, 0.98f, 1e9f);
            app->engine.injectBody(rogue);
        },
        [app]() {
            app->engine.triggerSupernovaImpulse(glm::vec3(0.0f), 14.0f);
            app->params.nebulaDensity = std::min(app->params.nebulaDensity + 0.55f, 2.0f);
        }
    );
#endif
    ImGui::Render();

    float aspect = float(app->engine.swapchainExtent.width) / float(std::max(app->engine.swapchainExtent.height, 1u));
    glm::mat4 viewProj = app->director.getViewProj(aspect);
    glm::mat4 invVP    = glm::inverse(viewProj);
    glm::vec3 camPos   = app->director.getLocalCameraPosition();
    float timeSec      = float(SDL_GetTicks()) * 0.001f;

    glm::vec3 forward  = glm::normalize(glm::vec3(app->director.targetPos - app->director.originOffset) - camPos);
    glm::vec3 camRight = glm::normalize(glm::cross(forward, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 camUp    = glm::cross(camRight, forward);

    aetheria::KerrUniforms kerr{};
    kerr.invViewProj   = invVP;
    kerr.cameraPosTime = glm::vec4(camPos, timeSec);
    kerr.bhParams      = glm::vec4(1.0f, app->params.kerrSpin,
                                   float(aetheria::AstrophysicsSolver::kerrISCO(app->params.kerrSpin)), 16.5f);
    kerr.diskVisuals   = glm::vec4(app->params.accretionRate, app->params.dopplerStrength,
                                   app->params.jetPower, app->director.warpLensStrength);
    kerr.nebulaParams  = glm::vec4(app->params.nebulaDensity, 1.4f, app->params.nebulaTurbulence, 1.0f);
    kerr.screenProfile = glm::ivec4(0, 0, app->params.mobilePerfMode ? 84 : 200, app->params.mobilePerfMode ? 0 : 1);

    aetheria::NebulaUBO neb{};
    neb.invViewProj  = invVP;
    neb.camPosTime   = glm::vec4(camPos, timeSec);
    neb.nebulaParams = glm::vec4(app->params.nebulaDensity, 1.45f, app->params.nebulaTurbulence,
                                 app->params.mobilePerfMode ? 24.0f : 56.0f);

    aetheria::NBodyPushConstants nbodyPC{};
    nbodyPC.dt                 = dt * app->params.timeScale * 0.45f;
    nbodyPC.G_sim              = 0.0044985f;
    nbodyPC.softeningSq        = 0.35f;
    nbodyPC.darkMatterHaloMass = app->params.darkMatterHaloMass;
    nbodyPC.darkMatterScaleR   = 18.0f;
    nbodyPC.cSpeedSq           = 900.0f;

    aetheria::StarPushConstants starPC{viewProj, glm::vec4(camRight, 0.0f), glm::vec4(camUp, 0.0f)};

    app->engine.drawFrame(kerr, neb, nbodyPC, starPC, !app->params.paused, app->isAndroid);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    (void)result;
    auto* app = static_cast<AppState*>(appstate);
    if (app) {
        app->engine.cleanup();
        if (app->window) SDL_DestroyWindow(app->window);
        delete app;
    }
    SDL_Quit();
}
