#pragma once
#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "../physics/AstrophysicsCore.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <array>

#ifndef AETHERIA_STAGE
#define AETHERIA_STAGE 2
#endif

#define VK_CHECK(x) do { VkResult err = (x); if (err != VK_SUCCESS) { \
    SDL_Log("Vulkan Error %d at %s:%d", (int)err, __FILE__, __LINE__); \
    throw std::runtime_error("Vulkan failure"); } } while (0)

namespace aetheria {

struct alignas(16) NebulaUBO {
    glm::mat4 invViewProj;
    glm::vec4 camPosTime;
    glm::vec4 nebulaParams;
};

struct alignas(16) NBodyPushConstants {
    float dt;
    float G_sim;
    float softeningSq;
    float darkMatterHaloMass;
    float darkMatterScaleR;
    float cSpeedSq;
    uint32_t bodyCount;
    uint32_t stepPhase;
};

struct alignas(16) StarPushConstants {
    glm::mat4 viewProj;
    glm::vec4 camRight;
    glm::vec4 camUp;
};

struct BufferAlloc {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    void* mapped = nullptr;
    VkDeviceSize size = 0;
};

struct GpuDiagnostics {
    std::string deviceName = "Unknown GPU";
    uint32_t apiMajor = 1;
    uint32_t apiMinor = 0;
    uint32_t maxSharedMemBytes = 0;
    uint32_t maxVertexSSBOs = 0;
    bool hdrStorageSupported = false;
    bool hdrLinearFilterSupported = false;
};

class VulkanEngine {
public:
    static constexpr uint32_t MAX_BODIES = 65536;

    SDL_Window* window = nullptr;
    GpuDiagnostics diag{};
    VkInstance instance = VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapchainExtent{1280, 720};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkFramebuffer> framebuffers;
    VkRenderPass renderPass = VK_NULL_HANDLE;

    VkImage hdrImage = VK_NULL_HANDLE;
    VkDeviceMemory hdrMemory = VK_NULL_HANDLE;
    VkImageView hdrView = VK_NULL_HANDLE;
    VkSampler hdrSampler = VK_NULL_HANDLE;
    VkExtent2D hdrExtent{1280, 720};

    BufferAlloc bodySSBO{};
    BufferAlloc kerrUBO{};
    BufferAlloc nebulaUBO{};
    uint32_t activeBodyCount = 0;

    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;

    VkDescriptorSetLayout nbodySetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout kerrSetLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout nebulaSetLayout = VK_NULL_HANDLE;
    VkDescriptorSet nbodySet = VK_NULL_HANDLE;
    VkDescriptorSet kerrSet = VK_NULL_HANDLE;
    VkDescriptorSet nebulaSet = VK_NULL_HANDLE;

    VkPipelineLayout trianglePipeLayout = VK_NULL_HANDLE;
    VkPipelineLayout nbodyPipeLayout = VK_NULL_HANDLE;
    VkPipelineLayout kerrPipeLayout = VK_NULL_HANDLE;
    VkPipelineLayout nebulaPipeLayout = VK_NULL_HANDLE;
    VkPipelineLayout starsPipeLayout = VK_NULL_HANDLE;

    VkPipeline trianglePipeline = VK_NULL_HANDLE;
    VkPipeline nbodyPipeline = VK_NULL_HANDLE;
    VkPipeline kerrPipeline = VK_NULL_HANDLE;
    VkPipeline nebulaPipeline = VK_NULL_HANDLE;
    VkPipeline starsPipeline = VK_NULL_HANDLE;

    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmdBuf = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSem = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSem = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
    bool hdrInitialized = false;
    bool needResize = false;
    bool suspended = false;
    bool imguiContextCreated = false;
    bool imguiSdlInitialized = false;
    bool imguiVulkanInitialized = false;

    void init(SDL_Window* win, bool isAndroid) {
        window = win;
        createInstance();
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }
        pickAndVerifyPhysicalDevice();
        createLogicalDevice();
        createCommandObjects();
        createSwapchainAndRenderPass();

#if AETHERIA_STAGE >= 1
        createHdrStorageTarget(isAndroid);
        createBuffers();
        createDescriptors();
#endif
        createPipelines();
        initImGui(isAndroid);
    }

    void suspendSurface() {
        if (!device || suspended) return;
        suspended = true;
        vkDeviceWaitIdle(device);
        destroySwapchainObjects();
        if (surface) {
            vkDestroySurfaceKHR(instance, surface, nullptr);
            surface = VK_NULL_HANDLE;
        }
    }

    void resumeSurface(bool isAndroid) {
        if (!device || !suspended) return;
        if (!SDL_Vulkan_CreateSurface(window, instance, nullptr, &surface)) {
            SDL_Log("Resume surface creation failed: %s", SDL_GetError());
            return;
        }
        createSwapchainAndRenderPass();
#if AETHERIA_STAGE >= 1
        rebuildHdrTarget(isAndroid);
#else
        (void)isAndroid;
#endif
        suspended = false;
        needResize = false;
    }

