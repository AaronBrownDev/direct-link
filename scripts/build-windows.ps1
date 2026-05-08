# Build the direct-link client as a portable Windows distribution.
#
# Run on Windows in a "x64 Native Tools Command Prompt for VS 2022" shell
# (or any environment where cl.exe + cmake are on PATH).
#
# Usage:
#   pwsh -File scripts\build-windows.ps1 [-Version dev]
#
# Required environment / parameters:
#   -QtPrefix   Path to Qt6 install (e.g. C:\Qt\6.8.0\msvc2022_64)
#   -LiveKitDir Path to a Windows livekit-cpp build that contains
#               build-release\bin\livekit.dll and livekit_ffi.dll
#   -VcpkgRoot  (optional) Path to vcpkg root used by livekit-cpp
#
# Output: dist\direct-link-<version>-windows-x64.zip

[CmdletBinding()]
param(
    [string]$Version    = $(if ($env:VERSION) { $env:VERSION } else { 'dev' }),
    [string]$QtPrefix   = $env:QT_PREFIX,
    [string]$LiveKitDir = $(if ($env:LIVEKIT_DIR) { $env:LIVEKIT_DIR } else { "$env:USERPROFILE\livekit-cpp" }),
    [string]$VcpkgRoot  = $env:VCPKG_ROOT,
    [string]$BuildDir   = $(if ($env:BUILD_DIR) { $env:BUILD_DIR } else { "$PSScriptRoot\..\client\build-windows" }),
    [string]$DistDir    = $(if ($env:DIST_DIR) { $env:DIST_DIR } else { "$PSScriptRoot\..\dist" }),
    [int]   $Jobs       = $(if ($env:NUMBER_OF_PROCESSORS) { [int]$env:NUMBER_OF_PROCESSORS } else { 4 })
)

$ErrorActionPreference = 'Stop'

function Log  ($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
function Die  ($msg) { Write-Host "==> $msg" -ForegroundColor Red; exit 1 }

# --- Resolve paths ---------------------------------------------------------
$RepoRoot  = (Resolve-Path "$PSScriptRoot\..").Path
$ClientDir = Join-Path $RepoRoot 'client'
$BuildDir  = [System.IO.Path]::GetFullPath($BuildDir)
$DistDir   = [System.IO.Path]::GetFullPath($DistDir)
$StageDir  = Join-Path $BuildDir 'stage'
$AppName   = 'direct-link'
$ZipOut    = Join-Path $DistDir "$AppName-$Version-windows-x64.zip"

# --- Validate prerequisites ------------------------------------------------
foreach ($tool in @('cmake', 'cl')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        Die "Missing required tool: $tool. Run from a VS x64 Native Tools prompt."
    }
}

if (-not $QtPrefix) {
    $qmake = Get-Command qmake.exe -ErrorAction SilentlyContinue
    if ($qmake) {
        $QtPrefix = (& $qmake.Source -query QT_INSTALL_PREFIX).Trim()
    } else {
        Die "QtPrefix is not set and qmake.exe is not on PATH. Pass -QtPrefix C:\Qt\<ver>\msvc2022_64."
    }
}
if (-not (Test-Path $QtPrefix))   { Die "QtPrefix does not exist: $QtPrefix" }
if (-not (Test-Path $LiveKitDir)) { Die "LiveKitDir does not exist: $LiveKitDir" }

$WindeployQt = Join-Path $QtPrefix 'bin\windeployqt.exe'
if (-not (Test-Path $WindeployQt)) { Die "windeployqt.exe not found at $WindeployQt" }

# Locate livekit DLLs - prefer build-release, fall back to common layouts.
$livekitDllCandidates = @(
    'build-release\bin',
    'build-release\lib',
    'build\bin',
    'build\Release'
)
$LiveKitBinDir = $null
foreach ($rel in $livekitDllCandidates) {
    $candidate = Join-Path $LiveKitDir $rel
    if (Test-Path (Join-Path $candidate 'livekit.dll')) {
        $LiveKitBinDir = $candidate
        break
    }
}
if (-not $LiveKitBinDir) {
    Die "Could not find livekit.dll under $LiveKitDir. Build livekit-cpp release first (.\build.cmd release)."
}
Log "LiveKit DLLs: $LiveKitBinDir"
Log "Qt prefix:    $QtPrefix"

# --- Configure -------------------------------------------------------------
Log "Configuring (Release) in $BuildDir"
$configureArgs = @(
    '-S', $ClientDir,
    '-B', $BuildDir,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DCMAKE_PREFIX_PATH=$QtPrefix",
    "-DLIVEKIT_DIR=$LiveKitDir"
)
if ($VcpkgRoot) {
    $configureArgs += "-DCMAKE_TOOLCHAIN_FILE=$VcpkgRoot\scripts\buildsystems\vcpkg.cmake"
}
& cmake @configureArgs
if ($LASTEXITCODE -ne 0) { Die "CMake configure failed" }

# --- Build -----------------------------------------------------------------
Log "Building direct-link with $Jobs jobs"
& cmake --build $BuildDir --target $AppName --config Release -j $Jobs
if ($LASTEXITCODE -ne 0) { Die "Build failed" }

# --- Stage -----------------------------------------------------------------
Log "Staging portable layout at $StageDir"
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Path $StageDir | Out-Null

# Copy the built executable
$ExePath = Get-ChildItem -Path $BuildDir -Filter "$AppName.exe" -Recurse |
    Select-Object -First 1
if (-not $ExePath) { Die "$AppName.exe not found under $BuildDir" }
Copy-Item $ExePath.FullName -Destination $StageDir

# windeployqt: pull in Qt DLLs, QML modules, plugins.
Log "Running windeployqt"
& $WindeployQt --release --no-translations --qmldir "$ClientDir\src" `
    (Join-Path $StageDir "$AppName.exe")
if ($LASTEXITCODE -ne 0) { Die "windeployqt failed" }

# LiveKit DLLs
Log "Bundling LiveKit DLLs"
Copy-Item (Join-Path $LiveKitBinDir 'livekit.dll')     -Destination $StageDir
Copy-Item (Join-Path $LiveKitBinDir 'livekit_ffi.dll') -Destination $StageDir
$bridgeDll = Join-Path $LiveKitBinDir 'livekit_bridge.dll'
if (Test-Path $bridgeDll) { Copy-Item $bridgeDll -Destination $StageDir }

# vcpkg-bundled DLLs (protobuf/abseil/etc.) live next to livekit.dll
# in livekit-cpp's distribution; copy them if present.
Get-ChildItem -Path $LiveKitBinDir -Filter '*.dll' |
    Where-Object { $_.Name -notmatch '^livekit' } |
    ForEach-Object {
        $dest = Join-Path $StageDir $_.Name
        if (-not (Test-Path $dest)) { Copy-Item $_.FullName -Destination $dest }
    }

# --- Package ---------------------------------------------------------------
New-Item -ItemType Directory -Path $DistDir -Force | Out-Null
if (Test-Path $ZipOut) { Remove-Item -Force $ZipOut }

Log "Compressing to $ZipOut"
Compress-Archive -Path "$StageDir\*" -DestinationPath $ZipOut -CompressionLevel Optimal

Log "Built: $ZipOut"
Get-Item $ZipOut | Format-List Name, Length, FullName
