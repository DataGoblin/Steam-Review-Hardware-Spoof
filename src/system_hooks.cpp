#include "system_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>

#include "MinHook.h"

namespace gpucfg::system_hooks {

namespace {

using PFN_GlobalMemoryStatusEx             = BOOL(WINAPI*)(LPMEMORYSTATUSEX);
using PFN_GetPhysicallyInstalledSystemMem  = BOOL(WINAPI*)(PULONGLONG);

PFN_GlobalMemoryStatusEx             g_real_GlobalMemoryStatusEx             = nullptr;
PFN_GetPhysicallyInstalledSystemMem  g_real_GetPhysicallyInstalledSystemMem  = nullptr;

BOOL WINAPI hk_GlobalMemoryStatusEx(LPMEMORYSTATUSEX p) {
    BOOL ok = g_real_GlobalMemoryStatusEx ? g_real_GlobalMemoryStatusEx(p) : FALSE;
    if (ok && p && gpucfg::config().mode == gpucfg::Mode::Custom) {
        uint64_t real_bytes = p->ullTotalPhys;
        uint64_t custom_bytes = static_cast<uint64_t>(gpucfg::config().ram_gb) * (1ULL << 30);
        p->ullTotalPhys     = custom_bytes;
        p->ullAvailPhys     = custom_bytes - (real_bytes - p->ullAvailPhys);
        if (static_cast<int64_t>(p->ullAvailPhys) < 0) p->ullAvailPhys = custom_bytes / 2;
        GCLOG("sys: GlobalMemoryStatusEx real_total=%llu MB -> custom_total=%llu MB",
              (unsigned long long)(real_bytes / (1024ull*1024ull)),
              (unsigned long long)(custom_bytes / (1024ull*1024ull)));
    }
    return ok;
}

BOOL WINAPI hk_GetPhysicallyInstalledSystemMemory(PULONGLONG p) {
    BOOL ok = g_real_GetPhysicallyInstalledSystemMem
        ? g_real_GetPhysicallyInstalledSystemMem(p) : FALSE;
    if (ok && p && gpucfg::config().mode == gpucfg::Mode::Custom) {
        ULONGLONG custom_kb = static_cast<ULONGLONG>(gpucfg::config().ram_gb) * 1024ULL * 1024ULL;
        GCLOG("sys: GetPhysicallyInstalledSystemMemory real=%llu KB -> custom=%llu KB",
              (unsigned long long)*p, (unsigned long long)custom_kb);
        *p = custom_kb;
    }
    return ok;
}

}  // namespace

bool install_hooks() {
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) k32 = LoadLibraryW(L"kernel32.dll");
    if (!k32) { GCLOG("sys: kernel32.dll not available"); return false; }

    bool any = false;

    void* t1 = reinterpret_cast<void*>(GetProcAddress(k32, "GlobalMemoryStatusEx"));
    if (t1) {
        MH_STATUS st = MH_CreateHook(t1,
            reinterpret_cast<void*>(&hk_GlobalMemoryStatusEx),
            reinterpret_cast<void**>(&g_real_GlobalMemoryStatusEx));
        if (st == MH_OK) { GCLOG("sys: hooked kernel32!GlobalMemoryStatusEx @ %p", t1); any = true; }
        else GCLOG("sys: MH_CreateHook(GlobalMemoryStatusEx) status=%d", int(st));
    }

    void* t2 = reinterpret_cast<void*>(GetProcAddress(k32, "GetPhysicallyInstalledSystemMemory"));
    if (t2) {
        MH_STATUS st = MH_CreateHook(t2,
            reinterpret_cast<void*>(&hk_GetPhysicallyInstalledSystemMemory),
            reinterpret_cast<void**>(&g_real_GetPhysicallyInstalledSystemMem));
        if (st == MH_OK) { GCLOG("sys: hooked kernel32!GetPhysicallyInstalledSystemMemory @ %p", t2); any = true; }
        else GCLOG("sys: MH_CreateHook(GetPhysicallyInstalledSystemMemory) status=%d", int(st));
    }

    return any;
}

}  // namespace gpucfg::system_hooks
