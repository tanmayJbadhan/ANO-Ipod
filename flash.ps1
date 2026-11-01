<#
.SYNOPSIS
  Builds the Jigglypuff firmware and flashes it onto the console over USB.

.DESCRIPTION
  Finds PlatformIO, finds the console's USB port by itself (or uses -Port),
  builds, uploads, and can open the serial monitor afterwards.

.EXAMPLE
  .\flash.ps1                 # build + flash, port found automatically
  .\flash.ps1 -Port COM12     # use a specific port
  .\flash.ps1 -Monitor        # flash, then show the console's log
  .\flash.ps1 -BuildOnly      # just check that it compiles
  .\flash.ps1 -Clean          # throw away old build output first
#>
param(
  [string]$Port = "",
  [switch]$Monitor,
  [switch]$BuildOnly,
  [switch]$Clean
)

$ErrorActionPreference = "Stop"
Set-Location -LiteralPath $PSScriptRoot
$Env = "Jigglypuff"

function Say($msg, $color = "Cyan") { Write-Host "==> $msg" -ForegroundColor $color }

# --- PlatformIO -------------------------------------------------------------
$pio = $null
$cmd = Get-Command pio -ErrorAction SilentlyContinue
if ($cmd) { $pio = $cmd.Source }
if (-not $pio) {
  $candidate = Join-Path $env:USERPROFILE ".platformio\penv\Scripts\pio.exe"
  if (Test-Path $candidate) { $pio = $candidate }
}
if (-not $pio) {
  Say "PlatformIO not found." "Red"
  Write-Host "Install the PlatformIO extension in VS Code (or 'pip install platformio'), then run this again."
  exit 1
}

# --- WiFi settings ------------------------------------------------------------
$wifi = "5_Web_Uploader\WifiConfig.h"
if (-not (Test-Path $wifi)) {
  Copy-Item "5_Web_Uploader\WifiConfig.example.h" $wifi
  Say "Created $wifi from the template - WiFi stays off until you fill it in." "Yellow"
}

# --- Build ------------------------------------------------------------------
if ($Clean) {
  Say "Cleaning old build output"
  & $pio run -e $Env -t clean | Out-Null
}

if ($BuildOnly) {
  Say "Building"
  & $pio run -e $Env
  if ($LASTEXITCODE -ne 0) { Say "Build failed." "Red"; exit 1 }
  Say "Build OK." "Green"
  exit 0
}

# --- Find the console's port -----------------------------------------------
# The ESP32-S3's own USB shows up with Espressif's vendor ID 303A.
if (-not $Port) {
  $found = @(Get-CimInstance Win32_PnPEntity |
    Where-Object { $_.PNPDeviceID -match 'VID_303A' -and $_.Name -match '\((COM\d+)\)' } |
    ForEach-Object { if ($_.Name -match '\((COM\d+)\)') { $Matches[1] } })
  if ($found.Count -eq 1) {
    $Port = $found[0]
    Say "Found the console on $Port"
  } elseif ($found.Count -gt 1) {
    Say "More than one ESP32 is plugged in: $($found -join ', ')" "Yellow"
    Write-Host "Run again with -Port, e.g.  .\flash.ps1 -Port $($found[0])"
    exit 1
  } else {
    Say "Couldn't find the console on USB." "Red"
    Write-Host "Check the cable (some are charge-only) and that the console is switched on."
    Write-Host "Ports seen right now:"
    Get-CimInstance Win32_PnPEntity | Where-Object { $_.Name -match '\(COM\d+\)' } |
      ForEach-Object { Write-Host "   $($_.Name)" }
    Write-Host "If yours is listed, run:  .\flash.ps1 -Port COMx"
    exit 1
  }
}

# --- Flash ------------------------------------------------------------------
Say "Building and flashing to $Port (takes about a minute)"
& $pio run -e $Env -t upload --upload-port $Port
if ($LASTEXITCODE -ne 0) {
  Say "Flashing failed." "Red"
  Write-Host "If the port is busy, close any serial monitor that has it open."
  Write-Host "If the board doesn't answer: hold BOOT, tap RST, release BOOT, then run this again."
  exit 1
}
Say "Done - the console restarts by itself." "Green"

if ($Monitor) {
  Say "Serial monitor (Ctrl+C to quit). Test keys: g = game, h = home, s/u/d/l/r = buttons"
  & $pio device monitor -p $Port -b 115200
}
