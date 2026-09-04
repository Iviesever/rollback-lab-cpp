#requires -Version 7.0
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/UnrealCommon.ps1')
$failures=[Collections.Generic.List[string]]::new()
foreach($path in @('docs','src','tests','artifacts/ue5-0.2','artifacts/ue5-0.2/plugin/../../../docs')) {
    $rejected=$false
    try {$null=Get-UnrealPluginOutputPath $path} catch {$rejected=$true}
    if(-not $rejected){$failures.Add("Unsafe plugin output accepted: $path")}
}
$valid=Get-SdkRepositoryPath 'artifacts/ue5-0.2/plugin'
if((Get-UnrealPluginOutputPath $valid) -cne $valid){$failures.Add('Valid dedicated plugin output rejected.')}
$testRoot=Get-SdkRepositoryPath 'artifacts/ue5-0.2/plugin/path-guard-test'
$target=Get-SdkRepositoryPath 'artifacts/ue5-0.2/path-guard-target'
New-Item -ItemType Directory -Path $testRoot,$target -Force|Out-Null
$link=Join-Path $testRoot 'redirect'
try {
    if(-not(Test-Path -LiteralPath $link)){New-Item -ItemType Junction -Path $link -Target $target|Out-Null}
    $rejected=$false
    try {$null=Get-UnrealPluginOutputPath (Join-Path $link 'child')} catch {$rejected=$true}
    if(-not $rejected){$failures.Add('Plugin output through junction accepted.')}
} finally {
    if(Test-Path -LiteralPath $link){Remove-Item -LiteralPath $link -Force}
}
$identity=Get-SdkSourceIdentity
$source=Get-SdkRepositoryPath ('artifacts/sdk/0.2.0-'+$identity.Sha.Substring(0,12)+'/install')
$destination=Get-SdkRepositoryPath ($script:UnrealPluginRelative+'/Binaries/ThirdParty/RollbackLab')
try {
    $null=Stage-RollbackSdk -SdkRoot $source
    $before=(Get-FileHash -LiteralPath (Join-Path $destination 'bin/rollback_lab_c.dll')).Hash
    try {
        $null=Stage-RollbackSdk -SdkRoot $destination
        $after=(Get-FileHash -LiteralPath (Join-Path $destination 'bin/rollback_lab_c.dll')).Hash
        if($after -cne $before){$failures.Add('Same-root staging changed verified SDK payload.')}
    } catch {$failures.Add('Same-root staging destroyed or rejected valid staged SDK: '+$_.Exception.Message)}
} finally {
    # Restore only this known generated staging directory from the immutable SDK.
    $null=Stage-RollbackSdk -SdkRoot $source
}
$evidence=Get-SdkRepositoryPath 'artifacts/ue5-0.2/path-tests.json'
[ordered]@{failure_count=$failures.Count;failures=@($failures);same_root_dll_sha256=$before}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath $evidence
if($failures.Count){$failures|ForEach-Object{Write-Output $_};throw "$($failures.Count) Unreal path tests failed."}
Write-Output 'Unreal path guard and verified same-root staging tests passed.'
