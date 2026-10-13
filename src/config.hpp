#pragma once

#include <cstdint>
#include <string>

namespace gpucfg {

enum class Mode { Passthrough, Custom };

struct Config {
    Mode        mode                      = Mode::Passthrough;
    std::wstring adapter_description       = L"NVIDIA RTX PRO 6000 Blackwell Max-Q Workstation Edition";
    std::string  adapter_description_utf8  = "NVIDIA RTX PRO 6000 Blackwell Max-Q Workstation Edition";
    std::string  gl_renderer               = "NVIDIA RTX PRO 6000 Blackwell Max-Q Workstation Edition/PCIe/SSE2";
    std::string  gl_vendor                 = "NVIDIA Corporation";

    // CPU / RAM spoofing.  Name, core counts and clock speed are returned
    // through WMI.  The CPU Brand Steam shows comes from CPUID directly, so
    // it stays real unless spoof_cpu below is enabled.
    std::wstring cpu_name                  = L"AMD Ryzen Threadripper PRO 7995WX 96-Cores";
    std::string  cpu_name_utf8             = "AMD Ryzen Threadripper PRO 7995WX 96-Cores";
    uint32_t     cpu_physical_cores        = 96;
    uint32_t     cpu_logical_processors    = 192;
    uint32_t     cpu_max_clock_mhz         = 2500;
    uint32_t     ram_gb                    = 128;

    // Opt-in CPUID patching (INT3 + VEH).  Defaults to false because it
    // modifies code pages inside Valve's tier0_s64.dll and would likely be
    // noticed by anti-cheat memory scanners.  Flip SpoofCpu=true only for
    // cosmetic screenshot sessions, and only when no anti-cheat game will
    // launch afterwards.
    bool         spoof_cpu                 = false;
    // CPUID family/model/stepping for the target CPU (Threadripper 7000 =
    // family 0x19, model 0x18, stepping 0x1 by default).
    uint32_t     cpu_family                = 0x19;
    uint32_t     cpu_model                 = 0x18;
    uint32_t     cpu_stepping              = 0x1;
    uint32_t     vendor_id        = 0x10DE;
    uint32_t     device_id        = 0x2BB4;
    uint32_t     subsys_id        = 0x204C10DE;   // (SSID << 16) | SVID
    uint32_t     vram_gb          = 96;
    int          log_level        = 1;       // 0 = quiet, 1 = normal, 2 = verbose
    std::wstring config_path;                // resolved absolute path or empty
    bool         loaded_from_file = false;
};

// Reads GPUConfig.ini from the directory of the given module handle.
// Missing file => defaults (passthrough, log_level=1).
void load_config(void* self_module, Config& out);

const Config& config();

}  // namespace gpucfg