    void uploadBodies(const std::vector<GPUBody>& bodies) {
#if AETHERIA_STAGE >= 2
        if (!bodySSBO.mapped) return;
        vkDeviceWaitIdle(device);
        activeBodyCount = static_cast<uint32_t>(std::min<size_t>(bodies.size(), MAX_BODIES));
        std::memcpy(bodySSBO.mapped, bodies.data(), activeBodyCount * sizeof(GPUBody));
#else
        (void)bodies;
#endif
    }

    void injectBody(const GPUBody& body) {
#if AETHERIA_STAGE >= 2
        if (!bodySSBO.mapped || activeBodyCount >= MAX_BODIES) return;
        // The previous frame may still be reading this mapped SSBO.
        vkDeviceWaitIdle(device);
        auto* ptr = static_cast<GPUBody*>(bodySSBO.mapped);
        ptr[activeBodyCount++] = body;
#else
        (void)body;
#endif
    }

    void triggerSupernovaImpulse(const glm::vec3& epicenter, float blastImpulse) {
#if AETHERIA_STAGE >= 2
        if (!bodySSBO.mapped) return;
        // Avoid host writes racing with the GPU's previous compute/vertex use.
        vkDeviceWaitIdle(device);
        auto* ptr = static_cast<GPUBody*>(bodySSBO.mapped);
        for (uint32_t i = 0; i < activeBodyCount; ++i) {
            glm::vec3 d = glm::vec3(ptr[i].posMass) - epicenter;
            float dist = glm::length(d);
            if (dist > 0.2f && dist < 32.0f) {
                glm::vec3 push = (d / dist) * (blastImpulse / (1.0f + dist * 0.25f));
                ptr[i].velTemp.x += push.x;
                ptr[i].velTemp.y += push.y;
                ptr[i].velTemp.z += push.z;
                ptr[i].velTemp.w = std::min(ptr[i].velTemp.w + 14000.0f, 38000.0f);
            }
        }
#else
        (void)epicenter; (void)blastImpulse;
#endif
    }

    void drawFrame(
        const KerrUniforms& kerrData,
        const NebulaUBO& nebulaData,
        const NBodyPushConstants& nbodyPC,
        const StarPushConstants& starPC,
        bool runPhysics,
        bool isAndroid
    ) {
        if (suspended || swapchain == VK_NULL_HANDLE) return;
        if (needResize) {
            recreateSwapchain(isAndroid);
            needResize = false;
            if (swapchain == VK_NULL_HANDLE) return;
        }

        vkWaitForFences(device, 1, &inFlightFence, VK_TRUE, UINT64_MAX);

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, imageAvailableSem, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) {
            needResize = true;
            return;
        }
        if (acq != VK_SUCCESS && acq != VK_SUBOPTIMAL_KHR) {
            VK_CHECK(acq);
        }
        vkResetFences(device, 1, &inFlightFence);

#if AETHERIA_STAGE >= 1
        KerrUniforms kerrCopy = kerrData;
        kerrCopy.screenProfile.x = static_cast<int32_t>(hdrExtent.width);
        kerrCopy.screenProfile.y = static_cast<int32_t>(hdrExtent.height);
        std::memcpy(kerrUBO.mapped, &kerrCopy, sizeof(KerrUniforms));

        NebulaUBO nebCopy = nebulaData;
#if AETHERIA_STAGE == 1
        nebCopy.nebulaParams.x = 0.0f;
#endif
        std::memcpy(nebulaUBO.mapped, &nebCopy, sizeof(NebulaUBO));
#else
        (void)kerrData; (void)nebulaData;
#endif

        vkResetCommandBuffer(cmdBuf, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cmdBuf, &bi));

#if AETHERIA_STAGE >= 2
        if (runPhysics && activeBodyCount > 0) {
            uint32_t groupsX = (activeBodyCount + 127u) / 128u;
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, nbodyPipeline);
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, nbodyPipeLayout, 0, 1, &nbodySet, 0, nullptr);

            NBodyPushConstants pc = nbodyPC;
            pc.bodyCount = activeBodyCount;
            pc.stepPhase = 0u;
            vkCmdPushConstants(cmdBuf, nbodyPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmdBuf, groupsX, 1, 1);

            insertMemoryBarrier(cmdBuf, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

            pc.stepPhase = 1u;
            vkCmdPushConstants(cmdBuf, nbodyPipeLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);
            vkCmdDispatch(cmdBuf, groupsX, 1, 1);

            insertMemoryBarrier(cmdBuf, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT);
        }
#else
        (void)nbodyPC; (void)runPhysics;
#endif

