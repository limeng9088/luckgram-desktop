# Self-hosted Windows runner (luckgram-win)

`.github/workflows/build.yml` runs the **Luckgram x64** job on
`[self-hosted, luckgram-win]` when the repository variable `WIN_RUNNER` is
`self-hosted` (Settings → Secrets and variables → Actions → Variables).
Delete the variable (or set anything else) to go back to GitHub hosted runners.

On the self-hosted runner:

- Only pushes to this repository, manual runs, and PRs whose branch is in this
  repository run. PRs from forks skip the job (the repository is public).
- `TBuild` (libraries, ~60 GB with Qt Debug + Release) and `out/` stay in the
  runner workspace between runs, so later runs are incremental. No cache upload.
- The **Libraries** job is skipped; **Luckgram x64** runs `prepare.py` itself
  (about a minute when nothing changed), so libraries and app are always built
  on the same machine. Several machines can carry the `luckgram-win` label
  (aws-win, aws-win-2); each has its own `TBuild`, and a new machine builds the
  libraries once (~70 min on r7i.2xlarge).
- The workflow never deletes anything outside its workspace and never edits the
  machine's CMake or Visual Studio. A missing component fails the job with
  its name.

## Machine setup

Install system-wide (so the runner account sees them on `PATH`):

1. **Visual Studio 2022 Build Tools** with:
   - Desktop development with C++ (`Microsoft.VisualStudio.Workload.VCTools`)
   - MSVC v143 x64/x86 build tools **14.44** (`Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64`)
   - C++ ATL for v143 build tools **14.44** (`Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL`)
   - Windows 11 SDK **10.0.26100** (`Microsoft.VisualStudio.Component.Windows11SDK.26100`)
   - C++ CMake tools for Windows (`Microsoft.VisualStudio.Component.VC.CMake.Project`, provides CMake + Ninja)

   ```bat
   vs_BuildTools.exe --quiet --wait --norestart ^
     --add Microsoft.VisualStudio.Workload.VCTools ^
     --add Microsoft.VisualStudio.Component.VC.14.44.17.14.x86.x64 ^
     --add Microsoft.VisualStudio.Component.VC.14.44.17.14.ATL ^
     --add Microsoft.VisualStudio.Component.Windows11SDK.26100 ^
     --add Microsoft.VisualStudio.Component.VC.CMake.Project
   ```
2. **Python 3.10+** (add to PATH, for all users).
3. **Git for Windows** with "Git from the command line and also from 3rd-party
   software" (puts `bash` on PATH; `shell: bash` steps need it).
   Then `git config --system core.longpaths true`.
4. **PowerShell 7** (`pwsh`; `shell: pwsh` steps need it).
5. **Inno Setup 6.5+** (installer step; Chocolatey is not required).
6. Long paths: `reg add HKLM\SYSTEM\CurrentControlSet\Control\FileSystem /v LongPathsEnabled /t REG_DWORD /d 1 /f`.

`prepare.py` downloads msys2 (perl, nasm), NuGet and jom itself.

## Runner registration

Use a short folder on the big disk so build paths stay short, e.g. `E:\r`:

```bat
mkdir E:\r && cd /d E:\r
rem download + extract actions-runner-win-x64-*.zip here, then:
config.cmd --url https://github.com/limeng9088/luckgram-desktop --token <token> ^
  --name xs-master --labels luckgram-win --work _work --runasservice
```

Settings → Actions → General → "Fork pull request workflows": keep
"Require approval for all external contributors".

## Action archive cache (no downloads in "Set up job")

xs-master sometimes cannot reach codeload.github.com through the proxy
(runs 43685512381, 43686133298: "The SSL connection could not be
established", then a 100 s timeout, the job fails before it starts). The
runner can take the action archives from a local folder instead
(`ACTIONS_RUNNER_ACTION_ARCHIVE_CACHE`, actions/runner `ActionManager.cs`):

```
<cache>\<owner>_<repo>\<sha>.zip      zipball of the action repository at <sha>
E:\r\action-archive-cache\actions_checkout\3d3c42e5aac5ba805825da76410c181273ba90b1.zip
```

`<sha>` is the commit the ref in build.yml resolves to (the "Set up job"
log prints it: `Download action repository 'actions/checkout@v7'
(SHA:...)`). A missing archive is not an error, the runner downloads it as
before. Resolving the refs ("Getting action download info") still needs
the network; only the archive downloads go away.

1. When the network is good, fill the cache (re-running it is harmless,
   it only fetches what is missing):

   ```
   powershell -NoProfile -File luckgram\windows\prepare_action_cache.ps1 -Cache E:\r\action-archive-cache
   ```

2. Once: add this line to `E:\r\.env` and restart the runner service while
   no job runs (`Restart-Service actions.runner.limeng9088-luckgram-desktop.xs-master`):

   ```
   ACTIONS_RUNNER_ACTION_ARCHIVE_CACHE=E:\r\action-archive-cache
   ```

3. Check: the next job's `E:\r\_diag\Worker_*.log` has
   `Found action archive 'E:\r\action-archive-cache\...zip' in cache directory`
   for each action.

When build.yml gets a new action, or a tag / branch it uses moves (the
SHA in the "Set up job" log changes), add the new `owner/repo@sha` to the
list in prepare_action_cache.ps1 and run it again.
