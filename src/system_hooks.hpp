#pragma once

namespace gpucfg::system_hooks {

// Hooks kernel32 APIs used for system resource totals:
// GlobalMemoryStatusEx and GetPhysicallyInstalledSystemMemory.
bool install_hooks();

}  // namespace gpucfg::system_hooks