#if AETHERIA_STAGE >= 1
        transitionImage(cmdBuf, hdrImage,
                        hdrInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_GENERAL,
                        0, VK_ACCESS_SHADER_WRITE_BIT,
                        hdrInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        hdrInitialized = true;

        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, kerrPipeline);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_COMPUTE, kerrPipeLayout, 0, 1, &kerrSet, 0, nullptr);
        vkCmdDispatch(cmdBuf, (hdrExtent.width + 7u) / 8u, (hdrExtent.height + 7u) / 8u, 1);

        transitionImage(cmdBuf, hdrImage,
                        VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                        VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
#endif

        VkClearValue clearVal{{{0.02f, 0.03f, 0.06f, 1.0f}}};
        VkRenderPassBeginInfo rpbi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        rpbi.renderPass = renderPass;
        rpbi.framebuffer = framebuffers[imageIndex];
        rpbi.renderArea = {{0, 0}, swapchainExtent};
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clearVal;
        vkCmdBeginRenderPass(cmdBuf, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport vp{0.0f, 0.0f, float(swapchainExtent.width), float(swapchainExtent.height), 0.0f, 1.0f};
        VkRect2D scissor{{0, 0}, swapchainExtent};
        vkCmdSetViewport(cmdBuf, 0, 1, &vp);
        vkCmdSetScissor(cmdBuf, 0, 1, &scissor);

#if AETHERIA_STAGE == 0
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, trianglePipeline);
        vkCmdDraw(cmdBuf, 3, 1, 0, 0);
        (void)starPC;
#else
        vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, nebulaPipeline);
        vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, nebulaPipeLayout, 0, 1, &nebulaSet, 0, nullptr);
        vkCmdDraw(cmdBuf, 3, 1, 0, 0);
#if AETHERIA_STAGE >= 2
        if (activeBodyCount > 0) {
            vkCmdBindPipeline(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, starsPipeline);
            vkCmdBindDescriptorSets(cmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, starsPipeLayout, 0, 1, &nbodySet, 0, nullptr);
            vkCmdPushConstants(cmdBuf, starsPipeLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(StarPushConstants), &starPC);
            vkCmdDraw(cmdBuf, 6, activeBodyCount, 0, 0);
        }
#else
        (void)starPC;
