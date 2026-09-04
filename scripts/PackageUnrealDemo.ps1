#requires -Version 7.0
param([Parameter(Mandatory)][string]$EngineRoot,[string]$SdkRoot,[string]$Output,
      [ValidateSet('Development','Shipping')][string]$Configuration='Shipping',[switch]$AllowDirty)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$identity=Get-SdkSourceIdentity
if(-not $identity.Clean -and -not $AllowDirty){throw 'Demo packaging requires clean source; use -AllowDirty only for intermediate diagnostics.'}
if([string]::IsNullOrEmpty($Output)) {
    $label='0.2.0-'+$identity.Sha.Substring(0,12)+'-'+$Configuration.ToLowerInvariant()
    if(-not $identity.Clean){$label+='-working'}
    $Output='artifacts/ue5-0.2/demo/'+$label
}
$package=Get-UnrealDemoOutputPath $Output
# Reuse the normal Editor build and content-generator entry points, serially.
& (Join-Path $PSScriptRoot 'BuildUnrealDemo.ps1') -EngineRoot $EngineRoot -SdkRoot $SdkRoot
& (Join-Path $PSScriptRoot 'GenerateUnrealContent.ps1') -EngineRoot $EngineRoot
$context=New-UnrealContext -EngineRoot $EngineRoot -Role 'package-demo'
$previousExtraArgs=$env:UBT_EXTRA_ARGS
try {
    if(Test-Path -LiteralPath $package) {
        $resolved=(Resolve-Path -LiteralPath $package).Path
        if($resolved -ine $package){throw 'Demo archive target resolution mismatch.'}
        # The complete absolute target has passed the dedicated directory/reparse guards.
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $package -Force|Out-Null
    $env:UBT_EXTRA_ARGS=$null
    $cookerOptions=(Get-UnrealRuntimeArguments $context) -join ' '
    $ubtOptions=$context.UbtArguments -join ' '
    Invoke-UnrealProcess -Context $context -FilePath $context.Dotnet -Name 'build-cook-run' -TimeoutSeconds 3600 -Arguments @(
        $context.Uat,'BuildCookRun',('-project='+$context.Project),'-target=RollbackArena','-platform=Win64',
        ('-clientconfig='+$Configuration),'-build','-cook','-stage','-pak','-archive',
        ('-archivedirectory='+$package),('-stagingdirectory='+(Join-Path $context.Run 'stage')),
        '-map=/Game/Maps/RollbackArena','-NoP4','-NoCompile','-NoCompileEditor','-unattended','-utf8output',
        '-nodebuginfo','-ddc=(Local)',('-UbtArgs='+$ubtOptions),('-AdditionalCookerOptions='+$cookerOptions))
    $after=Get-SdkSourceIdentity
    if($after.Sha -cne $identity.Sha -or $after.Clean -ne $identity.Clean){throw 'Source identity changed during packaging.'}
    foreach($required in @('Windows/RollbackArena.exe',
        'Windows/RollbackArena/Plugins/RollbackLabBridge/Binaries/ThirdParty/RollbackLab/bin/rollback_lab_c.dll',
        'Windows/RollbackArena/Plugins/RollbackLabBridge/Binaries/ThirdParty/RollbackLab/manifest.json')) {
        if(-not(Test-Path -LiteralPath (Join-Path $package $required) -PathType Leaf)){throw "Package omitted $required"}
    }
    # Artifact creation is kept separate from UAT; it verifies the whole immutable tree.
    . (Join-Path $PSScriptRoot 'UnrealEvidence.ps1')
    New-UnrealArtifactPackage -Root $package -Kind demo -Configuration $Configuration -AllowDirty:$AllowDirty
    Write-Output "BuildCookRun passed; package: $package; logs: $($context.Run)"
} finally {$env:UBT_EXTRA_ARGS=$previousExtraArgs;$context.Lock.Dispose()}
