<#
Package the Linux (Development) build for Cradle from a clean, pushed commit.

  powershell -File tools/package_linux.ps1            # build + tarball + SHA256SUMS in Builds/build-<sha>/
  powershell -File tools/package_linux.ps1 -Publish   # ...and publish GitHub prerelease build-<sha>

Needs: the editor closed, the v26 toolchain (LINUX_MULTIARCH_ROOT) and the Linux target platform (README -> Windows dev setup).
#>
param(
	[switch]$Publish,
	[switch]$AllowDirty,
	[int]$Jobs = 4
)
$ErrorActionPreference = "Stop"
$Repo = (Resolve-Path "$PSScriptRoot\..").Path
$Engine = if ($env:UE_ROOT) { $env:UE_ROOT } else { "C:\Program Files\Epic Games\UE_5.7" }
$Project = "$Repo\Game\FPSAgent\FPSAgent.uproject"

# 1. A build maps to exactly one commit.
$Dirty = git -C $Repo status --porcelain
if ($Dirty -and -not $AllowDirty) { throw "Working tree is not clean. Commit first: a build must map to an exact SHA." }
$FullSha = (git -C $Repo rev-parse HEAD).Trim()
$Sha = $FullSha.Substring(0, 7)
$Tag = "build-$Sha"
$Machine = [Environment]::GetEnvironmentVariable('LINUX_MULTIARCH_ROOT', 'Machine')
if (-not $env:LINUX_MULTIARCH_ROOT -and -not $Machine) { throw "LINUX_MULTIARCH_ROOT is not set: install the v26 toolchain." }
if (-not $env:LINUX_MULTIARCH_ROOT) { $env:LINUX_MULTIARCH_ROOT = $Machine }

$Out = "$Repo\Builds\$Tag"
$Stage = "$Out\stage"
if (Test-Path $Stage) { Remove-Item -Recurse -Force $Stage }
New-Item -ItemType Directory -Force $Out | Out-Null
Write-Host "Packaging $Tag (Linux, Development) -> $Out"

# 2. Compile for Linux, cook, stage, pak, archive.
& "$Engine\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun "-project=$Project" -platform=Linux -clientconfig=Development `
	-build -cook -stage -pak -archive "-archivedirectory=$Stage" -noP4 -utf8output -unattended `
	"-ubtargs=-MaxParallelActions=$Jobs"
if ($LASTEXITCODE -ne 0) { throw "BuildCookRun failed (exit $LASTEXITCODE). See the UAT log above." }

$Root = Get-ChildItem $Stage -Directory | Where-Object { $_.Name -like "Linux*" } | Select-Object -First 1
if (-not $Root) { throw "No Linux* folder under $Stage" }
foreach ($Need in @("FPSAgent.sh", "FPSAgent\Binaries\Linux\FPSAgent")) {
	if (-not (Test-Path "$($Root.FullName)\$Need")) { throw "Missing $Need in the packaged build" }
}

# 3. Build info inside the build, so a running job can always say what it is.
$EngineVer = Get-Content "$Engine\Engine\Build\Build.version" -Raw | ConvertFrom-Json
@"
build: $Tag
commit: $FullSha
engine: $($EngineVer.MajorVersion).$($EngineVer.MinorVersion).$($EngineVer.PatchVersion) (CL $($EngineVer.Changelist))
toolchain: $(Split-Path $env:LINUX_MULTIARCH_ROOT.TrimEnd('\') -Leaf)
config: Development
packaged: $((Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ'))
"@ | Out-File -Encoding ascii "$($Root.FullName)\BUILD_INFO.txt"

# 4. Tarball (with Unix exec bits) + SHA256SUMS (sha256sum -c format).
$TarName = "FPSAgent-Linux-$Sha.tar.gz"
$Tar = "$Out\$TarName"
Push-Location $Repo
try {
	conda run -n fps-agent python tools/package/make_tarball.py $Root.FullName $Tar Linux
	if ($LASTEXITCODE -ne 0) { throw "make_tarball.py failed" }
} finally { Pop-Location }
$Hash = (Get-FileHash $Tar -Algorithm SHA256).Hash.ToLower()
[IO.File]::WriteAllText("$Out\SHA256SUMS", "$Hash  $TarName`n")
$SizeMB = [math]::Round((Get-Item $Tar).Length / 1MB, 1)
Write-Host "Tarball: $Tar ($SizeMB MB), sha256 $Hash"
if ((Get-Item $Tar).Length -gt 2GB) { throw "Tarball exceeds GitHub's 2 GB release asset limit" }

# 5. Optional: publish the prerelease (the commit must already be on origin).
if ($Publish) {
	git -C $Repo fetch -q origin
	$OnRemote = git -C $Repo branch -r --contains $FullSha
	if (-not $OnRemote) { throw "Commit $Sha is not on origin yet: git push first." }
	$Notes = "Linux Development build of $FullSha. See docs/builds.md for what changed and what to test.`n`nVerify: sha256sum -c SHA256SUMS"
	gh release create $Tag $Tar "$Out\SHA256SUMS" --target $FullSha --prerelease --title "Linux build $Sha" --notes $Notes
	if ($LASTEXITCODE -ne 0) { throw "gh release create failed" }
	Write-Host "Published $Tag"
}