#endif
#endif

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmdBuf);
        vkCmdEndRenderPass(cmdBuf);
        VK_CHECK(vkEndCommandBuffer(cmdBuf));

        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
        si.waitSemaphoreCount = 1;
        si.pWaitSemaphores = &imageAvailableSem;
        si.pWaitDstStageMask = &waitStage;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cmdBuf;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &renderFinishedSem;
        VK_CHECK(vkQueueSubmit(queue, 1, &si, inFlightFence));

        VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &renderFinishedSem;
        pi.swapchainCount = 1;
        pi.pSwapchains = &swapchain;
        pi.pImageIndices = &imageIndex;
        VkResult pres = vkQueuePresentKHR(queue, &pi);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) {
            needResize = true;
        }
    }

    void cleanup() {
        if (device) vkDeviceWaitIdle(device);

        if (imguiVulkanInitialized) {
            ImGui_ImplVulkan_Shutdown();
            imguiVulkanInitialized = false;
        }
        if (imguiSdlInitialized) {
            ImGui_ImplSDL3_Shutdown();
            imguiSdlInitialized = false;
        }
        if (imguiContextCreated) {
            ImGui::DestroyContext();
            imguiContextCreated = false;
        }

        if (device) {
            if (trianglePipeline) vkDestroyPipeline(device, trianglePipeline, nullptr);
            if (nbodyPipeline)    vkDestroyPipeline(device, nbodyPipeline, nullptr);
            if (kerrPipeline)     vkDestroyPipeline(device, kerrPipeline, nullptr);
            if (nebulaPipeline)   vkDestroyPipeline(device, nebulaPipeline, nullptr);
            if (starsPipeline)    vkDestroyPipeline(device, starsPipeline, nullptr);

            if (trianglePipeLayout) vkDestroyPipelineLayout(device, trianglePipeLayout, nullptr);
            if (nbodyPipeLayout)    vkDestroyPipelineLayout(device, nbodyPipeLayout, nullptr);
            if (kerrPipeLayout)     vkDestroyPipelineLayout(device, kerrPipeLayout, nullptr);
            if (nebulaPipeLayout)   vkDestroyPipelineLayout(device, nebulaPipeLayout, nullptr);
            if (starsPipeLayout)    vkDestroyPipelineLayout(device, starsPipeLayout, nullptr);

            if (nbodySetLayout)  vkDestroyDescriptorSetLayout(device, nbodySetLayout, nullptr);
            if (kerrSetLayout)   vkDestroyDescriptorSetLayout(device, kerrSetLayout, nullptr);
            if (nebulaSetLayout) vkDestroyDescriptorSetLayout(device, nebulaSetLayout, nullptr);
            if (descriptorPool)  vkDestroyDescriptorPool(device, descriptorPool, nullptr);
            if (imguiPool)       vkDestroyDescriptorPool(device, imguiPool, nullptr);

            destroyBuffer(bodySSBO);
            destroyBuffer(kerrUBO);
            destroyBuffer(nebulaUBO);
            destroyHdrTarget();
            destroySwapchainObjects();

            if (renderPass)        vkDestroyRenderPass(device, renderPass, nullptr);
            if (imageAvailableSem) vkDestroySemaphore(device, imageAvailableSem, nullptr);
            if (renderFinishedSem) vkDestroySemaphore(device, renderFinishedSem, nullptr);
            if (inFlightFence)     vkDestroyFence(device, inFlightFence, nullptr);
            if (cmdPool)           vkDestroyCommandPool(device, cmdPool, nullptr);
            vkDestroyDevice(device, nullptr);
            device = VK_NULL_HANDLE;
        }

        if (surface && instance) vkDestroySurfaceKHR(instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
        if (instance) vkDestroyInstance(instance, nullptr);
        instance = VK_NULL_HANDLE;

        physicalDevice = VK_NULL_HANDLE;
        queue = VK_NULL_HANDLE;
        swapchain = VK_NULL_HANDLE;
    }

private:
    void createInstance() {
        Uint32 extCount = 0;
        const char* const* exts = SDL_Vulkan_GetInstanceExtensions(&extCount);
        if (!exts || extCount == 0) {
            throw std::runtime_error(std::string("SDL Vulkan instance extensions unavailable: ") + SDL_GetError());
        }

        VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        appInfo.pApplicationName = "AETHERIA-GR";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "AETHERIA_VULKAN";
        appInfo.apiVersion = VK_API_VERSION_1_1;

        VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        ci.pApplicationInfo = &appInfo;
        ci.enabledExtensionCount = extCount;
        ci.ppEnabledExtensionNames = exts;
        VK_CHECK(vkCreateInstance(&ci, nullptr, &instance));
    }

    static bool supportsSwapchainExtension(VkPhysicalDevice gpu) {
        uint32_t extCount = 0;
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, nullptr);
        std::vector<VkExtensionProperties> exts(extCount);
        vkEnumerateDeviceExtensionProperties(gpu, nullptr, &extCount, exts.data());
        for (const auto& e : exts) {
            if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) return true;
        }
        return false;
    }

    void pickAndVerifyPhysicalDevice() {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance, &count, nullptr);
        if (count == 0) throw std::runtime_error("No Vulkan GPU detected");
        std::vector<VkPhysicalDevice> gpus(count);
        vkEnumeratePhysicalDevices(instance, &count, gpus.data());

        VkPhysicalDevice best = VK_NULL_HANDLE;
        uint32_t bestQueue = UINT32_MAX;

        for (auto gpu : gpus) {
            if (!supportsSwapchainExtension(gpu)) continue;

            uint32_t qCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qCount, nullptr);
            std::vector<VkQueueFamilyProperties> qProps(qCount);
            vkGetPhysicalDeviceQueueFamilyProperties(gpu, &qCount, qProps.data());

            for (uint32_t i = 0; i < qCount; ++i) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
                bool hasGfxCompute = (qProps[i].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT)) ==
                                     (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
                if (hasGfxCompute && present) {
                    VkPhysicalDeviceProperties props{};
                    vkGetPhysicalDeviceProperties(gpu, &props);
                    if (best == VK_NULL_HANDLE || props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                        best = gpu;
                        bestQueue = i;
                    }
                    break;
                }
            }
        }

        if (best == VK_NULL_HANDLE) {
            throw std::runtime_error("No GPU with Swapchain + Graphics + Compute + Present queue found");
        }
        physicalDevice = best;
        queueFamily = bestQueue;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(physicalDevice, &props);
        diag.deviceName        = props.deviceName;
        diag.apiMajor          = VK_VERSION_MAJOR(props.apiVersion);
        diag.apiMinor          = VK_VERSION_MINOR(props.apiVersion);
        diag.maxSharedMemBytes = props.limits.maxComputeSharedMemorySize;
        diag.maxVertexSSBOs    = props.limits.maxPerStageDescriptorStorageBuffers;

        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_R16G16B16A16_SFLOAT, &fp);
        diag.hdrStorageSupported =
            (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0 &&
            (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
        diag.hdrLinearFilterSupported =
            (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;

        SDL_Log("AETHERIA-GR GPU: %s (Vulkan %u.%u) | SharedMem=%u B | StageSSBO=%u | HDR_Storage=%d | HDR_Linear=%d",
                diag.deviceName.c_str(), diag.apiMajor, diag.apiMinor,
                diag.maxSharedMemBytes, diag.maxVertexSSBOs,
                (int)diag.hdrStorageSupported, (int)diag.hdrLinearFilterSupported);

#if AETHERIA_STAGE >= 1
        if (!diag.hdrStorageSupported) {
            throw std::runtime_error("GPU lacks R16G16B16A16_SFLOAT STORAGE_IMAGE support");
        }
#endif
#if AETHERIA_STAGE >= 2
        if (diag.maxSharedMemBytes < 2048 || diag.maxVertexSSBOs < 1) {
            throw std::runtime_error("GPU compute/SSBO limits below AETHERIA Stage 2 requirements");
        }
#endif
    }

    void createLogicalDevice() {
        float prio = 1.0f;
        VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        qci.queueFamilyIndex = queueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &prio;

        const char* devExts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        VkPhysicalDeviceFeatures supported{};
        vkGetPhysicalDeviceFeatures(physicalDevice, &supported);
        VkPhysicalDeviceFeatures enabled{};
#if AETHERIA_STAGE >= 1
        // rgba16f storage images require this Vulkan feature on devices where
        // the format is not part of the baseline storage-image format set.
        if (!supported.shaderStorageImageExtendedFormats) {
            throw std::runtime_error("GPU lacks shaderStorageImageExtendedFormats required by the RGBA16F HDR target");
        }
        enabled.shaderStorageImageExtendedFormats = VK_TRUE;
#endif
        VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        dci.pEnabledFeatures = &enabled;
        dci.queueCreateInfoCount = 1;
        dci.pQueueCreateInfos = &qci;
        dci.enabledExtensionCount = 1;
        dci.ppEnabledExtensionNames = devExts;
        VK_CHECK(vkCreateDevice(physicalDevice, &dci, nullptr, &device));
        vkGetDeviceQueue(device, queueFamily, 0, &queue);
    }

    void createCommandObjects() {
        VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cpci.queueFamilyIndex = queueFamily;
        VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &cmdPool));

        VkCommandBufferAllocateInfo cbai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
        cbai.commandPool = cmdPool;
        cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cbai.commandBufferCount = 1;
        VK_CHECK(vkAllocateCommandBuffers(device, &cbai, &cmdBuf));

        VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &imageAvailableSem));
        VK_CHECK(vkCreateSemaphore(device, &sci, nullptr, &renderFinishedSem));
        VK_CHECK(vkCreateFence(device, &fci, nullptr, &inFlightFence));
    }

    void createSwapchainAndRenderPass() {
        VkSurfaceCapabilitiesKHR caps{};
        VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &caps));

        uint32_t fmtCount = 0;
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, nullptr));
        if (fmtCount == 0) throw std::runtime_error("Vulkan surface reports no supported formats");
        std::vector<VkSurfaceFormatKHR> fmts(fmtCount);
        VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &fmtCount, fmts.data()));

        VkSurfaceFormatKHR chosen = fmts[0];
        for (const auto& f : fmts) {
            if (f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) {
                chosen = f;
                break;
            }
        }
        swapchainFormat = chosen.format;

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        if (w <= 0 || h <= 0) return;

        swapchainExtent = (caps.currentExtent.width != UINT32_MAX && caps.currentExtent.width > 0)
            ? caps.currentExtent
            : VkExtent2D{std::clamp<uint32_t>(uint32_t(w), caps.minImageExtent.width, caps.maxImageExtent.width),
                         std::clamp<uint32_t>(uint32_t(h), caps.minImageExtent.height, caps.maxImageExtent.height)};

        uint32_t imgCount = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && imgCount > caps.maxImageCount) {
            imgCount = caps.maxImageCount;
        }

        VkCompositeAlphaFlagBitsKHR alphaMode = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        if (!(caps.supportedCompositeAlpha & alphaMode)) {
            alphaMode = VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR;
        }

        VkSurfaceTransformFlagBitsKHR preTransform =
            (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
                ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
                : caps.currentTransform;

        VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
        sci.surface = surface;
        sci.minImageCount = imgCount;
        sci.imageFormat = chosen.format;
        sci.imageColorSpace = chosen.colorSpace;
        sci.imageExtent = swapchainExtent;
        sci.imageArrayLayers = 1;
        sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        sci.preTransform = preTransform;
        sci.compositeAlpha = alphaMode;
        sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
        sci.clipped = VK_TRUE;
        VK_CHECK(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain));

        uint32_t scCount = 0;
        vkGetSwapchainImagesKHR(device, swapchain, &scCount, nullptr);
        swapchainImages.resize(scCount);
        vkGetSwapchainImagesKHR(device, swapchain, &scCount, swapchainImages.data());

        if (renderPass == VK_NULL_HANDLE) {
            VkAttachmentDescription colorAtt{};
            colorAtt.format = swapchainFormat;
            colorAtt.samples = VK_SAMPLE_COUNT_1_BIT;
            colorAtt.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            colorAtt.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            colorAtt.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            colorAtt.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            colorAtt.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            colorAtt.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

            VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
            VkSubpassDescription sub{};
            sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            sub.colorAttachmentCount = 1;
            sub.pColorAttachments = &colorRef;

            VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
            rpci.attachmentCount = 1;
            rpci.pAttachments = &colorAtt;
            rpci.subpassCount = 1;
            rpci.pSubpasses = &sub;
            VK_CHECK(vkCreateRenderPass(device, &rpci, nullptr, &renderPass));
        }

        swapchainViews.resize(scCount);
        framebuffers.resize(scCount);
        for (uint32_t i = 0; i < scCount; ++i) {
            VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            ivci.image = swapchainImages[i];
            ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ivci.format = swapchainFormat;
            ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(device, &ivci, nullptr, &swapchainViews[i]));

            VkFramebufferCreateInfo fbci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fbci.renderPass = renderPass;
            fbci.attachmentCount = 1;
            fbci.pAttachments = &swapchainViews[i];
            fbci.width = swapchainExtent.width;
            fbci.height = swapchainExtent.height;
            fbci.layers = 1;
            VK_CHECK(vkCreateFramebuffer(device, &fbci, nullptr, &framebuffers[i]));
        }
    }

    void destroySwapchainObjects() {
        for (auto fb : framebuffers) vkDestroyFramebuffer(device, fb, nullptr);
        for (auto iv : swapchainViews) vkDestroyImageView(device, iv, nullptr);
        framebuffers.clear();
        swapchainViews.clear();
        if (swapchain) vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }

    void createHdrStorageTarget(bool isAndroid) {
        float scale = isAndroid ? 0.65f : 1.0f;
        hdrExtent = {
            std::max(1u, static_cast<uint32_t>(swapchainExtent.width * scale)),
            std::max(1u, static_cast<uint32_t>(swapchainExtent.height * scale))
        };
        hdrInitialized = false;

        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ici.extent = {hdrExtent.width, hdrExtent.height, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        VK_CHECK(vkCreateImage(device, &ici, nullptr, &hdrImage));

        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(device, hdrImage, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &hdrMemory));
        VK_CHECK(vkBindImageMemory(device, hdrImage, hdrMemory, 0));

        VkImageViewCreateInfo ivci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        ivci.image = hdrImage;
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = VK_FORMAT_R16G16B16A16_SFLOAT;
        ivci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        VK_CHECK(vkCreateImageView(device, &ivci, nullptr, &hdrView));

        if (!hdrSampler) {
            VkFilter filt = diag.hdrLinearFilterSupported ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
            VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
            sci.magFilter = filt;
            sci.minFilter = filt;
            sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            VK_CHECK(vkCreateSampler(device, &sci, nullptr, &hdrSampler));
        }
    }

    void rebuildHdrTarget(bool isAndroid) {
        if (hdrView) vkDestroyImageView(device, hdrView, nullptr);
        if (hdrImage) vkDestroyImage(device, hdrImage, nullptr);
        if (hdrMemory) vkFreeMemory(device, hdrMemory, nullptr);
        hdrView = VK_NULL_HANDLE; hdrImage = VK_NULL_HANDLE; hdrMemory = VK_NULL_HANDLE;
        createHdrStorageTarget(isAndroid);
        updateImageDescriptors();
    }

    void destroyHdrTarget() {
        if (hdrView) vkDestroyImageView(device, hdrView, nullptr);
        if (hdrImage) vkDestroyImage(device, hdrImage, nullptr);
        if (hdrMemory) vkFreeMemory(device, hdrMemory, nullptr);
        if (hdrSampler) vkDestroySampler(device, hdrSampler, nullptr);
        hdrView = VK_NULL_HANDLE; hdrImage = VK_NULL_HANDLE;
        hdrMemory = VK_NULL_HANDLE; hdrSampler = VK_NULL_HANDLE;
    }

    void recreateSwapchain(bool isAndroid) {
        vkDeviceWaitIdle(device);
        destroySwapchainObjects();
        createSwapchainAndRenderPass();
#if AETHERIA_STAGE >= 1
        if (swapchain != VK_NULL_HANDLE) rebuildHdrTarget(isAndroid);
#else
        (void)isAndroid;
#endif
    }

    uint32_t findMemoryType(uint32_t filter, VkMemoryPropertyFlags props) {
        VkPhysicalDeviceMemoryProperties mp{};
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((filter & (1u << i)) && (mp.memoryTypes[i].propertyFlags & props) == props) return i;
        }
        throw std::runtime_error("No matching Vulkan memory type");
    }

    BufferAlloc createHostBuffer(VkDeviceSize size, VkBufferUsageFlags usage) {
        BufferAlloc out{};
        out.size = size;
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = size;
        bci.usage = usage;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VK_CHECK(vkCreateBuffer(device, &bci, nullptr, &out.buffer));

        VkMemoryRequirements req{};
        vkGetBufferMemoryRequirements(device, out.buffer, &req);
        VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai.allocationSize = req.size;
        mai.memoryTypeIndex = findMemoryType(req.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VK_CHECK(vkAllocateMemory(device, &mai, nullptr, &out.memory));
        VK_CHECK(vkBindBufferMemory(device, out.buffer, out.memory, 0));
        VK_CHECK(vkMapMemory(device, out.memory, 0, size, 0, &out.mapped));
        return out;
    }

    void destroyBuffer(BufferAlloc& b) {
        if (b.mapped) vkUnmapMemory(device, b.memory);
        if (b.buffer) vkDestroyBuffer(device, b.buffer, nullptr);
        if (b.memory) vkFreeMemory(device, b.memory, nullptr);
        b = {};
    }

    void createBuffers() {
        bodySSBO  = createHostBuffer(MAX_BODIES * sizeof(GPUBody), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
        kerrUBO   = createHostBuffer(sizeof(KerrUniforms), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        nebulaUBO = createHostBuffer(sizeof(NebulaUBO), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }

    void createDescriptors() {
        VkDescriptorSetLayoutBinding b0{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
            VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo lci{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        lci.bindingCount = 1; lci.pBindings = &b0;
        VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &nbodySetLayout));

        std::array<VkDescriptorSetLayoutBinding, 2> kerrBindings = {{
            {0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr}
        }};
        lci.bindingCount = 2; lci.pBindings = kerrBindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &kerrSetLayout));

        std::array<VkDescriptorSetLayoutBinding, 2> nebBindings = {{
            {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr},
            {1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr}
        }};
        lci.bindingCount = 2; lci.pBindings = nebBindings.data();
        VK_CHECK(vkCreateDescriptorSetLayout(device, &lci, nullptr, &nebulaSetLayout));

        std::array<VkDescriptorPoolSize, 4> sizes = {{
            {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2},
            {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 2},
            {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4},
            {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2}
        }};
        VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpci.maxSets = 6;
        dpci.poolSizeCount = static_cast<uint32_t>(sizes.size());
        dpci.pPoolSizes = sizes.data();
        VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &descriptorPool));

        std::array<VkDescriptorSetLayout, 3> layouts = {nbodySetLayout, kerrSetLayout, nebulaSetLayout};
        VkDescriptorSet sets[3];
        VkDescriptorSetAllocateInfo dsai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dsai.descriptorPool = descriptorPool;
        dsai.descriptorSetCount = 3;
        dsai.pSetLayouts = layouts.data();
        VK_CHECK(vkAllocateDescriptorSets(device, &dsai, sets));
        nbodySet = sets[0]; kerrSet = sets[1]; nebulaSet = sets[2];

        VkDescriptorBufferInfo ssboInfo{bodySSBO.buffer, 0, bodySSBO.size};
        VkDescriptorBufferInfo kerrInfo{kerrUBO.buffer, 0, kerrUBO.size};
        VkDescriptorBufferInfo nebInfo{nebulaUBO.buffer, 0, nebulaUBO.size};

        std::array<VkWriteDescriptorSet, 3> writes{};
        writes[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, nbodySet, 0, 0, 1,
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssboInfo, nullptr};
        writes[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, kerrSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &kerrInfo, nullptr};
        writes[2] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, nebulaSet, 1, 0, 1,
                     VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &nebInfo, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
        updateImageDescriptors();
    }

    void updateImageDescriptors() {
        VkDescriptorImageInfo storageInfo{VK_NULL_HANDLE, hdrView, VK_IMAGE_LAYOUT_GENERAL};
        VkDescriptorImageInfo sampledInfo{hdrSampler, hdrView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

        std::array<VkWriteDescriptorSet, 2> imgWrites{};
        imgWrites[0] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, kerrSet, 0, 0, 1,
                        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, &storageInfo, nullptr, nullptr};
        imgWrites[1] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, nebulaSet, 0, 0, 1,
                        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &sampledInfo, nullptr, nullptr};
        vkUpdateDescriptorSets(device, static_cast<uint32_t>(imgWrites.size()), imgWrites.data(), 0, nullptr);
    }

    VkShaderModule loadShaderModule(const char* relativePath) {
        SDL_IOStream* io = SDL_IOFromFile(relativePath, "rb");
#ifndef __ANDROID__
        if (!io) {
            const char* base = SDL_GetBasePath();
            if (base) {
                std::string fullPath = std::string(base) + relativePath;
                io = SDL_IOFromFile(fullPath.c_str(), "rb");
            }
        }
#endif
        if (!io) {
            SDL_Log("Failed to load SPIR-V asset: %s (%s)", relativePath, SDL_GetError());
            throw std::runtime_error(std::string("Missing shader: ") + relativePath);
        }
        Sint64 size = SDL_GetIOSize(io);
        if (size < 4 || (size % 4) != 0) {
            SDL_CloseIO(io);
            throw std::runtime_error(std::string("Invalid SPIR-V byte size: ") + relativePath);
        }
        std::vector<uint32_t> code(static_cast<size_t>(size) / 4);
        size_t bytesRead = SDL_ReadIO(io, code.data(), static_cast<size_t>(size));
        SDL_CloseIO(io);
        if (bytesRead != static_cast<size_t>(size) || code.front() != 0x07230203u) {
            throw std::runtime_error(std::string("Corrupt or incomplete SPIR-V: ") + relativePath);
        }

        VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = static_cast<size_t>(size);
        smci.pCode = code.data();
        VkShaderModule mod = VK_NULL_HANDLE;
        VK_CHECK(vkCreateShaderModule(device, &smci, nullptr, &mod));
        return mod;
    }

    void createPipelines() {
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1; vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkDynamicState dynStates[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        dyn.dynamicStateCount = 2; dyn.pDynamicStates = dynStates;

        VkPipelineColorBlendAttachmentState opaqueBlend{};
        opaqueBlend.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1; cb.pAttachments = &opaqueBlend;

#if AETHERIA_STAGE == 0
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &trianglePipeLayout));

        VkShaderModule triVert = loadShaderModule("shaders/stage0_triangle.vert.spv");
        VkShaderModule triFrag = loadShaderModule("shaders/stage0_triangle.frag.spv");
        VkPipelineShaderStageCreateInfo stages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, triVert, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, triFrag, "main", nullptr}
        };
        VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gpci.stageCount = 2; gpci.pStages = stages;
        gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dyn; gpci.layout = trianglePipeLayout; gpci.renderPass = renderPass;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &trianglePipeline));
        vkDestroyShaderModule(device, triVert, nullptr);
        vkDestroyShaderModule(device, triFrag, nullptr);
