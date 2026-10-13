#include "dxgi_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include "MinHook.h"

#include <algorithm>
#include <mutex>
#include <unordered_map>

using Microsoft::WRL::ComPtr;

namespace gpucfg::dxgi_hooks {

namespace {

using PFN_CreateDXGIFactory  = HRESULT (WINAPI*)(REFIID, void**);
using PFN_CreateDXGIFactory1 = HRESULT (WINAPI*)(REFIID, void**);
using PFN_CreateDXGIFactory2 = HRESULT (WINAPI*)(UINT, REFIID, void**);

PFN_CreateDXGIFactory  g_real_CreateDXGIFactory  = nullptr;
PFN_CreateDXGIFactory1 g_real_CreateDXGIFactory1 = nullptr;
PFN_CreateDXGIFactory2 g_real_CreateDXGIFactory2 = nullptr;

using PFN_GetDesc  = HRESULT (STDMETHODCALLTYPE*)(IDXGIAdapter*,  DXGI_ADAPTER_DESC*);
using PFN_GetDesc1 = HRESULT (STDMETHODCALLTYPE*)(IDXGIAdapter1*, DXGI_ADAPTER_DESC1*);
using PFN_GetDesc2 = HRESULT (STDMETHODCALLTYPE*)(IDXGIAdapter2*, DXGI_ADAPTER_DESC2*);
using PFN_GetDesc3 = HRESULT (STDMETHODCALLTYPE*)(IDXGIAdapter4*, DXGI_ADAPTER_DESC3*);

// Vtable slot indices. IUnknown=3, IDXGIObject adds 4, IDXGIAdapter adds 3
// (EnumOutputs, GetDesc, CheckInterfaceSupport). Adapter1/2/3/4 add further.
constexpr size_t SLOT_GETDESC  = 8;
constexpr size_t SLOT_GETDESC1 = 10;
constexpr size_t SLOT_GETDESC2 = 11;
constexpr size_t SLOT_GETDESC3 = 18;

struct VtableRecord {
    PFN_GetDesc  get_desc  = nullptr;
    PFN_GetDesc1 get_desc1 = nullptr;
    PFN_GetDesc2 get_desc2 = nullptr;
    PFN_GetDesc3 get_desc3 = nullptr;
};

std::mutex g_vtable_mutex;
std::unordered_map<void**, VtableRecord> g_vtable_records;

bool patch_vtable_slot(void** vtable, size_t slot, void* new_fn, void** out_original) {
    DWORD old_prot = 0;
    if (!VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old_prot)) {
        GCLOG("dxgi: VirtualProtect(rw) slot %zu failed GLE=%lu", slot, GetLastError());
        return false;
    }
    *out_original = vtable[slot];
    vtable[slot]  = new_fn;
    VirtualProtect(&vtable[slot], sizeof(void*), old_prot, &old_prot);
    return true;
}

void apply_custom_identity(WCHAR desc[128], UINT& vid, UINT& did, UINT& ssid, SIZE_T& vram_bytes) {
    const auto& cfg = gpucfg::config();
    if (cfg.mode != gpucfg::Mode::Custom) return;

    ZeroMemory(desc, 128 * sizeof(WCHAR));
    size_t n = std::min<size_t>(cfg.adapter_description.size(), 127);
    memcpy(desc, cfg.adapter_description.c_str(), n * sizeof(WCHAR));

    vid        = cfg.vendor_id;
    did        = cfg.device_id;
    ssid       = cfg.subsys_id;
    vram_bytes = static_cast<SIZE_T>(cfg.vram_gb) * (1ULL << 30);
}

PFN_GetDesc  lookup_real_get_desc (void** vt) { std::lock_guard lk(g_vtable_mutex); auto it = g_vtable_records.find(vt); return it != g_vtable_records.end() ? it->second.get_desc  : nullptr; }
PFN_GetDesc1 lookup_real_get_desc1(void** vt) { std::lock_guard lk(g_vtable_mutex); auto it = g_vtable_records.find(vt); return it != g_vtable_records.end() ? it->second.get_desc1 : nullptr; }
PFN_GetDesc2 lookup_real_get_desc2(void** vt) { std::lock_guard lk(g_vtable_mutex); auto it = g_vtable_records.find(vt); return it != g_vtable_records.end() ? it->second.get_desc2 : nullptr; }
PFN_GetDesc3 lookup_real_get_desc3(void** vt) { std::lock_guard lk(g_vtable_mutex); auto it = g_vtable_records.find(vt); return it != g_vtable_records.end() ? it->second.get_desc3 : nullptr; }

HRESULT STDMETHODCALLTYPE hk_GetDesc(IDXGIAdapter* self, DXGI_ADAPTER_DESC* out) {
    void** vt = *reinterpret_cast<void***>(self);
    PFN_GetDesc real = lookup_real_get_desc(vt);
    if (!real) return E_FAIL;
    HRESULT hr = real(self, out);
    if (SUCCEEDED(hr) && out) {
        GCLOG("dxgi: GetDesc real vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
               out->VendorId, out->DeviceId, out->SubSysId,
               static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
               out->Description);
        apply_custom_identity(out->Description, out->VendorId, out->DeviceId,
                              out->SubSysId, out->DedicatedVideoMemory);
        if (gpucfg::config().mode == gpucfg::Mode::Custom) {
            GCLOG("dxgi: GetDesc returned custom vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
                   out->VendorId, out->DeviceId, out->SubSysId,
                   static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
                   out->Description);
        }
    } else if (FAILED(hr)) {
        GCLOG("dxgi: GetDesc hr=0x%08lX", hr);
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_GetDesc1(IDXGIAdapter1* self, DXGI_ADAPTER_DESC1* out) {
    void** vt = *reinterpret_cast<void***>(self);
    PFN_GetDesc1 real = lookup_real_get_desc1(vt);
    if (!real) return E_FAIL;
    HRESULT hr = real(self, out);
    if (SUCCEEDED(hr) && out) {
        GCLOG("dxgi: GetDesc1 real vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu flags=0x%X desc=\"%ls\"",
               out->VendorId, out->DeviceId, out->SubSysId,
               static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
               out->Flags, out->Description);
        apply_custom_identity(out->Description, out->VendorId, out->DeviceId,
                              out->SubSysId, out->DedicatedVideoMemory);
        if (gpucfg::config().mode == gpucfg::Mode::Custom) {
            GCLOG("dxgi: GetDesc1 returned custom vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
                   out->VendorId, out->DeviceId, out->SubSysId,
                   static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
                   out->Description);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_GetDesc2(IDXGIAdapter2* self, DXGI_ADAPTER_DESC2* out) {
    void** vt = *reinterpret_cast<void***>(self);
    PFN_GetDesc2 real = lookup_real_get_desc2(vt);
    if (!real) return E_FAIL;
    HRESULT hr = real(self, out);
    if (SUCCEEDED(hr) && out) {
        GCLOG("dxgi: GetDesc2 real vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
               out->VendorId, out->DeviceId, out->SubSysId,
               static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
               out->Description);
        apply_custom_identity(out->Description, out->VendorId, out->DeviceId,
                              out->SubSysId, out->DedicatedVideoMemory);
        if (gpucfg::config().mode == gpucfg::Mode::Custom) {
            GCLOG("dxgi: GetDesc2 returned custom vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
                   out->VendorId, out->DeviceId, out->SubSysId,
                   static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
                   out->Description);
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE hk_GetDesc3(IDXGIAdapter4* self, DXGI_ADAPTER_DESC3* out) {
    void** vt = *reinterpret_cast<void***>(self);
    PFN_GetDesc3 real = lookup_real_get_desc3(vt);
    if (!real) return E_FAIL;
    HRESULT hr = real(self, out);
    if (SUCCEEDED(hr) && out) {
        GCLOG("dxgi: GetDesc3 real vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
               out->VendorId, out->DeviceId, out->SubSysId,
               static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
               out->Description);
        apply_custom_identity(out->Description, out->VendorId, out->DeviceId,
                              out->SubSysId, out->DedicatedVideoMemory);
        if (gpucfg::config().mode == gpucfg::Mode::Custom) {
            GCLOG("dxgi: GetDesc3 returned custom vid=0x%04X did=0x%04X ssid=0x%08X vram_mb=%llu desc=\"%ls\"",
                   out->VendorId, out->DeviceId, out->SubSysId,
                   static_cast<unsigned long long>(out->DedicatedVideoMemory / (1024ull * 1024ull)),
                   out->Description);
        }
    }
    return hr;
}

void patch_adapter_vtable_once(IDXGIAdapter* adapter) {
    if (!adapter) return;
    void** vt = *reinterpret_cast<void***>(adapter);

    {
        std::lock_guard lk(g_vtable_mutex);
        if (g_vtable_records.count(vt)) return;
    }

    VtableRecord rec;
    void* orig = nullptr;

    if (patch_vtable_slot(vt, SLOT_GETDESC, reinterpret_cast<void*>(&hk_GetDesc), &orig)) {
        rec.get_desc = reinterpret_cast<PFN_GetDesc>(orig);
        GCLOG("dxgi: patched vtable %p slot %zu GetDesc (real=%p)", (void*)vt, SLOT_GETDESC, orig);
    }

    ComPtr<IDXGIAdapter1> a1;
    if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&a1)))) {
        if (patch_vtable_slot(vt, SLOT_GETDESC1, reinterpret_cast<void*>(&hk_GetDesc1), &orig)) {
            rec.get_desc1 = reinterpret_cast<PFN_GetDesc1>(orig);
            GCLOG("dxgi: patched slot %zu GetDesc1 (real=%p)", SLOT_GETDESC1, orig);
        }
    }
    ComPtr<IDXGIAdapter2> a2;
    if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&a2)))) {
        if (patch_vtable_slot(vt, SLOT_GETDESC2, reinterpret_cast<void*>(&hk_GetDesc2), &orig)) {
            rec.get_desc2 = reinterpret_cast<PFN_GetDesc2>(orig);
            GCLOG("dxgi: patched slot %zu GetDesc2 (real=%p)", SLOT_GETDESC2, orig);
        }
    }
    ComPtr<IDXGIAdapter4> a4;
    if (SUCCEEDED(adapter->QueryInterface(IID_PPV_ARGS(&a4)))) {
        if (patch_vtable_slot(vt, SLOT_GETDESC3, reinterpret_cast<void*>(&hk_GetDesc3), &orig)) {
            rec.get_desc3 = reinterpret_cast<PFN_GetDesc3>(orig);
            GCLOG("dxgi: patched slot %zu GetDesc3 (real=%p)", SLOT_GETDESC3, orig);
        }
    }

    {
        std::lock_guard lk(g_vtable_mutex);
        g_vtable_records[vt] = rec;
    }
}

void patch_factory_adapters(IDXGIFactory* factory) {
    if (!factory) return;
    ComPtr<IDXGIFactory1> f1;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&f1)))) {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter1> a;
            if (f1->EnumAdapters1(i, &a) == DXGI_ERROR_NOT_FOUND) break;
            patch_adapter_vtable_once(a.Get());
        }
    } else {
        for (UINT i = 0;; ++i) {
            ComPtr<IDXGIAdapter> a;
            if (factory->EnumAdapters(i, &a) == DXGI_ERROR_NOT_FOUND) break;
            patch_adapter_vtable_once(a.Get());
        }
    }
}

