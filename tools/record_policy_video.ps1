<#
Records an MP4 of a trained combat policy, or of the scripted reference agent (playback only: no training, hard rule 6).

  powershell -File tools/record_policy_video.ps1 -Snapshots <folder with encoder/policy/decoder.bin> [-Stage S0|S1] [-Episodes 10] [-View Agent|TopDown]
  powershell -File tools/record_policy_video.ps1 -Scripted -Stage S1 [-View TopDown]   # the -Scripted reference, no snapshots

How it works:
- Runs the editor build as a game window with -Stage=<S0|S1> -Eval: fixed seeds, mean actions, no ramp. The game exits after -Episodes.
- -dumpmovie saves every rendered frame. Headless playback uses the fixed 60 Hz step (D-019), so every frame is exactly 1/60 s of game time.
  The 60 fps video therefore plays in real time, however fast or slow the laptop renders.
- -View Agent looks through arena 0's agent (fps.WatchAgent 0): its own aim, with the HUD crosshair, ammo and hit markers.
  -View TopDown looks down at the whole arena (fps.TopDown 1).
- Shots are drawn as tracers (fps.ShowShots 1): red = hit a bean, yellow = miss.
- ffmpeg encodes H.264 into Game/FPSAgent/Saved/Videos/, which git ignores. The frames are deleted afterwards (-KeepFrames keeps them).

Needs: the editor closed, a built editor target (tools/build.ps1), ffmpeg on PATH, and about 2 MB of free disk per 1080p frame while recording.

Speed: frame capture is slow, about 1.2 s of wall time per 1080p frame on the dev laptop (2026-11-13). So 1 s of video (60 frames)
takes about 70 s, and a 14-episode video (about 60 s) takes about 70 min. If -TimeoutMinutes runs out, the game is stopped
and the frames written so far are still encoded.
#>
param(
	[string]$Snapshots = '',
	[ValidateSet('S0', 'S1')][string]$Stage = 'S0',
	# The -Scripted reference agent instead of a policy (then -Snapshots isn't needed).
	[switch]$Scripted,
	[int]$Episodes = 10,
	[int]$Seed = 1000,
	[double]$StartDifficulty = 1.0,
	[ValidateSet('Agent', 'TopDown')][string]$View = 'Agent',
	[int]$Width = 1920,
	[int]$Height = 1080,
	[string]$Title = '',
	# Settings overrides passed to the game as -Set= (D-025), e.g. "Curriculum.S0.bAgentCanMove=true". Use the same values the snapshot trained with.
	[string]$Set = '',
	[string]$Out = '',
	[int]$TimeoutMinutes = 90,
	[switch]$KeepFrames
)
$ErrorActionPreference = 'Stop'
$Repo = Split-Path -Parent $PSScriptRoot
$Project = Join-Path $Repo 'Game\FPSAgent\FPSAgent.uproject'
$Saved = Join-Path $Repo 'Game\FPSAgent\Saved'
$Engine = if ($env:UE_ROOT) { $env:UE_ROOT } else { 'C:\Program Files\Epic Games\UE_5.7' }
$Editor = Join-Path $Engine 'Engine\Binaries\Win64\UnrealEditor.exe'

if ($Scripted) {
	$Name = "scripted-$Stage"
} else {
	if (-not $Snapshots) { throw 'give -Snapshots <folder>, or -Scripted' }
	$Snapshots = (Resolve-Path $Snapshots).Path
	foreach ($f in 'encoder.bin', 'policy.bin', 'decoder.bin') {
		if (-not (Test-Path (Join-Path $Snapshots $f))) { throw "missing $f in $Snapshots" }
	}
	$Name = Split-Path $Snapshots -Leaf
}
if (-not (Get-Command ffmpeg -ErrorAction SilentlyContinue)) { throw 'ffmpeg not found on PATH' }
if (Get-Process UnrealEditor* -ErrorAction SilentlyContinue) { throw 'close the Unreal editor first' }