#else
        VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        VkComputePipelineCreateInfo cpci{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};

        plci.setLayoutCount = 1; plci.pSetLayouts = &kerrSetLayout;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &kerrPipeLayout));

        VkShaderModule kerrMod = loadShaderModule("shaders/kerr_blackhole.comp.spv");
        cpci.stage = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                      VK_SHADER_STAGE_COMPUTE_BIT, kerrMod, "main", nullptr};
        cpci.layout = kerrPipeLayout;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &kerrPipeline));
        vkDestroyShaderModule(device, kerrMod, nullptr);

        plci.setLayoutCount = 1; plci.pSetLayouts = &nebulaSetLayout;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &nebulaPipeLayout));

        VkShaderModule fsVert = loadShaderModule("shaders/fullscreen.vert.spv");
        VkShaderModule nebFrag = loadShaderModule("shaders/nebula_volumetric.frag.spv");
        VkPipelineShaderStageCreateInfo nebStages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, fsVert, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, nebFrag, "main", nullptr}
        };
        VkGraphicsPipelineCreateInfo gpci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gpci.stageCount = 2; gpci.pStages = nebStages;
        gpci.pVertexInputState = &vi; gpci.pInputAssemblyState = &ia;
        gpci.pViewportState = &vp; gpci.pRasterizationState = &rs;
        gpci.pMultisampleState = &ms; gpci.pColorBlendState = &cb;
        gpci.pDynamicState = &dyn; gpci.layout = nebulaPipeLayout; gpci.renderPass = renderPass;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &nebulaPipeline));
        vkDestroyShaderModule(device, fsVert, nullptr);
        vkDestroyShaderModule(device, nebFrag, nullptr);