HRESULT WINAPI hk_CreateDXGIFactory(REFIID riid, void** out) {
    HRESULT hr = g_real_CreateDXGIFactory ? g_real_CreateDXGIFactory(riid, out) : E_FAIL;
    GCLOG("dxgi: CreateDXGIFactory  hr=0x%08lX factory=%p", hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out) patch_factory_adapters(static_cast<IDXGIFactory*>(*out));
    return hr;
}

HRESULT WINAPI hk_CreateDXGIFactory1(REFIID riid, void** out) {
    HRESULT hr = g_real_CreateDXGIFactory1 ? g_real_CreateDXGIFactory1(riid, out) : E_FAIL;
    GCLOG("dxgi: CreateDXGIFactory1 hr=0x%08lX factory=%p", hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out) patch_factory_adapters(static_cast<IDXGIFactory*>(*out));
    return hr;
}

HRESULT WINAPI hk_CreateDXGIFactory2(UINT flags, REFIID riid, void** out) {
    HRESULT hr = g_real_CreateDXGIFactory2 ? g_real_CreateDXGIFactory2(flags, riid, out) : E_FAIL;
    GCLOG("dxgi: CreateDXGIFactory2 flags=0x%X hr=0x%08lX factory=%p", flags, hr, out ? *out : nullptr);
    if (SUCCEEDED(hr) && out && *out) patch_factory_adapters(static_cast<IDXGIFactory*>(*out));
    return hr;
}

}  // namespace

