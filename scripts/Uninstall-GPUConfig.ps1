<#
.SYNOPSIS
    Removes every file GPUConfig ever deployed

.DESCRIPTION
    Deletes:
      - version.dll + version.dll.disabled + GPUConfig.ini from
        C:\Program Files (x86)\Steam\
      - version.dll + version.dll.disabled + GPUConfig.ini from
        C:\Program Files (x86)\Steam\bin\
      - %LOCALAPPDATA%\GPUConfig\ (all log files)

    Refuses to run while Steam is running (log files and version.dll would
    be locked)

.PARAMETER KeepLogs
    If present, preserves %LOCALAPPDATA%\GPUConfig\ so the logs remain
    available for debug.

.EXAMPLE
    .\Uninstall-GPUConfig.ps1
    .\Uninstall-GPUConfig.ps1 -KeepLogs
#>

#Requires -RunAsAdministrator

[CmdletBinding()]
param(
    [switch]$KeepLogs
)

$ErrorActionPreference = 'Stop'

function Test-SteamRunning {
    $procs = Get-Process -ErrorAction SilentlyContinue |
             Where-Object { $_.Name -in 'steam','steamwebhelper','steamservice','steamsysinfo' }
    return $procs.Count -gt 0
}

if (Test-SteamRunning) {
    Write-Warning 'Steam is still running. Exit Steam fully (tray icon -> Exit) and rerun this script.'
    exit 1
}

# Locate Steam via registry (fallback to default path).
$steamRoot = $null
try {
    $steamRoot = (Get-ItemProperty 'HKLM:\SOFTWARE\WOW6432Node\Valve\Steam' -ErrorAction Stop).InstallPath
} catch {}
if (-not $steamRoot -or -not (Test-Path $steamRoot)) {
    $steamRoot = 'C:\Program Files (x86)\Steam'
}
$steamBin = Join-Path $steamRoot 'bin'

$targets = @()
foreach ($dir in @($steamRoot, $steamBin)) {
    foreach ($name in @('version.dll','version.dll.disabled','GPUConfig.ini')) {
        $path = Join-Path $dir $name
        if (Test-Path $path) { $targets += $path }
    }
}

if ($targets.Count -eq 0) {
    Write-Host 'No GPUConfig files found in Steam directories. Already clean.' -ForegroundColor Green
} else {
    Write-Host ('Removing {0} file(s):' -f $targets.Count) -ForegroundColor Cyan
    foreach ($t in $targets) {
        try {
            Remove-Item -LiteralPath $t -Force -ErrorAction Stop
            Write-Host "  [OK] $t"
        } catch {
            Write-Warning "  [FAIL] $t -- $($_.Exception.Message)"
        }
    }
}

$logDir = Join-Path $env:LOCALAPPDATA 'GPUConfig'
if (Test-Path $logDir) {
    if ($KeepLogs) {
        Write-Host "Keeping log directory $logDir (use without -KeepLogs to delete)." -ForegroundColor DarkGray
    } else {
        try {
            Remove-Item -LiteralPath $logDir -Recurse -Force -ErrorAction Stop
            Write-Host "Removed $logDir" -ForegroundColor Cyan
        } catch {
            Write-Warning "Could not remove $logDir -- $($_.Exception.Message)"
        }
    }
}

Write-Host "`nUninstall complete. Launch Steam to confirm the panel shows real hardware again." -ForegroundColor Green
