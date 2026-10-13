#include "vulkan_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

#include "MinHook.h"

#include <cstring>

namespace gpucfg::vulkan_hooks {

namespace {

using PFN_vkGetInstanceProcAddr_t = PFN_vkVoidFunction (VKAPI_PTR*)(VkInstance, const char*);

PFN_vkGetInstanceProcAddr_t g_real_vkGetInstanceProcAddr = nullptr;

PFN_vkGetPhysicalDeviceProperties         g_real_GetPhysicalDeviceProperties         = nullptr;
PFN_vkGetPhysicalDeviceProperties2        g_real_GetPhysicalDeviceProperties2        = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties   g_real_GetPhysicalDeviceMemoryProperties   = nullptr;
PFN_vkGetPhysicalDeviceMemoryProperties2  g_real_GetPhysicalDeviceMemoryProperties2  = nullptr;

void log_props(const VkPhysicalDeviceProperties& p) {
    GCLOG("vk: GetPhysicalDeviceProperties vid=0x%04X did=0x%04X type=%d api=%u.%u.%u name=\"%s\"",
           p.vendorID, p.deviceID, int(p.deviceType),
           VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion),
           p.deviceName);
}

void log_mem(const VkPhysicalDeviceMemoryProperties& m, const char* tag) {
    uint64_t largest_local = 0;
    for (uint32_t i = 0; i < m.memoryHeapCount; ++i) {
        if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (m.memoryHeaps[i].size > largest_local) largest_local = m.memoryHeaps[i].size;
        }
    }
    GCLOG("vk: GetPhysicalDeviceMemoryProperties %s heaps=%u largest_local=%llu MB",
           tag, m.memoryHeapCount, (unsigned long long)(largest_local / (1024ull * 1024ull)));
}

