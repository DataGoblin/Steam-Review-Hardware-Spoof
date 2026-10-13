#include "wmi_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>
#include <oleauto.h>
#include <wbemidl.h>

#include "MinHook.h"

#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace gpucfg::wmi_hooks {

namespace {

// COM vtable slots (0-based) for the methods we care about, counted as
// IUnknown (3) + interface-specific methods above that.
constexpr size_t SLOT_CONNECT_SERVER = 3;   // IWbemLocator
constexpr size_t SLOT_EXEC_QUERY     = 20;  // IWbemServices  (IUnknown 3 + 17)
constexpr size_t SLOT_NEXT           = 4;   // IEnumWbemClassObject (IUnknown 3 + Reset(3) + Next(4))
constexpr size_t SLOT_GET            = 4;   // IWbemClassObject     (IUnknown 3 + GetQualifierSet(3) + Get(4))

using PFN_CoCreateInstance = HRESULT(STDAPICALLTYPE*)(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID*);
using PFN_ConnectServer    = HRESULT(STDMETHODCALLTYPE*)(IWbemLocator*, BSTR, BSTR, BSTR, BSTR, long, BSTR, IWbemContext*, IWbemServices**);
using PFN_ExecQuery        = HRESULT(STDMETHODCALLTYPE*)(IWbemServices*, BSTR, BSTR, long, IWbemContext*, IEnumWbemClassObject**);
using PFN_Next             = HRESULT(STDMETHODCALLTYPE*)(IEnumWbemClassObject*, long, ULONG, IWbemClassObject**, ULONG*);
using PFN_Get              = HRESULT(STDMETHODCALLTYPE*)(IWbemClassObject*, LPCWSTR, long, VARIANT*, CIMTYPE*, long*);

PFN_CoCreateInstance g_real_CoCreateInstance = nullptr;
PFN_ConnectServer    g_real_ConnectServer    = nullptr;
PFN_ExecQuery        g_real_ExecQuery        = nullptr;
PFN_Next             g_real_Next             = nullptr;
PFN_Get              g_real_Get              = nullptr;

enum class Kind : uint8_t {
    None,
    VideoController,
    Processor,
    ComputerSystem,
};

std::mutex g_mutex;
std::unordered_set<void**> g_seen_locator_vts;
std::unordered_set<void**> g_seen_services_vts;
std::unordered_set<void**> g_seen_enum_vts;
std::unordered_set<void**> g_seen_object_vts;

// Tracks which enumerators (and which result objects they produce) correspond
// to which WMI class, so the Get hook knows what to substitute.
std::unordered_map<IEnumWbemClassObject*, Kind> g_enum_kind;
std::unordered_map<IWbemClassObject*, Kind>     g_object_kind;

const char* kind_name(Kind k) {
    switch (k) {
        case Kind::VideoController: return "VideoController";
        case Kind::Processor:       return "Processor";
        case Kind::ComputerSystem:  return "ComputerSystem";
        default:                    return "None";
    }
}

bool patch_slot(void** vt, size_t slot, void* new_fn, void** out_original) {
    DWORD old = 0;
    if (!VirtualProtect(&vt[slot], sizeof(void*), PAGE_READWRITE, &old)) {
        GCLOG("wmi: VirtualProtect(rw) slot %zu failed GLE=%lu", slot, GetLastError());
        return false;
    }
    *out_original = vt[slot];
    vt[slot] = new_fn;
    VirtualProtect(&vt[slot], sizeof(void*), old, &old);
    return true;
}

bool is_identity_property(LPCWSTR name) {
    if (!name) return false;
    return _wcsicmp(name, L"Name")                 == 0
        || _wcsicmp(name, L"Caption")              == 0
        || _wcsicmp(name, L"Description")          == 0
        || _wcsicmp(name, L"VideoProcessor")       == 0
        || _wcsicmp(name, L"AdapterCompatibility") == 0;
}

bool is_nvidia_bstr(BSTR s) {
    if (!s) return false;
    return wcsstr(s, L"NVIDIA")  != nullptr
        || wcsstr(s, L"GeForce") != nullptr
        || wcsstr(s, L"RTX")     != nullptr
        || wcsstr(s, L"Quadro")  != nullptr;
}

// ---------- Get (on IWbemClassObject) -------------------------------------

void set_variant_bstr(VARIANT* v, const wchar_t* s) {
    if (v->vt == VT_BSTR && v->bstrVal) { SysFreeString(v->bstrVal); v->bstrVal = nullptr; }
    v->vt = VT_BSTR;
    v->bstrVal = SysAllocString(s);
}

void set_variant_uint32(VARIANT* v, uint32_t n) {
    if (v->vt == VT_BSTR && v->bstrVal) SysFreeString(v->bstrVal);
    v->vt = VT_I4;
    v->lVal = static_cast<LONG>(n);
}

// WMI often surfaces uint64 properties as VT_BSTR holding a decimal string
// to preserve precision.  Match whatever the real Get returned.
void set_variant_uint64(VARIANT* v, uint64_t n) {
    if (v->vt == VT_BSTR) {
        if (v->bstrVal) SysFreeString(v->bstrVal);
        wchar_t buf[32];
        _snwprintf_s(buf, _TRUNCATE, L"%llu", (unsigned long long)n);
        v->bstrVal = SysAllocString(buf);
    } else {
        v->vt = VT_UI8;
        v->ullVal = n;
    }
}

HRESULT STDMETHODCALLTYPE hk_Get(IWbemClassObject* self, LPCWSTR name, long flags,
                                 VARIANT* pVal, CIMTYPE* pType, long* plFlavor) {
    HRESULT hr = g_real_Get(self, name, flags, pVal, pType, plFlavor);
    if (FAILED(hr) || !name || !pVal) return hr;

    Kind kind;
    {
        std::lock_guard lk(g_mutex);
        auto it = g_object_kind.find(self);
        kind = it != g_object_kind.end() ? it->second : Kind::None;
    }
    if (kind == Kind::None || gpucfg::config().mode != gpucfg::Mode::Custom) return hr;
    const auto& cfg = gpucfg::config();

    if (kind == Kind::VideoController) {
        if (is_identity_property(name) && pVal->vt == VT_BSTR && is_nvidia_bstr(pVal->bstrVal)) {
            GCLOG("wmi: VideoController.Get(\"%ls\") real=\"%ls\" -> custom", name, pVal->bstrVal);
            set_variant_bstr(pVal, cfg.adapter_description.c_str());
        }
    } else if (kind == Kind::Processor) {
        if (_wcsicmp(name, L"Name") == 0 || _wcsicmp(name, L"Caption") == 0) {
            GCLOG("wmi: Processor.Get(\"%ls\") real=\"%ls\" -> custom=\"%ls\"",
                  name, pVal->vt == VT_BSTR && pVal->bstrVal ? pVal->bstrVal : L"(nonstr)",
                  cfg.cpu_name.c_str());
            set_variant_bstr(pVal, cfg.cpu_name.c_str());
        } else if (_wcsicmp(name, L"NumberOfCores") == 0) {
            GCLOG("wmi: Processor.Get(\"NumberOfCores\") -> %u", cfg.cpu_physical_cores);
            set_variant_uint32(pVal, cfg.cpu_physical_cores);
        } else if (_wcsicmp(name, L"NumberOfLogicalProcessors") == 0 ||
                   _wcsicmp(name, L"ThreadCount") == 0) {
            GCLOG("wmi: Processor.Get(\"%ls\") -> %u", name, cfg.cpu_logical_processors);
            set_variant_uint32(pVal, cfg.cpu_logical_processors);
        } else if (_wcsicmp(name, L"MaxClockSpeed") == 0 ||
                   _wcsicmp(name, L"CurrentClockSpeed") == 0) {
            GCLOG("wmi: Processor.Get(\"%ls\") -> %u MHz", name, cfg.cpu_max_clock_mhz);
            set_variant_uint32(pVal, cfg.cpu_max_clock_mhz);
        }
    } else if (kind == Kind::ComputerSystem) {
        if (_wcsicmp(name, L"TotalPhysicalMemory") == 0) {
            uint64_t bytes = static_cast<uint64_t>(cfg.ram_gb) * (1ULL << 30);
            GCLOG("wmi: ComputerSystem.Get(\"TotalPhysicalMemory\") -> %llu", (unsigned long long)bytes);
            set_variant_uint64(pVal, bytes);
        } else if (_wcsicmp(name, L"NumberOfProcessors") == 0) {
            set_variant_uint32(pVal, cfg.cpu_physical_cores);
        } else if (_wcsicmp(name, L"NumberOfLogicalProcessors") == 0) {
            set_variant_uint32(pVal, cfg.cpu_logical_processors);
        }
    }
    return hr;
}

void ensure_object_vt_patched(IWbemClassObject* obj) {
    if (!obj) return;
    void** vt = *reinterpret_cast<void***>(obj);
    std::lock_guard lk(g_mutex);
    if (!g_seen_object_vts.insert(vt).second) return;
    void* orig = nullptr;
    if (patch_slot(vt, SLOT_GET, reinterpret_cast<void*>(&hk_Get), &orig)) {
        if (!g_real_Get) g_real_Get = reinterpret_cast<PFN_Get>(orig);
        GCLOG("wmi: patched IWbemClassObject vtable %p slot %zu Get (real=%p)",
              (void*)vt, SLOT_GET, orig);
    }
}

// ---------- Next (on IEnumWbemClassObject) --------------------------------

HRESULT STDMETHODCALLTYPE hk_Next(IEnumWbemClassObject* self, long timeout, ULONG uCount,
                                  IWbemClassObject** ppObjs, ULONG* puReturned) {
    HRESULT hr = g_real_Next(self, timeout, uCount, ppObjs, puReturned);
    if (FAILED(hr)) return hr;

    Kind k = Kind::None;
    {
        std::lock_guard lk(g_mutex);
        auto it = g_enum_kind.find(self);
        if (it != g_enum_kind.end()) k = it->second;
    }
    if (k != Kind::None && ppObjs && puReturned) {
        for (ULONG i = 0; i < *puReturned; ++i) {
            IWbemClassObject* o = ppObjs[i];
            { std::lock_guard lk(g_mutex); g_object_kind[o] = k; }
            ensure_object_vt_patched(o);
        }
    }
    return hr;
}

void ensure_enum_vt_patched(IEnumWbemClassObject* enm) {
    if (!enm) return;
    void** vt = *reinterpret_cast<void***>(enm);
    std::lock_guard lk(g_mutex);
    if (!g_seen_enum_vts.insert(vt).second) return;
    void* orig = nullptr;
    if (patch_slot(vt, SLOT_NEXT, reinterpret_cast<void*>(&hk_Next), &orig)) {
        if (!g_real_Next) g_real_Next = reinterpret_cast<PFN_Next>(orig);
        GCLOG("wmi: patched IEnumWbemClassObject vtable %p slot %zu Next (real=%p)",
              (void*)vt, SLOT_NEXT, orig);
    }
}

// ---------- ExecQuery (on IWbemServices) ----------------------------------

Kind classify_query(BSTR query) {
    if (!query) return Kind::None;
    if (wcsstr(query, L"Win32_VideoController")) return Kind::VideoController;
    if (wcsstr(query, L"Win32_Processor"))       return Kind::Processor;
    if (wcsstr(query, L"Win32_ComputerSystem"))  return Kind::ComputerSystem;
    return Kind::None;
}

HRESULT STDMETHODCALLTYPE hk_ExecQuery(IWbemServices* self, BSTR lang, BSTR query, long flags,
                                       IWbemContext* ctx, IEnumWbemClassObject** ppEnum) {
    Kind k = classify_query(query);
    GCLOG("wmi: ExecQuery lang=\"%ls\" query=\"%ls\" kind=%s",
          lang ? lang : L"(null)", query ? query : L"(null)", kind_name(k));

    HRESULT hr = g_real_ExecQuery(self, lang, query, flags, ctx, ppEnum);
    if (SUCCEEDED(hr) && ppEnum && *ppEnum && k != Kind::None) {
        { std::lock_guard lk(g_mutex); g_enum_kind[*ppEnum] = k; }
        ensure_enum_vt_patched(*ppEnum);
    }
    return hr;
}

// ---------- ConnectServer (on IWbemLocator) -------------------------------

HRESULT STDMETHODCALLTYPE hk_ConnectServer(IWbemLocator* self, BSTR ns, BSTR user, BSTR pwd,
                                           BSTR locale, long flags, BSTR authority,
                                           IWbemContext* ctx, IWbemServices** ppSvc) {
    HRESULT hr = g_real_ConnectServer(self, ns, user, pwd, locale, flags, authority, ctx, ppSvc);
    if (SUCCEEDED(hr) && ppSvc && *ppSvc) {
        void** vt = *reinterpret_cast<void***>(*ppSvc);
        std::lock_guard lk(g_mutex);
        if (g_seen_services_vts.insert(vt).second) {
            void* orig = nullptr;
            if (patch_slot(vt, SLOT_EXEC_QUERY, reinterpret_cast<void*>(&hk_ExecQuery), &orig)) {
                if (!g_real_ExecQuery) g_real_ExecQuery = reinterpret_cast<PFN_ExecQuery>(orig);
                GCLOG("wmi: patched IWbemServices vtable %p slot %zu ExecQuery (real=%p)",
                      (void*)vt, SLOT_EXEC_QUERY, orig);
            }
        }
    }
    return hr;
}

// ---------- CoCreateInstance (on ole32) -----------------------------------

HRESULT STDAPICALLTYPE hk_CoCreateInstance(REFCLSID rclsid, LPUNKNOWN pUnkOuter, DWORD dwClsContext,
                                           REFIID riid, LPVOID* ppv) {
    HRESULT hr = g_real_CoCreateInstance(rclsid, pUnkOuter, dwClsContext, riid, ppv);
    if (SUCCEEDED(hr) && ppv && *ppv && IsEqualCLSID(rclsid, CLSID_WbemLocator)) {
        IWbemLocator* loc = reinterpret_cast<IWbemLocator*>(*ppv);
        void** vt = *reinterpret_cast<void***>(loc);
        std::lock_guard lk(g_mutex);
        if (g_seen_locator_vts.insert(vt).second) {
            void* orig = nullptr;
            if (patch_slot(vt, SLOT_CONNECT_SERVER, reinterpret_cast<void*>(&hk_ConnectServer), &orig)) {
                if (!g_real_ConnectServer) g_real_ConnectServer = reinterpret_cast<PFN_ConnectServer>(orig);
                GCLOG("wmi: patched IWbemLocator vtable %p slot %zu ConnectServer (real=%p)",
                      (void*)vt, SLOT_CONNECT_SERVER, orig);
            }
        }
    }
    return hr;
}

}  // namespace

bool install_hooks() {
    HMODULE ole = LoadLibraryW(L"ole32.dll");
    if (!ole) {
        GCLOG("wmi: LoadLibrary(ole32.dll) failed GLE=%lu", GetLastError());
        return false;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(ole, "CoCreateInstance"));
    if (!target) {
        GCLOG("wmi: CoCreateInstance not found in ole32");
        return false;
    }
    MH_STATUS st = MH_CreateHook(target,
        reinterpret_cast<void*>(&hk_CoCreateInstance),
        reinterpret_cast<void**>(&g_real_CoCreateInstance));
    if (st != MH_OK) {
        GCLOG("wmi: MH_CreateHook(CoCreateInstance) failed status=%d", int(st));
        return false;
    }
    GCLOG("wmi: hooked ole32.dll!CoCreateInstance @ %p", target);
    return true;
}

}  // namespace gpucfg::wmi_hooks