bool install_hooks() {
    HMODULE dxgi = LoadLibraryW(L"dxgi.dll");
    if (!dxgi) {
        GCLOG("dxgi: LoadLibrary(dxgi.dll) failed GLE=%lu", GetLastError());
        return false;
    }

    struct Spec { const char* name; void** real; void* detour; };
    Spec specs[] = {
        {"CreateDXGIFactory",  reinterpret_cast<void**>(&g_real_CreateDXGIFactory),  reinterpret_cast<void*>(&hk_CreateDXGIFactory) },
        {"CreateDXGIFactory1", reinterpret_cast<void**>(&g_real_CreateDXGIFactory1), reinterpret_cast<void*>(&hk_CreateDXGIFactory1)},
        {"CreateDXGIFactory2", reinterpret_cast<void**>(&g_real_CreateDXGIFactory2), reinterpret_cast<void*>(&hk_CreateDXGIFactory2)},
    };

    bool any = false;
    for (const auto& s : specs) {
        void* target = reinterpret_cast<void*>(GetProcAddress(dxgi, s.name));
        if (!target) {
            GCLOG("dxgi: %s not exported by dxgi.dll", s.name);
            continue;
        }
        MH_STATUS st = MH_CreateHook(target, s.detour, s.real);
        if (st != MH_OK) {
            GCLOG("dxgi: MH_CreateHook(%s) failed status=%d", s.name, static_cast<int>(st));
            continue;
        }
        GCLOG("dxgi: hooked dxgi.dll!%s @ %p", s.name, target);
        any = true;
    }
    return any;
}

}  // namespace gpucfg::dxgi_hooks
