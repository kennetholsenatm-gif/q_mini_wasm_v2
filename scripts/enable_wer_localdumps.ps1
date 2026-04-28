# Register WER LocalDumps for qminiwasm.exe so access violations produce a .dmp (minidump by default).
# Requires: run PowerShell elevated (Administrator) so HKLM can be written.
# See: https://learn.microsoft.com/en-us/windows/win32/wer/collecting-user-mode-dumps
#
# Usage (from repo root):
#   powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\enable_wer_localdumps.ps1
# Optional:  -ExeName mytesthost.exe -DumpFolder "C:\Dumps"

param(
    [string] $ExeName = "qminiwasm.exe",
    [string] $DumpFolder = ""
)

$ErrorActionPreference = "Stop"

if (-not $DumpFolder) {
    $DumpFolder = Join-Path $env:LOCALAPPDATA "q_mini_wer_dumps"
}
New-Item -ItemType Directory -Force -Path $DumpFolder | Out-Null

$werRoot = "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps"
if (-not (Test-Path $werRoot)) {
    Write-Error "Cannot access WER LocalDumps path (try Run as Administrator)."
}
$regPath = Join-Path $werRoot $ExeName
New-Item -Path $regPath -Force | Out-Null
New-ItemProperty -Path $regPath -Name DumpFolder -PropertyType ExpandString -Value $DumpFolder -Force | Out-Null
# 1 = mini dump (small); 2 = full dump
New-ItemProperty -Path $regPath -Name DumpType -PropertyType DWord -Value 1 -Force | Out-Null
New-ItemProperty -Path $regPath -Name DumpCount -PropertyType DWord -Value 10 -Force | Out-Null

Write-Host "[WER] LocalDumps registered for $ExeName"
Write-Host "      DumpFolder=$DumpFolder"
Write-Host "      DumpType=1 (mini) DumpCount=10"
Write-Host "      After a native crash, look for $ExeName.*.dmp under the folder above."
