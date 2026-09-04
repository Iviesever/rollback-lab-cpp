#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[string]$SdkRoot,[string]$Output='artifacts/ue5-0.2/plugin')
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$package=Get-UnrealPluginOutputPath $Output
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'plugin'
try {
    $null=Stage-RollbackSdk -SdkRoot $SdkRoot
    $plugin=Get-SdkRepositoryPath ($script:UnrealPluginRelative+'/RollbackLabBridge.uplugin')
    Invoke-UnrealProcess -Context $context -FilePath $context.Dotnet -Name 'build-plugin' -Arguments @(
        $context.Uat,'BuildPlugin',"-Plugin=$plugin","-Package=$package",'-HostPlatforms=Win64',
        '-TargetPlatforms=Win64','-NoDeleteHostProject','-NoCompile','-unattended','-utf8output')
    Write-Output "BuildPlugin passed Editor Development, Game Development and Shipping. Output: $package; logs: $($context.Run)"
} finally {$context.Lock.Dispose()}
