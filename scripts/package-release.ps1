# package-release.ps1 — stage and zip a portable HAVEN-FSK release.
#
# Run AFTER build-release.bat. Produces:
#   dist\HavenFSK-v<version>-win64\   (clean staging folder, runnable)
#   dist\HavenFSK-v<version>-win64.zip
#
# Staging uses windeployqt --dir into a FRESH folder rather than zipping
# build-release\ directly — the build tree is full of object files and
# CMake internals that must not ship.

$ErrorActionPreference = "Stop"

$RepoRoot   = Split-Path -Parent $PSScriptRoot
$BuildDir   = Join-Path $RepoRoot "build-release"
$Exe        = Join-Path $BuildDir "HavenFSK.exe"
$WinDeploy  = "C:\Qt\6.11.1\mingw_64\bin\windeployqt.exe"

if (-not (Test-Path $Exe)) {
    Write-Error "build-release\HavenFSK.exe not found - run build-release.bat first"
}

# Version from the single source of truth in Constants.h
$verLine = Select-String -Path (Join-Path $RepoRoot "src\dsp\Constants.h") `
                         -Pattern 'APP_VERSION\s*=\s*"([^"]+)"'
$Version = $verLine.Matches[0].Groups[1].Value
if (-not $Version) { Write-Error "Could not read APP_VERSION from Constants.h" }

$Name    = "HavenFSK-v$Version-win64"
$DistDir = Join-Path $RepoRoot "dist"
$Staging = Join-Path $DistDir $Name
$ZipPath = Join-Path $DistDir "$Name.zip"

Write-Host "Packaging HAVEN-FSK v$Version -> $ZipPath"

# Fresh staging folder every time — stale files from a previous version
# must never leak into a new release.
if (Test-Path $Staging) { Remove-Item -Recurse -Force $Staging }
New-Item -ItemType Directory -Force $Staging | Out-Null

Copy-Item $Exe $Staging

# Qt runtime (DLLs, plugins, translations) into the staging dir.
# windeployqt prints benign warnings to stderr (e.g. optional D3D12
# compiler DLLs); under $ErrorActionPreference=Stop those would abort the
# script if stderr is redirected, so relax it just for this native call
# and judge success by the exit code alone.
$eap = $ErrorActionPreference
$ErrorActionPreference = "Continue"
& $WinDeploy --release --dir $Staging (Join-Path $Staging "HavenFSK.exe")
$deployExit = $LASTEXITCODE
$ErrorActionPreference = $eap
if ($deployExit -ne 0) { Write-Error "windeployqt failed (exit $deployExit)" }

# Hamlib runtime set + MinGW runtime, taken from the build dir where the
# CMake POST_BUILD step already resolved the correct copies (Hamlib's own
# libwinpthread/libgcc must win over Qt's — see CMakeLists.txt). Copied
# AFTER windeployqt so these overwrite whatever it placed.
$RuntimeDlls = @(
    "libhamlib-4.dll", "libusb-1.0.dll",
    "libwinpthread-1.dll", "libgcc_s_seh-1.dll", "libstdc++-6.dll"
)
foreach ($dll in $RuntimeDlls) {
    $src = Join-Path $BuildDir $dll
    if (Test-Path $src) { Copy-Item $src $Staging -Force }
    else { Write-Warning "$dll not found in build-release (Hamlib disabled?)" }
}

# License and docs — GPL requires the license text to ship with binaries
foreach ($doc in @("LICENSE", "THIRD_PARTY_LICENSES.md", "README.md", "CHANGELOG.md")) {
    Copy-Item (Join-Path $RepoRoot $doc) $Staging
}

if (Test-Path $ZipPath) { Remove-Item -Force $ZipPath }
Compress-Archive -Path $Staging -DestinationPath $ZipPath

$zipMB = [Math]::Round((Get-Item $ZipPath).Length / 1MB, 1)
Write-Host ""
Write-Host "Done: $ZipPath ($zipMB MB)"
Write-Host "Smoke test: unzip somewhere fresh and run HavenFSK.exe before publishing."
