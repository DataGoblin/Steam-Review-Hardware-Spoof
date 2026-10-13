// Replaces every CPUID (0F A2) instruction inside tier0_s64.dll with
// INT3 + NOP (CC 90), then handles the resulting EXCEPTION_BREAKPOINT
// via a Vectored Exception Handler that emulates CPUID with whatever
// identity the INI configures.

#include "cpuid_hooks.hpp"
#include "config.hpp"
#include "log.hpp"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_set>

// LdrRegisterDllNotification is exported by ntdll but not declared in the
// public SDK.  Minimal manual declaration.
typedef LONG NTSTATUS;
typedef struct _UNICODE_STRING {
    USHORT Length;
    USHORT MaximumLength;
    PWCH   Buffer;
} UNICODE_STRING, *PUNICODE_STRING;

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG                  Flags;
    const UNICODE_STRING*  FullDllName;
    const UNICODE_STRING*  BaseDllName;
    PVOID                  DllBase;
    ULONG                  SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA, *PLDR_DLL_LOADED_NOTIFICATION_DATA;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA   Loaded;
    LDR_DLL_LOADED_NOTIFICATION_DATA   Unloaded;
} LDR_DLL_NOTIFICATION_DATA, *PLDR_DLL_NOTIFICATION_DATA;

#define LDR_DLL_NOTIFICATION_REASON_LOADED   1
#define LDR_DLL_NOTIFICATION_REASON_UNLOADED 2

typedef VOID (NTAPI *PLDR_DLL_NOTIFICATION_FUNCTION)(
    ULONG NotificationReason,
    PLDR_DLL_NOTIFICATION_DATA NotificationData,
    PVOID Context);

using PFN_LdrRegisterDllNotification = NTSTATUS (NTAPI*)(
    ULONG, PLDR_DLL_NOTIFICATION_FUNCTION, PVOID, PVOID*);

extern "C" {
#include "hde/hde64.h"
}

