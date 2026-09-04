#requires -Version 7.0
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/UnrealCommon.ps1')
$null=Get-Command Get-UnrealDemoOutputPath -ErrorAction Stop
foreach($path in @('docs','examples/ue5/RollbackArena','artifacts/ue5-0.2','artifacts/ue5-0.2/demo/../../../docs')) {
    $rejected=$false
    try {$null=Get-UnrealDemoOutputPath $path} catch {$rejected=$true}
    if(-not $rejected){throw "Unsafe demo output accepted: $path"}
}
$valid=Get-SdkRepositoryPath 'artifacts/ue5-0.2/demo/path-test'
if((Get-UnrealDemoOutputPath $valid) -cne $valid){throw 'Dedicated demo output rejected.'}
$target=Get-SdkRepositoryPath 'artifacts/ue5-0.2/demo-path-target'
$link=Get-SdkRepositoryPath 'artifacts/ue5-0.2/demo/path-test/redirect'
New-Item -ItemType Directory -Path $target,(Split-Path $link) -Force|Out-Null
try {
    New-Item -ItemType Junction -Path $link -Target $target|Out-Null
    $rejected=$false
    try {$null=Get-UnrealDemoOutputPath (Join-Path $link 'output')} catch {$rejected=$true}
    if(-not $rejected){throw 'Demo output followed a junction.'}
} finally {if(Test-Path -LiteralPath $link){Remove-Item -LiteralPath $link -Force}}
Write-Output 'Demo archive path bounds passed.'
