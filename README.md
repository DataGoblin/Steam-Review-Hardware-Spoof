# Steam Review Hardware Spoof

A proxy `version.dll` that changes the hardware Steam detects in **Help → System Information**: GPU name and VRAM, CPU name and RAM. Pick a template hardware config from [presets/](presets/) or make your own.

This project was made to spoof the hardware attached to my steam reviews just for fun. Nothing more stupid than a $30,000 GPU being used to play Stardew Valley right? 
Thankfully steam let you save PC specs to your account, so you only need to use this tool once or when ever you want to make a new hardware config. Once you have saved your desired hardware config you can have fun larping in steam reviews.

Please DO NOT poison the steam hardware survey, every so often Steam will prompt you to submit your hardware to contribute your specs which is a great resource. It does not submit data automatically so you can make sure the spoofing is disabled. 

<img width="627" height="501" alt="image" src="https://github.com/user-attachments/assets/cca254cd-8759-42be-9676-7aecf88a7a33" />

<img width="475" height="120" alt="image" src="https://github.com/user-attachments/assets/c948043b-ccbf-4339-86f5-1d0265d6ac0a" />


# WARNING
> - **Anti-cheat:** the DLL gets hooked by the Steam client process and patches memory. To minimise risk, do not have any game open and make sure no persistent anti-cheat is running (like Riot Vanguard). If you have a spare computer you don't use for gaming like an awful school laptop, you can just run this tool on it with steam installed to be extra safe.

## Install

A prebuilt `version.dll` (x64) is in the repo root. To compile it yourself, see [Build from source](#build-from-source).

1. Exit Steam completely.
2. Open PowerShell as administrator in this folder, set `$preset` to the rig you want, and run:

   ```powershell
   $steam  = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam').InstallPath
   $preset = 'presets\rtx-5090.ini'

   foreach ($dir in $steam, "$steam\bin") {
       Copy-Item version.dll $dir
       Copy-Item $preset "$dir\GPUConfig.ini"
   }
   ```

3. Start Steam and open **Help → System Information** if it shows the spoofed hardware, congrats.

To switch rigs, run the same commands with a different `$preset` and restart Steam.

## Presets

#### CPU spoofing is DISABLED by default. Swap `SpoofCpu` to `true` in the preset config you pick. 

| Preset | GPU | VRAM | CPU | RAM |
|---|---|---|---|---|
| `rtx-pro-6000-blackwell-maxq` | NVIDIA RTX PRO 6000 Blackwell Max-Q Workstation Edition | 96 GB | Ryzen Threadripper PRO 7995WX, 96 cores | 128 GB |
| `rtx-5090` | NVIDIA GeForce RTX 5090 | 32 GB | Ryzen 9 9950X3D, 16 cores | 64 GB |
| `rtx-4090` | NVIDIA GeForce RTX 4090 | 24 GB | Ryzen 7 7800X3D, 8 cores | 32 GB |
| `rtx-4060` | NVIDIA GeForce RTX 4060 | 8 GB | Ryzen 5 5600X, 6 cores | 16 GB |
| `h100-80gb` | NVIDIA H100 80GB HBM3 | 80 GB | EPYC 9654, 96 cores | 512 GB |
| `h200-141gb` | NVIDIA H200 | 141 GB | EPYC 9754, 128 cores | 1024 GB |
| `b200-180gb` | NVIDIA B200 | 180 GB | EPYC 9575F, 64 cores | 2048 GB |
| `potato` | "Potato Processing Unit" | 1 GB | Athlon 3000G, 2 cores | 4 GB |

All presets are NVIDIA GPU with an AMD CPU. Did not test any other pairing! (lazy)

## Turn it off

Rename `version.dll` to `version.dll.disabled` in both Steam folders. Restart Steam

## Uninstalling

With Steam closed, run the script below with PowerShell ran as administrator:

```powershell
scripts\Uninstall-GPUConfig.ps1
```

This removes the DLL and config from both Steam folders and deletes the log folder.

## Configuration

`GPUConfig.ini` sits next to `version.dll` and is read once when Steam starts. The copy in the repo root documents every key and is the template for a custom rig.

| Key | Meaning |
|---|---|
| `Mode` | `custom` returns the values below. `passthrough` changes nothing and only logs. |
| `AdapterDescription` | GPU name, up to 127 characters |
| `VendorId`, `DeviceId` | PCI vendor and device ID, hex or decimal |
| `SubSysId` | `(subsystem id << 16) \| subsystem vendor id` |
| `VramGB` | Dedicated video memory in GB |
| `CPUName` | CPU name, up to 47 characters |
| `CPUPhysicalCores`, `CPULogicalProcessors`, `CPUMaxClockMHz` | Core count, thread count and clock speed |
| `RamGB` | Total system memory in GB |
| `SpoofCpu` | `true` also changes the CPU brand, family, model and stepping by patching Steam's code in memory. Off by default; read the warning in `GPUConfig.ini` first |
| `CPUFamily`, `CPUModel`, `CPUStepping` | CPUID signature, used only with `SpoofCpu=true` |

A config has to list every key: one that is missing falls back to a built-in default, not to your real hardware.

Each Steam process that loads the DLL writes a log to `%LOCALAPPDATA%\GPUConfig\`, showing every intercepted query with the values before and after.

## Build from source

Requirements: Windows x64, Visual Studio 2022 or the VS 2022 Build Tools with the "Desktop development with C++" workload, and CMake 3.20 or newer (bundled with Visual Studio).

From a Developer PowerShell for VS 2022, in the repo root:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
```

The result is `build\out\version.dll`.

## Limitations

- **NVIDIA GPUs only.** Some of the hooks only replace a GPU name that contains `NVIDIA`, `GeForce`, `RTX` or `Quadro`, and driver details such as the driver version stay real.
- **The CPU brand needs `SpoofCpu=true`.** Without it Steam still shows the real CPU.
- **CPU core counts and clock speed may still show real values in Steam.** Steam reads them through calls this DLL does not intercept. 
- **The CPU vendor string is never changed.** With `SpoofCpu=true`, choose a CPU from the same brand as your real one.
- **x64 only.**

## License

[MIT](LICENSE):

- [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, BSD 2-Clause, including Hacker Disassembler Engine 64 by Vyacheslav Patkov. See `third_party/minhook/LICENSE.txt`.
- [Vulkan-Headers](https://github.com/KhronosGroup/Vulkan-Headers) by The Khronos Group, Apache-2.0 OR MIT. See `third_party/vulkan-headers/LICENSE.md`.
