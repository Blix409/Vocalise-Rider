# Builds Vocalise-Rider-<version>-Windows-Setup.exe (needs Inno Setup 6 and a finished Release build).
param([string]$Version = "1.0.0")
$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path -Parent (Split-Path -Parent $here)
$vst3 = Join-Path $root "build\VocalRider_artefacts\Release\VST3\Vocalise-Rider.vst3"
if (-not (Test-Path $vst3)) {
    Write-Host "Build first: could not find $vst3" -ForegroundColor Red
    exit 1
}
$candidates = @(
    "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
    "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
    "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
)
$iscc = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $iscc) {
    Write-Host "Inno Setup 6 was not found. Install it free from https://jrsoftware.org/isinfo.php and run this again." -ForegroundColor Red
    exit 1
}
Push-Location $here
try { & $iscc "/DAppVersion=$Version" "VocaliseRider.iss"; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } } finally { Pop-Location }
Write-Host ""
Write-Host "Created: $(Join-Path $root "Vocalise-Rider-$Version-Windows-Setup.exe")" -ForegroundColor Green
