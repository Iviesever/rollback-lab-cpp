#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'generate-content'
try {
    Invoke-UnrealProcess -Context $context -FilePath $context.Editor -Name 'generate-content' -Arguments (
        @($context.Project,'-run=RollbackArenaBuildContent','-NullRHI')+(Get-UnrealRuntimeArguments $context))
    foreach($asset in @('Content/Maps/RollbackArena.umap','Content/Materials/M_RollbackTint.uasset')) {
        $file=Get-SdkRepositoryPath ('examples/ue5/RollbackArena/'+$asset)
        if(-not(Test-Path -LiteralPath $file -PathType Leaf) -or (Get-Item -LiteralPath $file).Length -eq 0) {
            throw "Native content generator did not materialize $asset"
        }
    }
    Write-Output "Native arena map and Tint material generated; logs: $($context.Run)"
} finally {$context.Lock.Dispose()}
