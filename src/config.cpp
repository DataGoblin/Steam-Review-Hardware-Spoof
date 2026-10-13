#include "config.hpp"
#include "log.hpp"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>

namespace gpucfg {

namespace {

Config g_cfg;

std::string trim(std::string_view s) {
    auto is_ws = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    size_t b = 0, e = s.size();
    while (b < e && is_ws(s[b])) ++b;
    while (e > b && is_ws(s[e - 1])) --e;
    return std::string(s.substr(b, e - b));
}

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = char(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = char(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

uint32_t parse_hex_or_dec(std::string_view s) {
    std::string t = trim(s);
    if (t.empty()) return 0;
    int base = 10;
    const char* p = t.c_str();
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) {
        base = 16;
        p += 2;
    }
    return static_cast<uint32_t>(std::strtoul(p, nullptr, base));
}

std::wstring module_dir(void* self_module) {
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(static_cast<HMODULE>(self_module), buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return L".";
    std::wstring path(buf, n);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L".";
    return path.substr(0, slash);
}

}  // namespace

void load_config(void* self_module, Config& out) {
    out = Config{};

    std::wstring dir = module_dir(self_module);
    std::wstring path = dir + L"\\GPUConfig.ini";
    out.config_path = path;

    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) {
        GCLOG("config: no GPUConfig.ini found at %ls -- defaults (Mode=passthrough)",
               path.c_str());
        g_cfg = out;
        return;
    }

    char line[1024];
    std::string section;
    while (fgets(line, sizeof(line), f)) {
        std::string t = trim(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t.front() == '[' && t.back() == ']') {
            section = t.substr(1, t.size() - 2);
            continue;
        }
        size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(std::string_view(t).substr(0, eq));
        std::string val = trim(std::string_view(t).substr(eq + 1));

        if (iequals(key, "Mode")) {
            out.mode = iequals(val, "custom") ? Mode::Custom : Mode::Passthrough;
        } else if (iequals(key, "AdapterDescription")) {
            out.adapter_description      = widen(val);
            out.adapter_description_utf8 = val;
        } else if (iequals(key, "GLRenderer")) {
            out.gl_renderer = val;
        } else if (iequals(key, "GLVendor")) {
            out.gl_vendor = val;
        } else if (iequals(key, "VendorId")) {
            out.vendor_id = parse_hex_or_dec(val);
        } else if (iequals(key, "DeviceId")) {
            out.device_id = parse_hex_or_dec(val);
        } else if (iequals(key, "SubSysId")) {
            out.subsys_id = parse_hex_or_dec(val);
        } else if (iequals(key, "VramGB")) {
            out.vram_gb = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (iequals(key, "CPUName")) {
            out.cpu_name      = widen(val);
            out.cpu_name_utf8 = val;
        } else if (iequals(key, "CPUPhysicalCores")) {
            out.cpu_physical_cores = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (iequals(key, "CPULogicalProcessors")) {
            out.cpu_logical_processors = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (iequals(key, "CPUMaxClockMHz")) {
            out.cpu_max_clock_mhz = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (iequals(key, "RamGB")) {
            out.ram_gb = static_cast<uint32_t>(std::strtoul(val.c_str(), nullptr, 10));
        } else if (iequals(key, "SpoofCpu")) {
            out.spoof_cpu = iequals(val, "true") || iequals(val, "1") || iequals(val, "yes");
        } else if (iequals(key, "CPUFamily")) {
            out.cpu_family = parse_hex_or_dec(val);
        } else if (iequals(key, "CPUModel")) {
            out.cpu_model = parse_hex_or_dec(val);
        } else if (iequals(key, "CPUStepping")) {
            out.cpu_stepping = parse_hex_or_dec(val);
        } else if (iequals(key, "LogLevel")) {
            out.log_level = static_cast<int>(std::strtol(val.c_str(), nullptr, 10));
        }
    }
    fclose(f);
    out.loaded_from_file = true;

    GCLOG("config: loaded %ls (Mode=%s, AdapterDescription=\"%ls\", "
           "VendorId=0x%04X, DeviceId=0x%04X, SubSysId=0x%08X, VramGB=%u, LogLevel=%d)",
           path.c_str(),
           out.mode == Mode::Custom ? "custom" : "passthrough",
           out.adapter_description.c_str(),
           out.vendor_id, out.device_id, out.subsys_id, out.vram_gb, out.log_level);

    g_cfg = out;
}

const Config& config() { return g_cfg; }

}  // namespace gpucfg
