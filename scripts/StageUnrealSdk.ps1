#requires -Version 7.0
param([string]$SdkRoot)
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')
$stage=Stage-RollbackSdk -SdkRoot $SdkRoot
Write-Output "Staged verified SDK $($stage.SourceGitSha) at $($stage.Root)"
