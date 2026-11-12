# Builds Luckgram-Setup-<version>-<build>-x64.exe with Inno Setup.
# Usage: make_installer.ps1 -Version 7.4.2 -Build 12 -SourceDir <stage> -OutputDir <dist>
param(
  [Parameter(Mandatory)] [string] $Version,
  [Parameter(Mandatory)] [string] $Build,
  [Parameter(Mandatory)] [string] $SourceDir,
  [Parameter(Mandatory)] [string] $OutputDir
)
$ErrorActionPreference = 'Stop'

# ChineseSimplified.isl needs Inno Setup 6.5+.
function Find-Iscc {
  Get-ChildItem -Path "${env:ProgramFiles(x86)}\Inno Setup *\ISCC.exe", "$env:ProgramFiles\Inno Setup *\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup *\ISCC.exe" -ErrorAction SilentlyContinue |
    Where-Object { [version]('{0}.{1}' -f $_.VersionInfo.FileMajorPart, $_.VersionInfo.FileMinorPart) -ge [version]'6.5' } |
    Sort-Object { $_.VersionInfo.FileMajorPart * 1000 + $_.VersionInfo.FileMinorPart } -Descending | Select-Object -First 1
}
$iscc = Find-Iscc
if (-not $iscc -and (Get-Command choco -ErrorAction SilentlyContinue)) {
  # Hosted runners: install or upgrade with Chocolatey.
  choco upgrade innosetup -y --no-progress | Out-Host
  $iscc = Find-Iscc
}
if (-not $iscc) { throw 'Inno Setup 6.5+ (ISCC.exe) not found. Install it from https://jrsoftware.org/isdl.php' }
Write-Host "Using $($iscc.FullName) $($iscc.VersionInfo.FileVersion)"

New-Item -ItemType Directory -Force $OutputDir | Out-Null
& $iscc.FullName `
  "/DMyAppVersion=$Version" `
  "/DMyBuildNumber=$Build" `
  "/DSourceDir=$SourceDir" `
  "/DOutputDir=$OutputDir" `
  (Join-Path $PSScriptRoot 'luckgram.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with $LASTEXITCODE" }