#if AETHERIA_STAGE >= 2
        VkPushConstantRange nbodyPCR{VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(NBodyPushConstants)};
        plci.setLayoutCount = 1; plci.pSetLayouts = &nbodySetLayout;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &nbodyPCR;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &nbodyPipeLayout));

        VkShaderModule nbodyMod = loadShaderModule("shaders/nbody_galactic.comp.spv");
        cpci.stage.module = nbodyMod;
        cpci.layout = nbodyPipeLayout;
        VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cpci, nullptr, &nbodyPipeline));
        vkDestroyShaderModule(device, nbodyMod, nullptr);

        VkPushConstantRange starPCR{VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(StarPushConstants)};
        plci.setLayoutCount = 1; plci.pSetLayouts = &nbodySetLayout;
        plci.pushConstantRangeCount = 1; plci.pPushConstantRanges = &starPCR;
        VK_CHECK(vkCreatePipelineLayout(device, &plci, nullptr, &starsPipeLayout));

        VkShaderModule starVert = loadShaderModule("shaders/stars_instanced.vert.spv");
        VkShaderModule starFrag = loadShaderModule("shaders/stars_instanced.frag.spv");
        VkPipelineShaderStageCreateInfo starStages[2] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT, starVert, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, starFrag, "main", nullptr}
        };
        VkPipelineColorBlendAttachmentState addBlend{};
        addBlend.blendEnable = VK_TRUE;
        addBlend.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        addBlend.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        addBlend.colorBlendOp = VK_BLEND_OP_ADD;
        addBlend.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        addBlend.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        addBlend.alphaBlendOp = VK_BLEND_OP_ADD;
        addBlend.colorWriteMask = 0xF;
        cb.pAttachments = &addBlend;

        gpci.pStages = starStages;
        gpci.layout = starsPipeLayout;
        VK_CHECK(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gpci, nullptr, &starsPipeline));
        vkDestroyShaderModule(device, starVert, nullptr);
        vkDestroyShaderModule(device, starFrag, nullptr);