void apply_custom_props(VkPhysicalDeviceProperties& p) {
    const auto& cfg = gpucfg::config();
    if (cfg.mode != gpucfg::Mode::Custom) return;
    p.vendorID = cfg.vendor_id;
    p.deviceID = cfg.device_id;
    std::memset(p.deviceName, 0, sizeof(p.deviceName));
    size_t n = (std::min<size_t>)(cfg.adapter_description_utf8.size(),
                                  VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    std::memcpy(p.deviceName, cfg.adapter_description_utf8.data(), n);
}

void apply_custom_mem(VkPhysicalDeviceMemoryProperties& m) {
    const auto& cfg = gpucfg::config();
    if (cfg.mode != gpucfg::Mode::Custom) return;

    uint32_t target = UINT32_MAX;
    VkDeviceSize best = 0;
    for (uint32_t i = 0; i < m.memoryHeapCount; ++i) {
        if (m.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
            if (m.memoryHeaps[i].size >= best) { best = m.memoryHeaps[i].size; target = i; }
        }
    }
    if (target != UINT32_MAX) {
        m.memoryHeaps[target].size = static_cast<VkDeviceSize>(cfg.vram_gb) * (1ULL << 30);
    }
}

VKAPI_ATTR void VKAPI_CALL hk_GetPhysicalDeviceProperties(
    VkPhysicalDevice pd, VkPhysicalDeviceProperties* p) {
    if (g_real_GetPhysicalDeviceProperties) g_real_GetPhysicalDeviceProperties(pd, p);
    if (!p) return;
    log_props(*p);
    apply_custom_props(*p);
    if (gpucfg::config().mode == gpucfg::Mode::Custom) {
        GCLOG("vk: GetPhysicalDeviceProperties returned custom vid=0x%04X did=0x%04X name=\"%s\"",
               p->vendorID, p->deviceID, p->deviceName);
    }
}

VKAPI_ATTR void VKAPI_CALL hk_GetPhysicalDeviceProperties2(
    VkPhysicalDevice pd, VkPhysicalDeviceProperties2* p) {
    if (g_real_GetPhysicalDeviceProperties2) g_real_GetPhysicalDeviceProperties2(pd, p);
    if (!p) return;
    log_props(p->properties);
    apply_custom_props(p->properties);
    if (gpucfg::config().mode == gpucfg::Mode::Custom) {
        GCLOG("vk: GetPhysicalDeviceProperties2 returned custom vid=0x%04X did=0x%04X name=\"%s\"",
               p->properties.vendorID, p->properties.deviceID, p->properties.deviceName);
    }
}

VKAPI_ATTR void VKAPI_CALL hk_GetPhysicalDeviceMemoryProperties(
    VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties* m) {
    if (g_real_GetPhysicalDeviceMemoryProperties) g_real_GetPhysicalDeviceMemoryProperties(pd, m);
    if (!m) return;
    log_mem(*m, "real");
    apply_custom_mem(*m);
    if (gpucfg::config().mode == gpucfg::Mode::Custom) log_mem(*m, "custom");
}

VKAPI_ATTR void VKAPI_CALL hk_GetPhysicalDeviceMemoryProperties2(
    VkPhysicalDevice pd, VkPhysicalDeviceMemoryProperties2* m) {
    if (g_real_GetPhysicalDeviceMemoryProperties2) g_real_GetPhysicalDeviceMemoryProperties2(pd, m);
    if (!m) return;
    log_mem(m->memoryProperties, "real");
    apply_custom_mem(m->memoryProperties);
    if (gpucfg::config().mode == gpucfg::Mode::Custom) log_mem(m->memoryProperties, "custom");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hk_vkGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (!name) return nullptr;

    // Chain to real to capture the actual loader/ICD pointers for any we don't already know.
    PFN_vkVoidFunction real_fn = g_real_vkGetInstanceProcAddr
        ? g_real_vkGetInstanceProcAddr(instance, name)
        : nullptr;

    auto try_swap = [&](const char* match, auto*& slot, void* detour) -> PFN_vkVoidFunction {
        if (std::strcmp(name, match) != 0) return {};
        if (!slot) slot = reinterpret_cast<std::decay_t<decltype(slot)>>(real_fn);
        return reinterpret_cast<PFN_vkVoidFunction>(detour);
    };

    if (auto p = try_swap("vkGetPhysicalDeviceProperties",
                          g_real_GetPhysicalDeviceProperties,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceProperties))) return p;
    if (auto p = try_swap("vkGetPhysicalDeviceProperties2",
                          g_real_GetPhysicalDeviceProperties2,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceProperties2))) return p;
    if (auto p = try_swap("vkGetPhysicalDeviceProperties2KHR",
                          g_real_GetPhysicalDeviceProperties2,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceProperties2))) return p;
    if (auto p = try_swap("vkGetPhysicalDeviceMemoryProperties",
                          g_real_GetPhysicalDeviceMemoryProperties,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceMemoryProperties))) return p;
    if (auto p = try_swap("vkGetPhysicalDeviceMemoryProperties2",
                          g_real_GetPhysicalDeviceMemoryProperties2,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceMemoryProperties2))) return p;
    if (auto p = try_swap("vkGetPhysicalDeviceMemoryProperties2KHR",
                          g_real_GetPhysicalDeviceMemoryProperties2,
                          reinterpret_cast<void*>(&hk_GetPhysicalDeviceMemoryProperties2))) return p;

    return real_fn;
}

}  // namespace

bool install_hooks() {
    HMODULE vk = LoadLibraryW(L"vulkan-1.dll");
    if (!vk) {
        GCLOG("vk: LoadLibrary(vulkan-1.dll) failed GLE=%lu (no runtime installed?)", GetLastError());
        return false;
    }

    void* target_gipa = reinterpret_cast<void*>(GetProcAddress(vk, "vkGetInstanceProcAddr"));
    if (!target_gipa) {
        GCLOG("vk: vulkan-1.dll!vkGetInstanceProcAddr not found");
        return false;
    }

    MH_STATUS st = MH_CreateHook(
        target_gipa,
        reinterpret_cast<void*>(&hk_vkGetInstanceProcAddr),
        reinterpret_cast<void**>(&g_real_vkGetInstanceProcAddr));
    if (st != MH_OK) {
        GCLOG("vk: MH_CreateHook(vkGetInstanceProcAddr) failed status=%d", int(st));
        return false;
    }
    GCLOG("vk: hooked vulkan-1.dll!vkGetInstanceProcAddr @ %p", target_gipa);
    return true;
}

}  // namespace gpucfg::vulkan_hooks