if (-not $Out) {
	$Out = Join-Path (Join-Path $Saved 'Videos') ("{0}-{1}-seed{2}.mp4" -f $Name, $View.ToLower(), $Seed)
}
if (-not [System.IO.Path]::IsPathRooted($Out)) { $Out = Join-Path (Get-Location) $Out }
$Out = [System.IO.Path]::GetFullPath($Out)
New-Item -ItemType Directory -Force (Split-Path -Parent $Out) | Out-Null

# Remove frames left over from earlier recordings so the numbering starts clean.
$ShotRoot = Join-Path $Saved 'Screenshots'
if (Test-Path $ShotRoot) { Get-ChildItem $ShotRoot -Recurse -Filter 'MovieFrame*' | Remove-Item -Force }

$ViewCmd = if ($View -eq 'Agent') { 'fps.WatchAgent 0' } else { 'fps.TopDown 1' }
$Log = Join-Path $Saved ("Logs\record-{0}.log" -f $Name)
$GameArgs = @(
	"`"$Project`"", '-game', '-windowed', "-ResX=$Width", "-ResY=$Height", '-nosound', '-unattended', '-dumpmovie',
	"-Stage=$Stage", '-Eval', "-EvalEpisodes=$Episodes", '-Arenas=1', "-Seed=$Seed", "-StartDifficulty=$StartDifficulty", "-abslog=`"$Log`"",
	# Readability: no motion blur (the agent turns up to 720 deg/s) and no auto-exposure flicker between sky and walls.
	"-ExecCmds=`"$ViewCmd, fps.ShowShots 1, r.MotionBlurQuality 0, r.EyeAdaptationQuality 0`""
)
if ($Scripted) { $GameArgs += '-Scripted' } else { $GameArgs += "-ResumeFrom=`"$Snapshots`"" }
if ($Set) { $GameArgs += "-Set=`"$Set`"" }
Write-Host "Recording $Episodes $Stage episodes of $Name ($View view, seed $Seed) ..."
$p = Start-Process $Editor -ArgumentList $GameArgs -PassThru
if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
	Stop-Process -Id $p.Id -Force
	Start-Sleep -Seconds 2
	Write-Warning "stopped the game after $TimeoutMinutes min; encoding the frames written so far (log: $Log)"
}
Select-String $Log -Pattern 'CombatEval:|Combat: exiting|fps.WatchAgent' | ForEach-Object { $_.Line.Substring([Math]::Max(0, $_.Line.IndexOf('Log'))) }

$Frames = Get-ChildItem $ShotRoot -Recurse -Filter 'MovieFrame*.png' | Sort-Object Name
if ($Frames.Count -eq 0) { throw "no frames were written under $ShotRoot (log: $Log)" }
$FrameDir = $Frames[0].DirectoryName
$First = [int]($Frames[0].BaseName -replace '^MovieFrame', '')
Write-Host ("{0} frames ({1:N1} s at 60 fps) in {2}" -f $Frames.Count, ($Frames.Count / 60.0), $FrameDir)

$Filter = "scale=${Width}:${Height}:flags=lanczos,format=yuv420p"
if ($Title) {
	$Font = 'C\:/Windows/Fonts/arial.ttf'
	$Text = $Title -replace "'", '' -replace ':', '\:'
	$Filter = "$Filter,drawtext=fontfile='$Font':text='$Text':x=24:y=24:fontsize=28:fontcolor=white:box=1:boxcolor=black@0.45:boxborderw=8"
}
& ffmpeg -hide_banner -loglevel error -y -framerate 60 -start_number $First -i (Join-Path $FrameDir 'MovieFrame%05d.png') `
	-vf $Filter -c:v libx264 -preset slow -crf 18 -movflags +faststart $Out
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed (frames kept in $FrameDir)" }
if (-not $KeepFrames) { $Frames | Remove-Item -Force }
$Size = (Get-Item $Out).Length / 1MB
Write-Host ("Video: {0} ({1:N1} MB)" -f $Out, $Size)
