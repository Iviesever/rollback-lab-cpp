#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[switch]$Smoke,[switch]$AutoDemo,[switch]$Desync,[switch]$Warmup,
      [string]$ScenarioSeed='12648430',[string]$TransportSeed='5351397',
      [uint32]$Frames=240,[int]$ExitAfterSeconds=0,[string]$CaptureDirectory,[string]$TracePath)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
foreach($seed in @($ScenarioSeed,$TransportSeed)) {
    [uint64]$parsed=0
    if($seed -cnotmatch '^[0-9]+$' -or -not [uint64]::TryParse($seed,[ref]$parsed)){throw 'Seeds must be complete unsigned 64-bit decimal integers.'}
}
if($Warmup -and $Smoke){throw 'Cold shader warmup is separate from the bounded parity smoke.'}
if($Warmup){$AutoDemo=$true;$ExitAfterSeconds=2}
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'editor-arena'
try {
    $identity=Get-SdkSourceIdentity
    if($Desync -and -not $PSBoundParameters.ContainsKey('ScenarioSeed')){$ScenarioSeed='1'}
    if($Smoke) {
        if([string]::IsNullOrEmpty($CaptureDirectory)){$CaptureDirectory=Join-Path $context.Run 'captures'}
        if([string]::IsNullOrEmpty($TracePath)){$TracePath=Join-Path $context.Run 'ue-trace.json'}
    }
    $arguments=@($context.Project,'-game','-windowed','-ResX=1600','-ResY=900','-ForceRes',
        ('-RollbackLabScenarioSeed='+$ScenarioSeed),('-RollbackLabTransportSeed='+$TransportSeed),
        ('-RollbackLabGitSha='+$identity.Sha))+(Get-UnrealRuntimeArguments $context)
    if($Smoke){$arguments+='-RollbackLabSmoke';$arguments+='-RollbackLabFrames='+$Frames}
    elseif($Desync){$arguments+='-RollbackLabDesync'}
    elseif($AutoDemo){$arguments+='-RollbackLabAuto';$arguments+='-RollbackLabFrames='+$Frames}
    if($ExitAfterSeconds -gt 0){$arguments+='-RollbackLabExitAfterSeconds='+$ExitAfterSeconds}
    if(-not [string]::IsNullOrEmpty($CaptureDirectory)){$arguments+='-RollbackLabCaptureDir='+(Get-SdkRepositoryPath $CaptureDirectory)}
    if(-not [string]::IsNullOrEmpty($TracePath)){$arguments+='-RollbackLabTrace='+(Get-SdkRepositoryPath $TracePath)}
    $editor=Join-Path $context.Engine 'Engine/Binaries/Win64/UnrealEditor.exe'
    $timeout=if($Warmup){900}else{240}
    Invoke-UnrealProcess -Context $context -FilePath $editor -Name 'editor-arena' -Arguments $arguments -Visible -TimeoutSeconds $timeout
    if($Smoke) {
        $trace=Get-Content -LiteralPath (Get-SdkRepositoryPath $TracePath) -Raw|ConvertFrom-Json
        if($trace.success -ne $true -or $trace.source_git_sha -cne $identity.Sha -or
           [uint64]$trace.scenario_seed -ne [uint64]$ScenarioSeed -or [uint64]$trace.transport_seed -ne [uint64]$TransportSeed -or
           $trace.confirmed_a -ne $Frames -or $trace.confirmed_b -ne $Frames -or
           $trace.final_hash_a -cne $trace.final_hash_b -or $trace.rollback_count -lt 1 -or
           $trace.replay_verified -ne $true){throw 'Editor smoke trace did not satisfy confirmed parity.'}
    }
    Write-Output "Editor arena exited successfully; artifacts: $($context.Run)"
} finally {$context.Lock.Dispose()}