namespace gpucfg::cpuid_hooks {

namespace {

std::mutex                 g_mutex;
std::unordered_set<void*>  g_patched_sites;
std::atomic<bool>          g_veh_installed{false};
PVOID                      g_veh_cookie = nullptr;

struct CpuidResult { uint32_t eax, ebx, ecx, edx; };

// Fill 16 bytes of the 48-byte brand string into r based on which
// 0x80000002-4 leaf is requested.
void fill_brand_leaf(uint32_t leaf, const char* brand48, CpuidResult& r) {
    size_t offset = (leaf - 0x80000002u) * 16u;
    std::memcpy(&r.eax, brand48 + offset, 4);
    std::memcpy(&r.ebx, brand48 + offset + 4, 4);
    std::memcpy(&r.ecx, brand48 + offset + 8, 4);
    std::memcpy(&r.edx, brand48 + offset + 12, 4);
}

CpuidResult emulate(uint32_t leaf, uint32_t subleaf) {
    int regs[4] = {};
    __cpuidex(regs, static_cast<int>(leaf), static_cast<int>(subleaf));
    CpuidResult r{static_cast<uint32_t>(regs[0]), static_cast<uint32_t>(regs[1]),
                  static_cast<uint32_t>(regs[2]), static_cast<uint32_t>(regs[3])};

    const auto& cfg = gpucfg::config();
    if (cfg.mode != gpucfg::Mode::Custom || !cfg.spoof_cpu) return r;

    switch (leaf) {
        case 0x00000001u: {
            // Pack family/model/stepping into EAX.
            uint32_t family       = cfg.cpu_family;
            uint32_t model        = cfg.cpu_model;
            uint32_t stepping     = cfg.cpu_stepping & 0xFu;
            uint32_t family_base  = (family >= 0xF) ? 0xFu : (family & 0xFu);
            uint32_t family_ext   = (family >= 0xF) ? (family - 0xF) & 0xFFu : 0u;
            uint32_t model_low    = model & 0xFu;
            uint32_t model_ext    = (model >> 4) & 0xFu;
            uint32_t type         = 0u;
            r.eax = (family_ext << 20) | (model_ext << 16) | (type << 12) |
                    (family_base << 8) | (model_low << 4) | stepping;
            break;
        }
        case 0x00000007u:
            if (subleaf == 0u) {
                // Advertise AVX-512 capability (F, DQ, IFMA, CD, BW, VL) + a few ECX extensions
                r.ebx |= (1u << 16) | (1u << 17) | (1u << 21) |
                         (1u << 28) | (1u << 30) | (1u << 31);
                r.ecx |= (1u << 1)  | (1u << 6)  | (1u << 11);  // VBMI, VBMI2, VNNI
            }
            break;
        case 0x80000002u:
        case 0x80000003u:
        case 0x80000004u: {
            // 48-byte CPUID brand string (null-padded).
            char brand48[48] = {};
            size_t n = cfg.cpu_name_utf8.size();
            if (n > 47) n = 47;
            std::memcpy(brand48, cfg.cpu_name_utf8.data(), n);
            fill_brand_leaf(leaf, brand48, r);
            break;
        }
        case 0x80000008u: {
            // ECX[7:0] = NC (logical processors - 1)
            uint32_t nc = cfg.cpu_logical_processors > 0
                        ? (cfg.cpu_logical_processors - 1) & 0xFFu
                        : 0u;
            r.ecx = (r.ecx & ~0xFFu) | nc;
            // ECX[15:12] = ApicIdSize (ceil(log2(logical))) — approximate to 8 for
            // high core counts.
            uint32_t apic_id_size = 0;
            for (uint32_t v = cfg.cpu_logical_processors ? cfg.cpu_logical_processors - 1 : 0;
                 v; v >>= 1) apic_id_size++;
            if (apic_id_size > 15) apic_id_size = 15;
            r.ecx = (r.ecx & ~0xF000u) | (apic_id_size << 12);
            break;
        }
        default:
            break;
    }
    return r;
}

LONG CALLBACK veh(PEXCEPTION_POINTERS ep) {
    if (ep->ExceptionRecord->ExceptionCode != static_cast<DWORD>(EXCEPTION_BREAKPOINT))
        return EXCEPTION_CONTINUE_SEARCH;

    void* rip = ep->ExceptionRecord->ExceptionAddress;
    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (g_patched_sites.find(rip) == g_patched_sites.end())
            return EXCEPTION_CONTINUE_SEARCH;
    }

    uint32_t leaf    = static_cast<uint32_t>(ep->ContextRecord->Rax);
    uint32_t subleaf = static_cast<uint32_t>(ep->ContextRecord->Rcx);
    CpuidResult r = emulate(leaf, subleaf);

    ep->ContextRecord->Rax = r.eax;
    ep->ContextRecord->Rbx = r.ebx;
    ep->ContextRecord->Rcx = r.ecx;
    ep->ContextRecord->Rdx = r.edx;
    ep->ContextRecord->Rip += 2;  // step past INT3 + NOP
    return EXCEPTION_CONTINUE_EXECUTION;
}

size_t patch_module_text(HMODULE mod) {
    if (!mod) return 0;

    auto base = reinterpret_cast<uint8_t*>(mod);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return 0;
    auto nt   = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return 0;

    auto section = IMAGE_FIRST_SECTION(nt);
    IMAGE_SECTION_HEADER* text = nullptr;
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (std::memcmp(section[i].Name, ".text", 5) == 0) { text = &section[i]; break; }
    }
    if (!text) return 0;

    uint8_t* start = base + text->VirtualAddress;
    size_t   size  = text->Misc.VirtualSize;
    uint8_t* limit = start + (size > 1 ? size - 1 : 0);

