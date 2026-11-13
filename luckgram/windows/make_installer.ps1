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
function Get-IsccVersion($file) {
  # ISCC.exe has no version resource (0.0.0.0); use Inno's uninstall entry.
  $keys = 'HKLM:\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*',
          'HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*',
          'HKCU:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\*'
  $dir = (Split-Path $file.FullName).TrimEnd('\')
  foreach ($e in (Get-ItemProperty $keys -ErrorAction SilentlyContinue)) {
    if ($e.InstallLocation -and $e.InstallLocation.TrimEnd('\') -eq $dir -and $e.DisplayVersion -match '^(\d+)\.(\d+)') {
      return [version]"$($Matches[1]).$($Matches[2])"
    }
  }
  return [version]'99.0'  # unknown: ISCC fails on ChineseSimplified.isl if it is too old
}
function Find-Iscc {
  Get-ChildItem -Path "${env:ProgramFiles(x86)}\Inno Setup *\ISCC.exe", "$env:ProgramFiles\Inno Setup *\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup *\ISCC.exe" -ErrorAction SilentlyContinue |
    ForEach-Object { Write-Host "Found $($_.FullName) version $(Get-IsccVersion $_)"; $_ } |
    Where-Object { (Get-IsccVersion $_) -ge [version]'6.5' } |
    Sort-Object { Get-IsccVersion $_ } -Descending | Select-Object -First 1
}
$iscc = Find-Iscc
if (-not $iscc -and (Get-Command choco -ErrorAction SilentlyContinue)) {
  # Hosted runners: install or upgrade with Chocolatey.
  choco upgrade innosetup -y --no-progress | Out-Host
  $iscc = Find-Iscc
}
if (-not $iscc) { throw 'Inno Setup 6.5+ (ISCC.exe) not found. Install it from https://jrsoftware.org/isdl.php' }
Write-Host "Using $($iscc.FullName) $(Get-IsccVersion $iscc)"

New-Item -ItemType Directory -Force $OutputDir | Out-Null
& $iscc.FullName `
  "/DMyAppVersion=$Version" `
  "/DMyBuildNumber=$Build" `
  "/DSourceDir=$SourceDir" `
  "/DOutputDir=$OutputDir" `
  (Join-Path $PSScriptRoot 'luckgram.iss')
if ($LASTEXITCODE -ne 0) { throw "ISCC failed with $LASTEXITCODE" }
