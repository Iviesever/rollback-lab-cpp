#requires -Version 7.0
param([Parameter(Mandatory)][string]$SdkRoot)

. (Join-Path $PSScriptRoot '../../scripts/SdkCommon.ps1')
$source = Get-SdkRepositoryPath $SdkRoot
$manifest = Get-Content -LiteralPath (Join-Path $source 'manifest.json') -Raw | ConvertFrom-Json
$fixture = Get-SdkRepositoryPath 'artifacts/sdk/c-api-integrity-fixture'
Remove-SdkGeneratedDirectory $fixture
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $fixture -Recurse
$manifestPath = Join-Path $fixture 'manifest.json'
$originalManifest = [IO.File]::ReadAllBytes($manifestPath)
$testsPassed = 0
function Expect-Rejection {
    param([scriptblock]$Action, [string]$Reason)
    $rejected = $false
    try { $null = & $Action }
    catch {
        if ($_.Exception.Message -notlike "*$Reason*") { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw "SDK verifier accepted invalid fixture: $Reason" }
    $script:testsPassed++
}
$null = Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty
$testsPassed++
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha ('0' * 40) -AllowDirty } 'source SHA mismatch'
if (-not $manifest.source_clean) {
    Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha } 'dirty source'
}
$manifest.abi_version = 99
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'manifest version'
[IO.File]::WriteAllBytes($manifestPath, $originalManifest)
$manifest.abi_version = 1
$dllPath = Join-Path $fixture 'bin/rollback_lab_c.dll'
[IO.File]::WriteAllBytes($dllPath, [byte[]]@(1, 2, 3))
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'checksum mismatch'
Remove-Item -LiteralPath $dllPath
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'Missing SDK file'
Copy-Item -LiteralPath (Join-Path $source 'bin/rollback_lab_c.dll') -Destination $dllPath
$extra = Join-Path $fixture 'unmanifested.txt'
Set-Content -LiteralPath $extra -Value 'unexpected' -Encoding utf8NoBOM
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'unmanifested files'
Remove-Item -LiteralPath $extra
$manifest.files[0].path = '../escaped'
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding utf8NoBOM
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'manifest file path'
[IO.File]::WriteAllBytes($manifestPath, $originalManifest)
$checksumPath = Join-Path $fixture 'checksums.sha256'
Add-Content -LiteralPath $checksumPath -Value ('0' * 64 + '  nonexistent') -Encoding utf8NoBOM
Expect-Rejection { Test-SdkManifest -SdkRoot $fixture -ExpectedGitSha $manifest.source_git_sha -AllowDirty } 'Unexpected or duplicate checksum entry'
Write-Output "SDK manifest tests passed: $testsPassed"
