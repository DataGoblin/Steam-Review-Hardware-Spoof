#pragma once

namespace gpucfg::dxgi_hooks {

// Creates MinHook detours over dxgi.dll!CreateDXGIFactory{,1,2} but does NOT
// enable them. Caller enables via MH_EnableHook(MH_ALL_HOOKS). Returns true if
// at least one hook was created.
bool install_hooks();

}  // namespace gpucfg::dxgi_hooks
