#pragma once

namespace gpucfg::wmi_hooks {

// Hooks the WMI COM vtables that steamui/steamclient use to read
// Win32_VideoController, so that queries for Name / Caption / Description /
// VideoProcessor on video-controller results return our custom adapter
// description instead of the real driver string.
bool install_hooks();

}  // namespace gpucfg::wmi_hooks
