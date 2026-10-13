// Real-function thunks for every export of C:\Windows\System32\version.dll.
//
// Why not PE forwarders: because our own DLL is named version.dll, the Windows
// loader resolves a forwarder "version.X" back to us -> infinite-recursion
// detected at EXE load time -> STATUS_INVALID_IMAGE_FORMAT (0xC000007B) before
// DllMain runs.  Real thunks side-step that: the loader sees normal exports
// with RVAs, binds imports to our thunk functions, and each thunk forwards to
// the real DLL (loaded by absolute path, so KnownDLLs lookup never revisits
// our module).
//
// winver.h declares these Windows APIs as dllimport, so we implement our
// thunks under alias names (Impl_X) and export-alias them to the real API
// names via /EXPORT pragmas.  Avoids "different linkage" redefinition
// errors without needing to avoid winver.h.
//
// Keep the kNames[] array below in sync with the exports of the system DLL
// (dumpbin /exports C:\Windows\System32\version.dll).

#include <windows.h>

#include <mutex>

namespace {

constexpr int kCount = 17;

const char* const kNames[kCount] = {
    "GetFileVersionInfoA",
    "GetFileVersionInfoByHandle",
    "GetFileVersionInfoExA",
    "GetFileVersionInfoExW",
    "GetFileVersionInfoSizeA",
    "GetFileVersionInfoSizeExA",
    "GetFileVersionInfoSizeExW",
    "GetFileVersionInfoSizeW",
    "GetFileVersionInfoW",
    "VerFindFileA",
    "VerFindFileW",
    "VerInstallFileA",
    "VerInstallFileW",
    "VerLanguageNameA",
    "VerLanguageNameW",
    "VerQueryValueA",
    "VerQueryValueW",
};

HMODULE      g_real       = nullptr;
FARPROC      g_fn[kCount] = {};
std::once_flag g_once;

void do_load() {
    // Absolute path -> the loader does not go through application-dir search,
    // so it cannot find our own DLL again.
    g_real = LoadLibraryW(L"C:\\Windows\\System32\\version.dll");
    if (!g_real) return;
    for (int i = 0; i < kCount; ++i) g_fn[i] = GetProcAddress(g_real, kNames[i]);
}

inline void ensure_loaded() { std::call_once(g_once, do_load); }

}  // namespace

namespace gpucfg::version_exports {

void init()   { ensure_loaded(); }
bool loaded() { return g_real != nullptr; }

}  // namespace gpucfg::version_exports

// ---------- thunks ----------------------------------------------------------
// Implemented under alias names; /EXPORT pragmas below re-export each under
// the real Windows API name.

extern "C" {

BOOL WINAPI Impl_GetFileVersionInfoA(LPCSTR a, DWORD b, DWORD c, LPVOID d) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    auto f = reinterpret_cast<Fn>(g_fn[0]);
    return f ? f(a, b, c, d) : FALSE;
}

// Undocumented. 4-arg signature per community reverse engineering.
DWORD WINAPI Impl_GetFileVersionInfoByHandle(INT_PTR a, LPCWSTR b, DWORD c, LPVOID d) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(INT_PTR, LPCWSTR, DWORD, LPVOID);
    auto f = reinterpret_cast<Fn>(g_fn[1]);
    return f ? f(a, b, c, d) : 0;
}

BOOL WINAPI Impl_GetFileVersionInfoExA(DWORD a, LPCSTR b, DWORD c, DWORD d, LPVOID e) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    auto f = reinterpret_cast<Fn>(g_fn[2]);
    return f ? f(a, b, c, d, e) : FALSE;
}

BOOL WINAPI Impl_GetFileVersionInfoExW(DWORD a, LPCWSTR b, DWORD c, DWORD d, LPVOID e) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    auto f = reinterpret_cast<Fn>(g_fn[3]);
    return f ? f(a, b, c, d, e) : FALSE;
}

DWORD WINAPI Impl_GetFileVersionInfoSizeA(LPCSTR a, LPDWORD b) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(LPCSTR, LPDWORD);
    auto f = reinterpret_cast<Fn>(g_fn[4]);
    return f ? f(a, b) : 0;
}

DWORD WINAPI Impl_GetFileVersionInfoSizeExA(DWORD a, LPCSTR b, LPDWORD c) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCSTR, LPDWORD);
    auto f = reinterpret_cast<Fn>(g_fn[5]);
    return f ? f(a, b, c) : 0;
}

