# Download and extract official LibTorch (CPU, MSVC /shared-with-deps) under cpp/.deps/libtorch.
# Usage (from repo root):  .\scripts\install-libtorch.ps1 [-Version 2.5.1] [-Force]
# After install, configure CMake with -DCMAKE_PREFIX_PATH=<printed path> or set env LIBTORCH_ROOT.
# ASCII-only: Windows PowerShell 5.1 can misparse UTF-8 without BOM if non-ASCII appears in strings.
param(
    [string] $Version = "2.5.1",
    [switch] $Force
)
$ErrorActionPreference = "Stop"

function Invoke-Native {
    param([string] $FilePath, [string[]] $ArgumentList)
    $p = Start-Process -FilePath $FilePath -ArgumentList $ArgumentList -NoNewWindow -Wait -PassThru
    if ($null -eq $p) { return -1 }
    return [int]$p.ExitCode
}

function Download-LibTorchArchive {
    param([string] $DownloadUrl, [string] $OutPath)

    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        $curlArgs = @("-fSL", "--connect-timeout", "60", "-o", $OutPath, $DownloadUrl)
        $code = Invoke-Native -FilePath "curl.exe" -ArgumentList $curlArgs
        if ($code -eq 0) {
            return
        }
        Write-Warning "curl.exe exited with code $code (23 = write error: disk, path, or AV). Trying WebClient..."
        Remove-Item -LiteralPath $OutPath -Force -ErrorAction SilentlyContinue
    }

    try {
        $wc = New-Object System.Net.WebClient
        $wc.DownloadFile($DownloadUrl, $OutPath)
    } catch {
        Remove-Item -LiteralPath $OutPath -Force -ErrorAction SilentlyContinue
        Write-Warning ".NET WebClient failed: $($_.Exception.Message). Trying Invoke-WebRequest..."
        $prev = $ProgressPreference
        $ProgressPreference = "SilentlyContinue"
        try {
            if ($PSVersionTable.PSVersion.Major -ge 6) {
                Invoke-WebRequest -Uri $DownloadUrl -OutFile $OutPath -UseBasicParsing -TimeoutSec 0
            } else {
                Invoke-WebRequest -Uri $DownloadUrl -OutFile $OutPath -UseBasicParsing
            }
        } finally {
            $ProgressPreference = $prev
        }
    }
}

$RepoRoot = Split-Path $PSScriptRoot -Parent
$DepsParent = Join-Path $RepoRoot "cpp\.deps"
$InstallRoot = Join-Path $DepsParent "libtorch"
$Marker = Join-Path $InstallRoot "share\cmake\Torch\TorchConfig.cmake"

if ((Test-Path -LiteralPath $Marker) -and -not $Force) {
    Write-Host "LibTorch already present at: $InstallRoot"
    Write-Host "Re-run with -Force to re-download."
    Write-Host ""
    Write-Host "CMake: -DCMAKE_PREFIX_PATH=`"$InstallRoot`"  (or set LIBTORCH_ROOT to that path)."
    exit 0
}

New-Item -ItemType Directory -Path $DepsParent -Force | Out-Null
$EncodedPlus = "%2B"
$Url = "https://download.pytorch.org/libtorch/cpu/libtorch-win-shared-with-deps-${Version}${EncodedPlus}cpu.zip"
# Zip lives under cpp/.deps (not %TEMP%) to avoid curl exit 23 write errors on some systems.
$Zip = Join-Path $DepsParent "libtorch-win-cpu-${Version}.zip"
Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue

Write-Host "Downloading: $Url"
Write-Host "  -> $Zip"
Write-Host "  (LibTorch is ~2GB; this can take several minutes.)"

Download-LibTorchArchive -DownloadUrl $Url -OutPath $Zip

if (-not (Test-Path -LiteralPath $Zip) -or ((Get-Item -LiteralPath $Zip).Length -lt 1MB)) {
    throw "Download failed or file too small: $Zip (check disk space and network)."
}

if (Test-Path -LiteralPath $InstallRoot) {
    Remove-Item -LiteralPath $InstallRoot -Recurse -Force
}
Expand-Archive -LiteralPath $Zip -DestinationPath $DepsParent -Force
Remove-Item -LiteralPath $Zip -Force -ErrorAction SilentlyContinue

if (-not (Test-Path -LiteralPath $Marker)) {
    throw "Extracted tree missing TorchConfig.cmake at $Marker"
}

Write-Host ""
Write-Host "LibTorch installed at:"
Write-Host "  $InstallRoot"
Write-Host ""
Write-Host 'Configure (PowerShell, from cpp/):'
if ($env:VCPKG_ROOT) {
    Write-Host "  cmake -S . -B build ``"
    Write-Host "    -DCMAKE_TOOLCHAIN_FILE=`"$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake`" ``"
    Write-Host "    -DQMINIWASM_WITH_TRAINING_ENGINE=ON -DQMINIWASM_WITH_GRPC=ON -DQMINIWASM_BUILD_PYBIND=OFF ``"
    Write-Host "    -DCMAKE_PREFIX_PATH=`"$InstallRoot`" ``"
    Write-Host "    -DQMINIWASM_TRAINING_WITH_LIBTORCH=ON"
} else {
    Write-Host "  cmake -S . -B build ``"
    Write-Host "    -DCMAKE_TOOLCHAIN_FILE=`"<VCPKG_ROOT>/scripts/buildsystems/vcpkg.cmake`" ``"
    Write-Host "    -DQMINIWASM_WITH_TRAINING_ENGINE=ON -DQMINIWASM_WITH_GRPC=ON -DQMINIWASM_BUILD_PYBIND=OFF ``"
    Write-Host "    -DCMAKE_PREFIX_PATH=`"$InstallRoot`" ``"
    Write-Host "    -DQMINIWASM_TRAINING_WITH_LIBTORCH=ON"
}
Write-Host ""
Write-Host "Or set env LIBTORCH_ROOT to the install path (CMake prepends it to CMAKE_PREFIX_PATH)."
exit 0
