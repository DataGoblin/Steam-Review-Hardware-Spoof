#pragma once

namespace gpucfg::identity_hooks {

// Installs MinHook detours over the extra Windows APIs Steam uses to
// cross-validate adapter identity: SetupDiGetDeviceRegistryProperty
// and EnumDisplayDevices.  These are secondary to DXGI/Vulkan but Steam
// references them from steamui.dll / steamclient64.dll.
bool install_hooks();

}  // namespace gpucfg::identity_hooks
