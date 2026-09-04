#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[string]$SdkRoot,[string]$Output,[switch]$AllowDirty)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$identity=Get-SdkSourceIdentity
if(-not $identity.Clean -and -not $AllowDirty){throw 'Plugin packaging requires clean source; use -AllowDirty only for intermediate diagnostics.'}
if([string]::IsNullOrEmpty($Output)) {
    $Output='artifacts/ue5-0.2/plugin/0.2.0-'+$identity.Sha.Substring(0,12)
    if(-not $identity.Clean){$Output+='-working'}
}
$package=Get-UnrealPluginOutputPath $Output
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'plugin'
try {
    $null=Stage-RollbackSdk -SdkRoot $SdkRoot
    $plugin=Get-SdkRepositoryPath ($script:UnrealPluginRelative+'/RollbackLabBridge.uplugin')
    Invoke-UnrealProcess -Context $context -FilePath $context.Dotnet -Name 'build-plugin' -Arguments @(
        $context.Uat,'BuildPlugin',"-Plugin=$plugin","-Package=$package",'-HostPlatforms=Win64',
        '-TargetPlatforms=Win64','-NoCompile','-unattended','-utf8output')
    $after=Get-SdkSourceIdentity
    if($after.Sha -cne $identity.Sha -or $after.Clean -ne $identity.Clean){throw 'Source identity changed during plugin packaging.'}
    . (Join-Path $PSScriptRoot 'UnrealEvidence.ps1')
    New-UnrealArtifactPackage -Root $package -Kind plugin -Configuration Development -AllowDirty:$AllowDirty
    Write-Output "BuildPlugin passed Editor Development, Game Development and Shipping. Output: $package; logs: $($context.Run)"
} finally {$context.Lock.Dispose()}
