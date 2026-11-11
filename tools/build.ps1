# Builds the FPSAgentEditor target. The editor must NOT be running.
# -Jobs caps parallel compile actions: each editor compile can take 2+ GB, so on a 16 GB machine
# the default (one per CPU thread) runs out of memory. Raise it on machines with more RAM.
param([string]$Config = "Development", [int]$Jobs = 4)
$ErrorActionPreference = "Stop"
$Engine  = if ($env:UE_ROOT) { $env:UE_ROOT } else { "C:\Program Files\Epic Games\UE_5.7" }
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\Game\FPSAgent\FPSAgent.uproject")).Path
& "$Engine\Engine\Build\BatchFiles\Build.bat" FPSAgentEditor Win64 $Config "-Project=$Project" -WaitMutex -NoHotReloadFromIDE "-MaxParallelActions=$Jobs"
exit $LASTEXITCODE
