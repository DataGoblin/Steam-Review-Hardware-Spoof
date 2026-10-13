#include <windows.h>

#include "MinHook.h"

#include "config.hpp"
#include "cpuid_hooks.hpp"
#include "dxgi_hooks.hpp"
#include "identity_hooks.hpp"
#include "log.hpp"
#include "opengl_hooks.hpp"
#include "system_hooks.hpp"
#include "vulkan_hooks.hpp"
#include "wmi_hooks.hpp"

namespace {

HMODULE g_self = nullptr;

void on_attach(HINSTANCE self) {
    g_self = self;

    if (!gpucfg::log::init()) {
        OutputDebugStringW(L"[GPUConfig] Log init failed. Continuing without file logging.\n");
    }

    GCLOG("dllmain: DLL_PROCESS_ATTACH self=%p", (void*)self);

    gpucfg::Config cfg;
    gpucfg::load_config(self, cfg);

    MH_STATUS mh = MH_Initialize();
    if (mh != MH_OK && mh != MH_ERROR_ALREADY_INITIALIZED) {
        GCLOG("minhook: MH_Initialize failed status=%d", int(mh));
        return;
    }
    GCLOG("minhook: MH_Initialize OK");

    bool dxgi_ok     = gpucfg::dxgi_hooks::install_hooks();
    bool vulkan_ok   = gpucfg::vulkan_hooks::install_hooks();
    bool opengl_ok   = gpucfg::opengl_hooks::install_hooks();
    bool identity_ok = gpucfg::identity_hooks::install_hooks();
    bool wmi_ok      = gpucfg::wmi_hooks::install_hooks();
    bool system_ok   = gpucfg::system_hooks::install_hooks();
    bool cpuid_ok    = gpucfg::cpuid_hooks::install_hooks();

    GCLOG("hooks installed: dxgi=%d vulkan=%d opengl=%d identity=%d wmi=%d system=%d cpuid=%d",
           int(dxgi_ok), int(vulkan_ok), int(opengl_ok),
           int(identity_ok), int(wmi_ok), int(system_ok), int(cpuid_ok));

    mh = MH_EnableHook(MH_ALL_HOOKS);
    if (mh != MH_OK) {
        GCLOG("minhook: MH_EnableHook(MH_ALL_HOOKS) failed status=%d", int(mh));
    } else {
        GCLOG("minhook: MH_EnableHook(MH_ALL_HOOKS) OK");
    }
}

void on_detach() {
    GCLOG("dllmain: DLL_PROCESS_DETACH");
    MH_DisableHook(MH_ALL_HOOKS);
    MH_Uninitialize();
    gpucfg::log::shutdown();
}

}  // namespace

BOOL APIENTRY DllMain(HINSTANCE self, DWORD reason, LPVOID /*reserved*/) {
    switch (reason) {
        case DLL_PROCESS_ATTACH:
            DisableThreadLibraryCalls(self);
            __try {
                on_attach(self);
            } __except(EXCEPTION_EXECUTE_HANDLER) {
                OutputDebugStringW(L"[GPUConfig] Unhandled SEH in on_attach\n");
            }
            break;
        case DLL_PROCESS_DETACH:
            __try { on_detach(); }
            __except(EXCEPTION_EXECUTE_HANDLER) {}
            break;
        default:
            break;
    }
    return TRUE;
}
