#include "identity_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>
#include <setupapi.h>

#include "MinHook.h"

#include <algorithm>
#include <cwchar>
#include <cstring>

namespace gpucfg::identity_hooks {

namespace {

using PFN_SetupDiGetDeviceRegistryPropertyW = BOOL(WINAPI*)(
    HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
using PFN_SetupDiGetDeviceRegistryPropertyA = BOOL(WINAPI*)(
    HDEVINFO, PSP_DEVINFO_DATA, DWORD, PDWORD, PBYTE, DWORD, PDWORD);
using PFN_EnumDisplayDevicesW = BOOL(WINAPI*)(LPCWSTR, DWORD, PDISPLAY_DEVICEW, DWORD);
using PFN_EnumDisplayDevicesA = BOOL(WINAPI*)(LPCSTR,  DWORD, PDISPLAY_DEVICEA, DWORD);

PFN_SetupDiGetDeviceRegistryPropertyW g_real_SetupDiGetDeviceRegistryPropertyW = nullptr;
PFN_SetupDiGetDeviceRegistryPropertyA g_real_SetupDiGetDeviceRegistryPropertyA = nullptr;
PFN_EnumDisplayDevicesW               g_real_EnumDisplayDevicesW               = nullptr;
PFN_EnumDisplayDevicesA               g_real_EnumDisplayDevicesA               = nullptr;

const char* prop_name(DWORD p) {
    switch (p) {
        case SPDRP_DEVICEDESC:          return "DEVICEDESC";
        case SPDRP_HARDWAREID:          return "HARDWAREID";
        case SPDRP_COMPATIBLEIDS:       return "COMPATIBLEIDS";
        case SPDRP_SERVICE:             return "SERVICE";
        case SPDRP_CLASS:               return "CLASS";
        case SPDRP_CLASSGUID:           return "CLASSGUID";
        case SPDRP_DRIVER:              return "DRIVER";
        case SPDRP_CONFIGFLAGS:         return "CONFIGFLAGS";
        case SPDRP_MFG:                 return "MFG";
        case SPDRP_FRIENDLYNAME:        return "FRIENDLYNAME";
        case SPDRP_LOCATION_INFORMATION:return "LOCATION_INFORMATION";
        default:                        return "<other>";
    }
}

bool looks_like_nvidia_gpu(const wchar_t* s) {
    if (!s) return false;
    return std::wcsstr(s, L"NVIDIA")  != nullptr
        || std::wcsstr(s, L"GeForce") != nullptr
        || std::wcsstr(s, L"RTX")     != nullptr
        || std::wcsstr(s, L"Quadro")  != nullptr;
}
bool looks_like_nvidia_gpu_a(const char* s) {
    if (!s) return false;
    return std::strstr(s, "NVIDIA")  != nullptr
        || std::strstr(s, "GeForce") != nullptr
        || std::strstr(s, "RTX")     != nullptr
        || std::strstr(s, "Quadro")  != nullptr;
}

BOOL WINAPI hk_SetupDiGetDeviceRegistryPropertyW(
    HDEVINFO dis, PSP_DEVINFO_DATA did, DWORD prop, PDWORD type,
    PBYTE buf, DWORD bufsize, PDWORD reqsize) {
    BOOL ok = g_real_SetupDiGetDeviceRegistryPropertyW
        ? g_real_SetupDiGetDeviceRegistryPropertyW(dis, did, prop, type, buf, bufsize, reqsize)
        : FALSE;

    if (ok && buf && prop == SPDRP_DEVICEDESC) {
        const wchar_t* s = reinterpret_cast<const wchar_t*>(buf);
        GCLOG("setupapi: SetupDiGetDeviceRegistryPropertyW(%s) real=\"%ls\"", prop_name(prop), s);
        if (looks_like_nvidia_gpu(s) && gpucfg::config().mode == gpucfg::Mode::Custom) {
            const auto& cfg = gpucfg::config();
            DWORD max_wchars = bufsize / sizeof(wchar_t);
            if (max_wchars > 0) {
                size_t n = (std::min<size_t>)(cfg.adapter_description.size(), max_wchars - 1);
                std::memset(buf, 0, bufsize);
                std::memcpy(buf, cfg.adapter_description.data(), n * sizeof(wchar_t));
                if (reqsize) *reqsize = static_cast<DWORD>((n + 1) * sizeof(wchar_t));
                GCLOG("setupapi: returned custom DEVICEDESC=\"%ls\"",
                      reinterpret_cast<wchar_t*>(buf));
            }
        }
    }
    return ok;
}

BOOL WINAPI hk_SetupDiGetDeviceRegistryPropertyA(
    HDEVINFO dis, PSP_DEVINFO_DATA did, DWORD prop, PDWORD type,
    PBYTE buf, DWORD bufsize, PDWORD reqsize) {
    BOOL ok = g_real_SetupDiGetDeviceRegistryPropertyA
        ? g_real_SetupDiGetDeviceRegistryPropertyA(dis, did, prop, type, buf, bufsize, reqsize)
        : FALSE;

    if (ok && buf && prop == SPDRP_DEVICEDESC) {
        const char* s = reinterpret_cast<const char*>(buf);
        GCLOG("setupapi: SetupDiGetDeviceRegistryPropertyA(%s) real=\"%s\"", prop_name(prop), s);
        if (looks_like_nvidia_gpu_a(s) && gpucfg::config().mode == gpucfg::Mode::Custom) {
            const auto& cfg = gpucfg::config();
            if (bufsize > 0) {
                size_t n = (std::min<size_t>)(cfg.adapter_description_utf8.size(),
                                              static_cast<size_t>(bufsize) - 1);
                std::memset(buf, 0, bufsize);
                std::memcpy(buf, cfg.adapter_description_utf8.data(), n);
                if (reqsize) *reqsize = static_cast<DWORD>(n + 1);
                GCLOG("setupapi: returned custom DEVICEDESC=\"%s\"",
                      reinterpret_cast<char*>(buf));
            }
        }
    }
    return ok;
}

BOOL WINAPI hk_EnumDisplayDevicesW(LPCWSTR dev, DWORD idx, PDISPLAY_DEVICEW out, DWORD flags) {
    BOOL ok = g_real_EnumDisplayDevicesW ? g_real_EnumDisplayDevicesW(dev, idx, out, flags) : FALSE;
    if (ok && out) {
        GCLOG("gdi: EnumDisplayDevicesW idx=%lu DeviceString=\"%ls\" DeviceID=\"%ls\"",
              idx, out->DeviceString, out->DeviceID);
        if (looks_like_nvidia_gpu(out->DeviceString) &&
            gpucfg::config().mode == gpucfg::Mode::Custom) {
            const auto& cfg = gpucfg::config();
            size_t cap = sizeof(out->DeviceString) / sizeof(wchar_t);
            size_t n = (std::min<size_t>)(cfg.adapter_description.size(), cap - 1);
            std::memset(out->DeviceString, 0, sizeof(out->DeviceString));
            std::memcpy(out->DeviceString, cfg.adapter_description.data(), n * sizeof(wchar_t));
            GCLOG("gdi: returned custom DeviceString=\"%ls\"", out->DeviceString);
        }
    }
    return ok;
}

BOOL WINAPI hk_EnumDisplayDevicesA(LPCSTR dev, DWORD idx, PDISPLAY_DEVICEA out, DWORD flags) {
    BOOL ok = g_real_EnumDisplayDevicesA ? g_real_EnumDisplayDevicesA(dev, idx, out, flags) : FALSE;
    if (ok && out) {
        GCLOG("gdi: EnumDisplayDevicesA idx=%lu DeviceString=\"%s\" DeviceID=\"%s\"",
              idx, out->DeviceString, out->DeviceID);
        if (looks_like_nvidia_gpu_a(out->DeviceString) &&
            gpucfg::config().mode == gpucfg::Mode::Custom) {
            const auto& cfg = gpucfg::config();
            size_t cap = sizeof(out->DeviceString);
            size_t n = (std::min<size_t>)(cfg.adapter_description_utf8.size(), cap - 1);
            std::memset(out->DeviceString, 0, sizeof(out->DeviceString));
            std::memcpy(out->DeviceString, cfg.adapter_description_utf8.data(), n);
            GCLOG("gdi: returned custom DeviceString=\"%s\"", out->DeviceString);
        }
    }
    return ok;
}

}  // namespace

bool install_hooks() {
    bool any = false;

    HMODULE setupapi = LoadLibraryW(L"setupapi.dll");
    if (setupapi) {
        void* w = reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiGetDeviceRegistryPropertyW"));
        if (w) {
            MH_STATUS st = MH_CreateHook(w,
                reinterpret_cast<void*>(&hk_SetupDiGetDeviceRegistryPropertyW),
                reinterpret_cast<void**>(&g_real_SetupDiGetDeviceRegistryPropertyW));
            if (st == MH_OK) { GCLOG("setupapi: hooked SetupDiGetDeviceRegistryPropertyW @ %p", w); any = true; }
            else GCLOG("setupapi: MH_CreateHook(W) failed status=%d", int(st));
        }
        void* a = reinterpret_cast<void*>(GetProcAddress(setupapi, "SetupDiGetDeviceRegistryPropertyA"));
        if (a) {
            MH_STATUS st = MH_CreateHook(a,
                reinterpret_cast<void*>(&hk_SetupDiGetDeviceRegistryPropertyA),
                reinterpret_cast<void**>(&g_real_SetupDiGetDeviceRegistryPropertyA));
            if (st == MH_OK) { GCLOG("setupapi: hooked SetupDiGetDeviceRegistryPropertyA @ %p", a); any = true; }
            else GCLOG("setupapi: MH_CreateHook(A) failed status=%d", int(st));
        }
    } else {
        GCLOG("setupapi: LoadLibrary(setupapi.dll) failed GLE=%lu", GetLastError());
    }

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        void* w = reinterpret_cast<void*>(GetProcAddress(user32, "EnumDisplayDevicesW"));
        if (w) {
            MH_STATUS st = MH_CreateHook(w,
                reinterpret_cast<void*>(&hk_EnumDisplayDevicesW),
                reinterpret_cast<void**>(&g_real_EnumDisplayDevicesW));
            if (st == MH_OK) { GCLOG("gdi: hooked EnumDisplayDevicesW @ %p", w); any = true; }
            else GCLOG("gdi: MH_CreateHook(W) failed status=%d", int(st));
        }
        void* a = reinterpret_cast<void*>(GetProcAddress(user32, "EnumDisplayDevicesA"));
        if (a) {
            MH_STATUS st = MH_CreateHook(a,
                reinterpret_cast<void*>(&hk_EnumDisplayDevicesA),
                reinterpret_cast<void**>(&g_real_EnumDisplayDevicesA));
            if (st == MH_OK) { GCLOG("gdi: hooked EnumDisplayDevicesA @ %p", a); any = true; }
            else GCLOG("gdi: MH_CreateHook(A) failed status=%d", int(st));
        }
    }

    return any;
}

}  // namespace gpucfg::identity_hooks
