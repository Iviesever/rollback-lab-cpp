#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[string]$SdkRoot,[switch]$Game,
      [ValidateSet('Development','Shipping')][string]$Configuration='Development')
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'demo-build'
$previousExtraArgs=$env:UBT_EXTRA_ARGS
try {
    $null=Stage-RollbackSdk -SdkRoot $SdkRoot
    $target=if($Game){'RollbackArena'}else{'RollbackArenaEditor'}
    if(-not $Game -and $Configuration -ne 'Development'){throw 'Editor build supports Development only.'}
    # Builder-only options go on the top-level command, so WriteMetadata children
    # do not inherit options their mode cannot consume.
    $env:UBT_EXTRA_ARGS=$null
    Invoke-UnrealProcess -Context $context -FilePath $context.Dotnet -Name 'build-demo' -Arguments (@(
        $context.Ubt,$target,'Win64',$Configuration,('-Project='+$context.Project),'-NoHotReloadFromIDE',
        ('-Log='+ (Join-Path $context.Run 'ubt.log')))+$context.UbtArguments)
    Write-Output "Built $target $Configuration; logs: $($context.Run)"
} finally {$env:UBT_EXTRA_ARGS=$previousExtraArgs; $context.Lock.Dispose()}
