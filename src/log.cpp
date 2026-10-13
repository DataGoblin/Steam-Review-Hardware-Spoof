#include "log.hpp"

#include <windows.h>
#include <shlobj.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>

namespace gpucfg::log {

namespace {

std::mutex g_mutex;
FILE* g_file = nullptr;
std::wstring g_path;

std::wstring build_log_path() {
    PWSTR local_app_data = nullptr;
    std::wstring dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local_app_data))) {
        dir = local_app_data;
        CoTaskMemFree(local_app_data);
    } else {
        wchar_t temp[MAX_PATH];
        DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", temp, MAX_PATH);
        if (n > 0 && n < MAX_PATH) {
            dir = temp;
        } else {
            dir = L"C:\\Temp";
        }
    }
    dir += L"\\GPUConfig";
    CreateDirectoryW(dir.c_str(), nullptr);  // ignore ERROR_ALREADY_EXISTS

    wchar_t module_path[MAX_PATH];
    GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    const wchar_t* base = wcsrchr(module_path, L'\\');
    base = base ? base + 1 : module_path;

    std::wstring process_name = base;
    size_t dot = process_name.rfind(L'.');
    if (dot != std::wstring::npos) process_name.resize(dot);

    SYSTEMTIME st;
    GetLocalTime(&st);

    wchar_t filename[MAX_PATH];
    _snwprintf_s(filename, MAX_PATH, _TRUNCATE,
                 L"%s\\gpuconfig_%s_%lu_%04d%02d%02d-%02d%02d%02d.log",
                 dir.c_str(), process_name.c_str(),
                 GetCurrentProcessId(),
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond);

    return filename;
}

}  // namespace

bool init() {
    std::lock_guard lock(g_mutex);
    if (g_file) return true;

    g_path = build_log_path();
    g_file = _wfsopen(g_path.c_str(), L"a", _SH_DENYNO);
    if (!g_file) {
        wchar_t msg[512];
        _snwprintf_s(msg, _TRUNCATE,
                     L"[GPUConfig] Failed to open log file: %ls\n", g_path.c_str());
        OutputDebugStringW(msg);
        return false;
    }

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char header[1024];
    int n = _snprintf_s(header, _TRUNCATE,
        "=== GPUConfig log opened %04d-%02d-%02d %02d:%02d:%02d.%03d ===\n"
        "pid=%lu host=%ls\n",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
        GetCurrentProcessId(), exe);
    if (n > 0) {
        fwrite(header, 1, static_cast<size_t>(n), g_file);
        fflush(g_file);
    }
    return true;
}

void shutdown() {
    std::lock_guard lock(g_mutex);
    if (g_file) {
        fputs("=== GPUConfig log closed ===\n", g_file);
        fflush(g_file);
        fclose(g_file);
        g_file = nullptr;
    }
}

void write(std::string_view line) {
    std::lock_guard lock(g_mutex);
    if (!g_file) return;

    SYSTEMTIME st;
    GetLocalTime(&st);
    char prefix[64];
    int pn = _snprintf_s(prefix, _TRUNCATE,
                         "%02d:%02d:%02d.%03d t=%lu ",
                         st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                         GetCurrentThreadId());
    if (pn > 0) fwrite(prefix, 1, static_cast<size_t>(pn), g_file);
    fwrite(line.data(), 1, line.size(), g_file);
    if (line.empty() || line.back() != '\n') fputc('\n', g_file);
    fflush(g_file);
}

const std::wstring& log_path() { return g_path; }

}  // namespace gpucfg::log
