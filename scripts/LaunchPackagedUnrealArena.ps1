#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[Parameter(Mandatory)][string]$DemoRoot,
      [switch]$Smoke,[switch]$AutoDemo,[switch]$Desync,[switch]$AllowDirty,
      [string]$ScenarioSeed='12648430',[string]$TransportSeed='5351397',[uint32]$Frames=240,
      [ValidateRange(0,7200)][int]$ExitAfterSeconds=0)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
. (Join-Path $PSScriptRoot 'UnrealEvidence.ps1')
$identity=Get-SdkSourceIdentity
$root=Get-UnrealDemoOutputPath $DemoRoot
$null=Test-UnrealArtifactPackage -Root $root -Kind demo -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
if($Desync -and -not $PSBoundParameters.ContainsKey('ScenarioSeed')){$ScenarioSeed='1'}
foreach($seed in @($ScenarioSeed,$TransportSeed)) {
    [uint64]$parsed=0
    if($seed -cnotmatch '^[0-9]+$' -or -not [uint64]::TryParse($seed,[ref]$parsed)){throw 'Seeds must be complete unsigned 64-bit decimal integers.'}
}
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'packaged-arena'
try {
    $arguments=@('-windowed','-ResX=1600','-ResY=900','-ForceRes',
        ('-RollbackLabScenarioSeed='+$ScenarioSeed),('-RollbackLabTransportSeed='+$TransportSeed),
        ('-RollbackLabGitSha='+$identity.Sha))+(Get-UnrealRuntimeArguments $context)
    if($Smoke) {
        $arguments+=@('-RollbackLabSmoke',('-RollbackLabFrames='+$Frames),
            ('-RollbackLabTrace='+(Join-Path $context.Run 'ue-trace.json')),
            ('-RollbackLabCaptureDir='+(Join-Path $context.Run 'captures')))
    } elseif($Desync){$arguments+='-RollbackLabDesync'}
    elseif($AutoDemo){$arguments+=@('-RollbackLabAuto',('-RollbackLabFrames='+$Frames))}
    if($ExitAfterSeconds -gt 0){$arguments+='-RollbackLabExitAfterSeconds='+$ExitAfterSeconds}
    $timeout=if($Smoke){240}else{[Math]::Max(240,$ExitAfterSeconds+120)}
    Invoke-UnrealProcess -Context $context -FilePath (Join-Path $root 'Windows/RollbackArena.exe') `
        -Name 'packaged-arena' -Arguments $arguments -Visible -TimeoutSeconds $timeout
    if($Smoke) {
        $null=Test-UnrealSmokeEvidence -SmokeRun $context.Run -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
        $trace=Get-Content -LiteralPath (Join-Path $context.Run 'ue-trace.json') -Raw|ConvertFrom-Json
        if([uint64]$trace.scenario_seed -ne [uint64]$ScenarioSeed -or [uint64]$trace.transport_seed -ne [uint64]$TransportSeed -or
           $trace.target_frame -ne $Frames){throw 'Packaged smoke did not run the requested scenario.'}
    }
    Write-Output "Packaged arena exited successfully; artifacts: $($context.Run)"
} finally {$context.Lock.Dispose()}
