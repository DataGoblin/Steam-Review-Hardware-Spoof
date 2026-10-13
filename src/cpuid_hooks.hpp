#pragma once

namespace gpucfg::cpuid_hooks {

// Installs a Vectored Exception Handler, then scans tier0_s64.dll's .text
// section for the CPUID instruction (0F A2) and replaces each occurrence with
// INT3 + NOP (CC 90).  The VEH catches the resulting EXCEPTION_BREAKPOINT
// and synthesizes fake CPUID results for selected leaves.  Gated on both
// Mode=custom and SpoofCpu=true in the INI.
//
// Only patches when the config flag is set.  tier0_s64.dll may not be loaded
// yet when install_hooks() runs (steam.exe loads it lazily); in that case a
// short-lived poll thread waits for it to appear.
bool install_hooks();

}  // namespace gpucfg::cpuid_hooks
