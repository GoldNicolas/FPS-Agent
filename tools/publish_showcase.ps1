<#
Publishes the showcase branch `main` from `develop` (D-035). It is never merged: each run rebuilds main's tree from the
current develop commit.

Kept: the code, tools, cluster scripts, docs/media/ and docs/maps/, plus the public README (tools/showcase/README.md).
Dropped: every other markdown, the rest of docs/, .mcp.json, the Unreal-MCP plugin entry in the .uproject (the plugin
isn't in git), and these publishing tools. main's .gitignore also gets tools/showcase/gitignore-extra.
The commit message is neutral, with no co-author trailer (user, 2026-11-14).

  powershell -File tools/publish_showcase.ps1                 # build the showcase commit locally and print it (no push)
  powershell -File tools/publish_showcase.ps1 -Push           # ...and push it to origin/main (fast-forward)
  powershell -File tools/publish_showcase.ps1 -Initial -Push  # ONE TIME (2026-11-14, user's OK): replace main with a
                                                              # fresh single commit (force-push)

Run it on a clean develop that is pushed. Without -Push, the result is left for inspection in $env:TEMP\fpsagent-showcase.
#>
param([switch]$Push, [switch]$Initial)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $PSScriptRoot
function Git { & git -C $Repo @args; if ($LASTEXITCODE -ne 0) { throw "git $args failed" } }
function GitW { & git -C $Work @args; if ($LASTEXITCODE -ne 0) { throw "git (showcase) $args failed" } }
# Best-effort git (cleanup): errors ignored. PowerShell 5.1 turns redirected native stderr into errors under Stop.
function GitQuiet { $old = $ErrorActionPreference; $ErrorActionPreference = 'Continue'; & git -C $Repo @args 2>&1 | Out-Null; $ErrorActionPreference = $old }

if ((git -C $Repo rev-parse --abbrev-ref HEAD).Trim() -ne 'develop') { throw 'run this on develop' }
if (git -C $Repo status --porcelain) { throw 'commit develop first: the showcase must map to an exact develop commit' }
Git fetch -q origin
if ((git -C $Repo rev-parse HEAD).Trim() -ne (git -C $Repo rev-parse origin/develop).Trim()) { throw 'push develop first' }
$Src = (git -C $Repo rev-parse --short HEAD).Trim()

$Temp = [IO.Path]::GetTempPath()
$Work = Join-Path $Temp 'fpsagent-showcase'
$Tar = Join-Path $Temp 'fpsagent-showcase.tar'
if (Test-Path $Work) { GitQuiet worktree remove --force $Work; if (Test-Path $Work) { Remove-Item -Recurse -Force $Work } }
Git worktree prune
GitQuiet branch -q -D showcase-publish

if ($Initial) {
	Git worktree add -q --detach $Work HEAD
	GitW checkout -q --orphan showcase-publish
} else {
	Git worktree add -q -b showcase-publish $Work origin/main
}
# Replace the worktree's files with develop's tree (git archive -> tar; no binary piping through PowerShell).
GitW rm -rq --cached --ignore-unmatch .
Get-ChildItem $Work -Force | Where-Object { $_.Name -ne '.git' } | Remove-Item -Recurse -Force
Git archive -o $Tar HEAD
& tar -xf $Tar -C $Work; if ($LASTEXITCODE -ne 0) { throw 'tar failed' }
Remove-Item $Tar -Force

# Leave out the internal parts.
foreach ($p in 'CLAUDE.md', 'PLAN.md', '.mcp.json', 'tools/publish_showcase.ps1', 'tools/showcase') {
	$f = Join-Path $Work $p; if (Test-Path $f) { Remove-Item -Recurse -Force $f }
}
Get-ChildItem (Join-Path $Work 'docs') -Force | Where-Object { $_.Name -notin 'media', 'maps' } | Remove-Item -Recurse -Force
Get-ChildItem $Work -Recurse -Force -Filter *.md | Where-Object { $_.FullName -notmatch '\\\.git\\' } | Remove-Item -Force
Copy-Item (Join-Path $Repo 'tools/showcase/README.md') (Join-Path $Work 'README.md') -Force
Add-Content (Join-Path $Work '.gitignore') (Get-Content (Join-Path $Repo 'tools/showcase/gitignore-extra') -Raw) -NoNewline
$Up = Join-Path $Work 'Game/FPSAgent/FPSAgent.uproject'
$Text = [IO.File]::ReadAllText($Up)
$Stripped = [regex]::Replace($Text, ',\s*\{\s*"Name":\s*"UnrealMCP"[^}]*\}', '')
if ($Stripped -eq $Text) { Write-Warning 'no UnrealMCP plugin entry found in the .uproject' }
[IO.File]::WriteAllText($Up, $Stripped)

GitW add -A
if (-not $Initial -and -not (git -C $Work status --porcelain)) {
	Write-Host "main already matches develop $Src; nothing to publish."
} else {
	GitW commit -q -m "Showcase update $(Get-Date -Format yyyy-MM-dd) (development commit $Src)"
	& git -C $Work show --stat --format='%h %s' HEAD | Select-Object -Last 12
	Write-Host ("{0} files in the showcase tree" -f (git -C $Work ls-files | Measure-Object -Line).Lines)
	if ($Push) {
		if ($Initial) { GitW push --force origin showcase-publish:main } else { GitW push origin showcase-publish:main }
		Git fetch -q origin
		Git branch -f main origin/main
		Write-Host "Published main = develop $Src."
	} else {
		Write-Host "Not pushed. Inspect $Work; run again with -Push (or -Initial -Push once) to publish."
		return
	}
}
GitQuiet worktree remove --force $Work
GitQuiet branch -q -D showcase-publish
