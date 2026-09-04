#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[string]$Filter='RollbackLab',[int]$TimeoutSeconds=240)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'automation'
try {
    $report=Join-Path $context.Run 'automation'
    $arguments=@($context.Project,'-NullRHI',('-ExecCmds=Automation RunTests '+$Filter),
        '-TestExit=Automation Test Queue Empty',('-ReportExportPath='+$report))+(Get-UnrealRuntimeArguments $context)
    Invoke-UnrealProcess -Context $context -FilePath $context.Editor -Name 'automation' -Arguments $arguments -TimeoutSeconds $TimeoutSeconds
    $index=Get-Content -LiteralPath (Join-Path $report 'index.json') -Raw|ConvertFrom-Json
    if($index.failed -ne 0 -or $index.succeeded -lt 1 -or $index.notRun -gt 0){throw "Unreal automation did not pass: $report"}
    Write-Output "UE Automation: $($index.succeeded) passed, $($index.failed) failed. Report: $report"
} finally {$context.Lock.Dispose()}
