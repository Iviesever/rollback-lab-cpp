#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[switch]$IncludeWatchdog)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$cases=@(
    @{Name='zero-frames';Args=@('-RollbackLabFrames=0');Reason='bounded demo contract'},
    @{Name='seed-suffix';Args=@('-RollbackLabScenarioSeed=1,garbage');Reason='Expected unsigned decimal'},
    @{Name='seed-overflow';Args=@('-RollbackLabScenarioSeed=18446744073709551616');Reason='Numeric option overflow'},
    @{Name='early-exit';Args=@('-RollbackLabExitAfterSeconds=1');Reason='before smoke completion'},
    @{Name='wrong-source';Args=@('-RollbackLabGitSha=0000000000000000000000000000000000000000');Reason='differs from loaded SDK'}
)
if($IncludeWatchdog){$cases+=@{Name='screenshot-watchdog';Args=@('-NullRHI');Reason='watchdog expired'}}
$results=@()
foreach($case in $cases) {
    $context=New-UnrealContext -EngineRoot $EngineRoot -Role ('negative-'+$case.Name)
    try {
        $trace=Join-Path $context.Run 'ue-trace.json'
        $arguments=@($context.Project,'-game','-windowed','-ResX=1600','-ResY=900','-RollbackLabSmoke',
            ('-RollbackLabTrace='+$trace),('-RollbackLabCaptureDir='+(Join-Path $context.Run 'captures')))+
            $case.Args+(Get-UnrealRuntimeArguments $context)
        $rejected=$false
        try {
            Invoke-UnrealProcess -Context $context -FilePath (Join-Path $context.Engine 'Engine/Binaries/Win64/UnrealEditor.exe') `
                -Name 'negative' -Arguments $arguments -TimeoutSeconds 120
        } catch {$rejected=$true}
        $process=Get-Content -LiteralPath (Join-Path $context.Run 'negative.process.json') -Raw|ConvertFrom-Json
        if(-not $rejected -or $process.exit_code -ne 1 -or $process.timed_out -or -not $process.all_children_exited) {
            throw "Negative case $($case.Name) did not return explicit failure with clean process teardown."
        }
        $failure=Get-Content -LiteralPath $trace -Raw|ConvertFrom-Json
        if($failure.success -ne $false -or $failure.failure_reason -notlike ('*'+$case.Reason+'*')) {
            throw "Negative case $($case.Name) has missing or incorrect failure evidence."
        }
        $results+=[ordered]@{case=$case.Name;exit_code=$process.exit_code;failure_reason=$failure.failure_reason;
            run=[IO.Path]::GetRelativePath($script:SdkRepositoryRoot,$context.Run).Replace('\','/')}
    } finally {$context.Lock.Dispose()}
}
$results|ConvertTo-Json -Depth 5
