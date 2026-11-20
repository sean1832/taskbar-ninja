# Builds and packages a release zip into dist/.
# Usage: pwsh scripts/build-release.ps1 <version>
# Example: pwsh scripts/build-release.ps1 1.2.0
param(
    [Parameter(Mandatory = $true)]
    [string]$Version
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $true # stop when cmake exits non-zero

if ($Version.StartsWith('v')) {
    throw "version must not start with 'v', got '$Version'"
}

# repo root
Set-Location (Join-Path $PSScriptRoot '..')

# clean outputs
Remove-Item -Recurse -Force build-release, dist -ErrorAction SilentlyContinue
New-Item -ItemType Directory dist | Out-Null

# no -G: CMake picks the newest Visual Studio installed
cmake -B build-release -A x64 "-DTASKBAR_NINJA_VERSION=$Version"
cmake --build build-release --config Release

$stage_name = "taskbar-ninja_${Version}_x64"
$stage = Join-Path build-release $stage_name
New-Item -ItemType Directory $stage | Out-Null
Copy-Item build-release/Release/taskbar-ninja.exe, build-release/Release/taskbar-ninja-explorer.dll, README.md, LICENSE $stage

# one wrapping folder inside the zip
Compress-Archive -Path "$stage/*" -DestinationPath "dist/$stage_name.zip"

Write-Output "version=$Version zip=dist/$stage_name.zip"
