#requires -Version 7.0
param(
    [Parameter(Mandatory)][string]$SdkRoot,
    [string]$ExpectedGitSha,
    [string]$BuildDirectory = 'build/sdk-consumer',
    [string]$Archive,
    [switch]$AllowDirty
)

. (Join-Path $PSScriptRoot 'SdkCommon.ps1')
if ([string]::IsNullOrEmpty($ExpectedGitSha)) { $ExpectedGitSha = (Get-SdkSourceIdentity).Sha }
$root = Get-SdkRepositoryPath $SdkRoot
$null = Test-SdkManifest -SdkRoot $root -ExpectedGitSha $ExpectedGitSha -AllowDirty:$AllowDirty
if ([string]::IsNullOrEmpty($Archive)) {
    $archives = @(Get-ChildItem -LiteralPath (Split-Path -Parent $root) -Filter '*.zip' -File)
    if ($archives.Count -ne 1) { throw 'Specify -Archive when the SDK directory has no unique sibling ZIP.' }
    $Archive = $archives[0].FullName
}
Test-SdkArchive -SdkRoot $root -Archive $Archive
Initialize-SdkToolchain
$build = Get-SdkRepositoryPath $BuildDirectory
$consumer = Join-Path $script:SdkRepositoryRoot 'tests/c_api/consumer'
Invoke-SdkCommand cmake @('-S', $consumer, '-B', $build, '-G', 'Ninja', '--fresh',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', "-DCMAKE_PREFIX_PATH=$root", "-DSDK_EXPECTED_SHA=$ExpectedGitSha")
Invoke-SdkCommand cmake @('--build', $build)
$previousPath = $env:PATH
try {
    $env:PATH = (Join-Path $root 'bin') + [IO.Path]::PathSeparator + $env:PATH
    Invoke-SdkCommand ctest @('--test-dir', $build, '--output-on-failure')
} finally { $env:PATH = $previousPath }
Write-Output "Verified exact SDK source $ExpectedGitSha, manifest/checksums, C11 and C++ find_package consumers."
