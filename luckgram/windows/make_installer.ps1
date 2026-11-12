# Builds Luckgram-Setup-<version>-<build>-x64.exe with Inno Setup.
# Usage: make_installer.ps1 -Version 7.4.2 -Build 12 -SourceDir <stage> -OutputDir <dist>
param(
  [Parameter(Mandatory)] [string] $Version,
  [Parameter(Mandatory)] [string] $Build,
  [Parameter(Mandatory)] [string] $SourceDir,
  [Parameter(Mandatory)] [string] $OutputDir
)
$ErrorActionPreference = 'Stop'

# ChineseSimplified.isl needs Inno Setup 6.5+; install or upgrade.
choco upgrade innosetup -y --no-progress | Out-Host
$iscc = Get-ChildItem -Path "${env:ProgramFiles(x86)}\Inno Setup *\ISCC.exe", "$env:ProgramFiles\Inno Setup *\ISCC.exe" -ErrorAction SilentlyContinue |
  Sort-Object FullName -Descending | Select-Object -First 1
if (-not $iscc) { throw 'ISCC.exe not found' }
Write-Host "Using $($iscc.FullName)"

New-Item -ItemType Directory -Force $OutputDir | Out-Null
& $iscc.FullName `
  "/DMyAppVersion=$Version" `
  "/DMyBuildNumber=$Build" `
  "/DSourceDir=$SourceDir" `
  "/DOutputDir=$OutputDir" `
  (Join-Path $PSScriptRoot 'luckgram.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with $LASTEXITCODE" }