#endif
#endif
    }

    void initImGui(bool isAndroid) {
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
        VkDescriptorPoolCreateInfo dpci{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpci.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        dpci.maxSets = 16;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &poolSize;
        VK_CHECK(vkCreateDescriptorPool(device, &dpci, nullptr, &imguiPool));

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        imguiContextCreated = true;
        // ImGui 1.91.8 has no IsTouchScreen config flag. SDL3 synthesizes
        // touch-as-mouse events for widgets; app finger events still drive camera orbit/pinch.
        (void)isAndroid;

        if (!ImGui_ImplSDL3_InitForVulkan(window)) {
            throw std::runtime_error("ImGui SDL3 Vulkan backend initialization failed");
        }
        imguiSdlInitialized = true;
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.Instance = instance;
        initInfo.PhysicalDevice = physicalDevice;
        initInfo.Device = device;
        initInfo.QueueFamily = queueFamily;
        initInfo.Queue = queue;
        initInfo.DescriptorPool = imguiPool;
        initInfo.RenderPass = renderPass;
        initInfo.MinImageCount = 2;
        initInfo.ImageCount = static_cast<uint32_t>(swapchainImages.size());
        initInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            throw std::runtime_error("ImGui Vulkan backend initialization failed");
        }
        imguiVulkanInitialized = true;
    }

    static void insertMemoryBarrier(VkCommandBuffer cmd, VkAccessFlags srcA, VkAccessFlags dstA,
                                    VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkMemoryBarrier mb{VK_STRUCTURE_TYPE_MEMORY_BARRIER, nullptr, srcA, dstA};
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    static void transitionImage(VkCommandBuffer cmd, VkImage img, VkImageLayout oldL, VkImageLayout newL,
                                VkAccessFlags srcA, VkAccessFlags dstA,
                                VkPipelineStageFlags srcS, VkPipelineStageFlags dstS) {
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.oldLayout = oldL; b.newLayout = newL;
        b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        b.srcAccessMask = srcA; b.dstAccessMask = dstA;
        vkCmdPipelineBarrier(cmd, srcS, dstS, 0, 0, nullptr, 0, nullptr, 1, &b);
    }
};

} // namespace aetheria
