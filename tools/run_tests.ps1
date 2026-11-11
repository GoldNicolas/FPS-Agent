# Runs Unreal automation tests headless (-nullrhi) and prints a summary. Exit 0 = all passed.
param([string]$Filter = "FPSAgent")
$ErrorActionPreference = "Stop"
$Engine  = if ($env:UE_ROOT) { $env:UE_ROOT } else { "C:\Program Files\Epic Games\UE_5.7" }
$Project = (Resolve-Path (Join-Path $PSScriptRoot "..\Game\FPSAgent\FPSAgent.uproject")).Path
$Report  = Join-Path (Split-Path $Project) "Saved\TestReport"
if (Test-Path $Report) { Remove-Item -Recurse -Force $Report }
& "$Engine\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $Project `
    "-ExecCmds=Automation RunTests $Filter;Quit" "-TestExit=Automation Test Queue Empty" `
    "-ReportExportPath=$Report" -unattended -nopause -nosplash -nullrhi -nosound "-log=FPSAgentTests.log" | Out-Null
$Index = Join-Path $Report "index.json"
if (-not (Test-Path $Index)) { Write-Host "No report. See Game/FPSAgent/Saved/Logs/FPSAgentTests.log"; exit 2 }
$R = Get-Content $Index -Raw | ConvertFrom-Json
foreach ($t in $R.tests) {
    "{0,-8} {1}" -f $t.state, $t.fullTestPath
    foreach ($e in $t.entries) { if ($e.event.type -eq "Error") { "         ! " + $e.event.message } }
}
"Succeeded: $($R.succeeded)  Failed: $($R.failed)  NotRun: $($R.notRun)"
if ($R.failed -gt 0 -or $R.succeeded -eq 0) { exit 1 } else { exit 0 }