    size_t patched = 0;
    for (uint8_t* p = start; p < limit; ++p) {
        if (p[0] != 0x0F || p[1] != 0xA2) continue;

        // Validate with hde64: a genuine CPUID is a 2-byte instruction with no error.
        hde64s hs{};
        unsigned int len = hde64_disasm(p, &hs);
        if (len != 2 || (hs.flags & F_ERROR)) continue;

        DWORD old_prot = 0;
        if (!VirtualProtect(p, 2, PAGE_EXECUTE_READWRITE, &old_prot)) continue;
        p[0] = 0xCC;  // INT3
        p[1] = 0x90;  // NOP
        VirtualProtect(p, 2, old_prot, &old_prot);
        FlushInstructionCache(GetCurrentProcess(), p, 2);

        {
            std::lock_guard<std::mutex> lk(g_mutex);
            g_patched_sites.insert(p);
        }
        ++patched;
        // Skip the two bytes we just replaced.
        ++p;
    }
    return patched;
}

PVOID g_ldr_cookie = nullptr;

bool base_name_is(const UNICODE_STRING* name, const wchar_t* target) {
    if (!name || !name->Buffer) return false;
    size_t target_len = 0; while (target[target_len]) ++target_len;
    size_t name_len = name->Length / sizeof(wchar_t);
    if (name_len != target_len) return false;
    for (size_t i = 0; i < name_len; ++i) {
        wchar_t a = name->Buffer[i]; wchar_t b = target[i];
        if (a >= L'A' && a <= L'Z') a = wchar_t(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = wchar_t(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

VOID NTAPI ldr_notification(ULONG reason, PLDR_DLL_NOTIFICATION_DATA data, PVOID /*ctx*/) {
    if (reason != LDR_DLL_NOTIFICATION_REASON_LOADED || !data) return;
    const LDR_DLL_LOADED_NOTIFICATION_DATA& d = data->Loaded;
    if (!base_name_is(d.BaseDllName, L"tier0_s64.dll")) return;

    size_t n = patch_module_text(static_cast<HMODULE>(d.DllBase));
    GCLOG("cpuid: tier0_s64.dll loaded (ldr notify) @ %p, patched %zu CPUID site(s)",
          d.DllBase, n);
}

}  // namespace

bool install_hooks() {
    const auto& cfg = gpucfg::config();
    if (!cfg.spoof_cpu) {
        GCLOG("cpuid: disabled (SpoofCpu=false)");
        return false;
    }

    if (!g_veh_installed.exchange(true)) {
        g_veh_cookie = AddVectoredExceptionHandler(1, veh);
        if (!g_veh_cookie) {
            GCLOG("cpuid: AddVectoredExceptionHandler failed GLE=%lu", GetLastError());
            g_veh_installed = false;
            return false;
        }
        GCLOG("cpuid: VEH installed @ %p", g_veh_cookie);
    }

    // Register for DLL-load notifications so we can patch tier0_s64.dll the
    // instant it's mapped into the process (before steam.exe's init code can
    // call GetCPUInformation() and cache the real CPUID results).
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (ntdll) {
        auto LdrRegister = reinterpret_cast<PFN_LdrRegisterDllNotification>(
            GetProcAddress(ntdll, "LdrRegisterDllNotification"));
        if (LdrRegister) {
            NTSTATUS st = LdrRegister(0, &ldr_notification, nullptr, &g_ldr_cookie);
            if (st == 0) {
                GCLOG("cpuid: LdrRegisterDllNotification installed (cookie=%p)",
                      g_ldr_cookie);
            } else {
                GCLOG("cpuid: LdrRegisterDllNotification failed NTSTATUS=0x%08lX",
                      static_cast<unsigned long>(st));
            }
        }
    }

    HMODULE tier0 = GetModuleHandleW(L"tier0_s64.dll");
    if (tier0) {
        size_t n = patch_module_text(tier0);
        GCLOG("cpuid: tier0_s64.dll already loaded @ %p, patched %zu CPUID site(s)",
              (void*)tier0, n);
    } else {
        GCLOG("cpuid: tier0_s64.dll not loaded yet; waiting for load notification");
    }
    return true;
}

}  // namespace gpucfg::cpuid_hooks
