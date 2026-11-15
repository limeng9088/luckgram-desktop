# Fills the action archive cache of the xs-master runner
# (ACTIONS_RUNNER_ACTION_ARCHIVE_CACHE). Run when the network is good.
# Layout the runner looks for (actions/runner v2.338.2, ActionManager.cs):
#   <cache>\<owner>_<repo>\<sha>.zip   (zipball of the action repository)
param([string]$Cache = 'E:\r\action-archive-cache', [string]$Proxy = 'http://127.0.0.1:7897')
$ProgressPreference = 'SilentlyContinue'
$actions = @(   # what build.yml (Luckgram x64) uses, sha = the ref resolved on 2026-11-15
  'actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1',                  # v7
  'actions/cache@55cc8345863c7cc4c66a329aec7e433d2d1c52a9',                     # v6 (cache/restore)
  'Eden-CI/msvc-dev-cmd@1bd71f95d6f3d1b2b3395b335cc63bcdc90cf223',              # master
  'aws-actions/configure-aws-credentials@7474bc4690e29a8392af63c5b98e7449536d5c3a', # v4
  'actions/upload-artifact@cf430e030ddbb5b0abf93d22962f4752f3646cd9'            # v7
)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$bad = 0
foreach ($a in $actions) {
  $repo, $sha = $a -split '@'
  $dir = Join-Path $Cache ($repo -replace '/', '_')
  $zip = Join-Path $dir "$sha.zip"
  New-Item -ItemType Directory -Force $dir | Out-Null
  if (Test-Path $zip) { "have  $a"; continue }
  $tmp = "$zip.part"
  & curl.exe -sS --fail -L --retry 5 --retry-all-errors --retry-delay 10 -x $Proxy -o $tmp "https://codeload.github.com/$repo/zip/$sha"
  $ok = ($LASTEXITCODE -eq 0)
  if ($ok) {  # a zipball with one top folder and an action.yml in it
    try {
      $z = [IO.Compression.ZipFile]::OpenRead($tmp)
      $ok = [bool]($z.Entries | Where-Object { $_.FullName -match '^[^/]+/action\.ya?ml$' })
      $z.Dispose()
    } catch { $ok = $false }
  }
  if ($ok) { Move-Item -Force $tmp $zip; "added $a ($((Get-Item $zip).Length) bytes)" }
  else { Remove-Item -Force $tmp -ErrorAction SilentlyContinue; "FAILED $a"; $bad++ }
}
if ($bad) { exit 1 }