DWORD WINAPI Impl_GetFileVersionInfoSizeExW(DWORD a, LPCWSTR b, LPDWORD c) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    auto f = reinterpret_cast<Fn>(g_fn[6]);
    return f ? f(a, b, c) : 0;
}

DWORD WINAPI Impl_GetFileVersionInfoSizeW(LPCWSTR a, LPDWORD b) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(LPCWSTR, LPDWORD);
    auto f = reinterpret_cast<Fn>(g_fn[7]);
    return f ? f(a, b) : 0;
}

BOOL WINAPI Impl_GetFileVersionInfoW(LPCWSTR a, DWORD b, DWORD c, LPVOID d) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    auto f = reinterpret_cast<Fn>(g_fn[8]);
    return f ? f(a, b, c, d) : FALSE;
}

DWORD WINAPI Impl_VerFindFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d,
                               LPSTR e, PUINT f, LPSTR g, PUINT h) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(g_fn[9]);
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI Impl_VerFindFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d,
                               LPWSTR e, PUINT f, LPWSTR g, PUINT h) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(g_fn[10]);
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI Impl_VerInstallFileA(DWORD a, LPCSTR b, LPCSTR c, LPCSTR d,
                                  LPCSTR e, LPCSTR f, LPSTR g, PUINT h) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(g_fn[11]);
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI Impl_VerInstallFileW(DWORD a, LPCWSTR b, LPCWSTR c, LPCWSTR d,
                                  LPCWSTR e, LPCWSTR f, LPWSTR g, PUINT h) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    auto fn = reinterpret_cast<Fn>(g_fn[12]);
    return fn ? fn(a, b, c, d, e, f, g, h) : 0;
}

DWORD WINAPI Impl_VerLanguageNameA(DWORD a, LPSTR b, DWORD c) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPSTR, DWORD);
    auto f = reinterpret_cast<Fn>(g_fn[13]);
    return f ? f(a, b, c) : 0;
}

DWORD WINAPI Impl_VerLanguageNameW(DWORD a, LPWSTR b, DWORD c) {
    ensure_loaded();
    using Fn = DWORD (WINAPI*)(DWORD, LPWSTR, DWORD);
    auto f = reinterpret_cast<Fn>(g_fn[14]);
    return f ? f(a, b, c) : 0;
}

BOOL WINAPI Impl_VerQueryValueA(LPCVOID a, LPCSTR b, LPVOID* c, PUINT d) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    auto f = reinterpret_cast<Fn>(g_fn[15]);
    return f ? f(a, b, c, d) : FALSE;
}

BOOL WINAPI Impl_VerQueryValueW(LPCVOID a, LPCWSTR b, LPVOID* c, PUINT d) {
    ensure_loaded();
    using Fn = BOOL (WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    auto f = reinterpret_cast<Fn>(g_fn[16]);
    return f ? f(a, b, c, d) : FALSE;
}

}  // extern "C"

// ---------- export aliases --------------------------------------------------

#pragma comment(linker, "/EXPORT:GetFileVersionInfoA=Impl_GetFileVersionInfoA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoByHandle=Impl_GetFileVersionInfoByHandle")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExA=Impl_GetFileVersionInfoExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoExW=Impl_GetFileVersionInfoExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeA=Impl_GetFileVersionInfoSizeA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExA=Impl_GetFileVersionInfoSizeExA")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeExW=Impl_GetFileVersionInfoSizeExW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoSizeW=Impl_GetFileVersionInfoSizeW")
#pragma comment(linker, "/EXPORT:GetFileVersionInfoW=Impl_GetFileVersionInfoW")
#pragma comment(linker, "/EXPORT:VerFindFileA=Impl_VerFindFileA")
#pragma comment(linker, "/EXPORT:VerFindFileW=Impl_VerFindFileW")
#pragma comment(linker, "/EXPORT:VerInstallFileA=Impl_VerInstallFileA")
#pragma comment(linker, "/EXPORT:VerInstallFileW=Impl_VerInstallFileW")
#pragma comment(linker, "/EXPORT:VerLanguageNameA=Impl_VerLanguageNameA")
#pragma comment(linker, "/EXPORT:VerLanguageNameW=Impl_VerLanguageNameW")
#pragma comment(linker, "/EXPORT:VerQueryValueA=Impl_VerQueryValueA")
#pragma comment(linker, "/EXPORT:VerQueryValueW=Impl_VerQueryValueW")
