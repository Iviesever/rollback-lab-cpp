#requires -Version 7.0
param(
    [string]$OutputRoot = 'artifacts/sdk',
    [string]$BuildDirectory = 'build/sdk-release',
    [switch]$AllowDirty
)

. (Join-Path $PSScriptRoot 'SdkCommon.ps1')
$identity = Get-SdkSourceIdentity
if (-not $identity.Clean -and -not $AllowDirty) { throw 'SDK packaging requires clean source. Commit reviewed work first; -AllowDirty produces intermediate evidence only.' }
$artifactRoot = Get-SdkRepositoryPath $OutputRoot
$build = Get-SdkRepositoryPath $BuildDirectory
$label = '0.2.0-' + $identity.Sha.Substring(0, 12)
if (-not $identity.Clean) { $label += '-working' }
$packageRoot = Join-Path $artifactRoot $label
$install = Join-Path $packageRoot 'install'
Initialize-SdkToolchain
New-Item -ItemType Directory -Force -Path $packageRoot | Out-Null
Invoke-SdkCommand cmake @('-S', $script:SdkRepositoryRoot, '-B', $build, '-G', 'Ninja', '--fresh',
    '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_C_COMPILER=cl', '-DCMAKE_CXX_COMPILER=cl',
    '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL', '-DROLLBACK_LAB_BUILD_TESTS=ON',
    '-DBUILD_TESTING=ON', '-DROLLBACK_LAB_ENABLE_SANITIZERS=OFF')
Invoke-SdkCommand cmake @('--build', $build)
Invoke-SdkCommand ctest @('--test-dir', $build, '--output-on-failure')
Remove-SdkGeneratedDirectory $install
Invoke-SdkCommand cmake @('--install', $build, '--prefix', $install)
$compilerInfo = Get-ChildItem -LiteralPath (Join-Path $build 'CMakeFiles') -Filter CMakeCXXCompiler.cmake -Recurse -File | Select-Object -First 1
$compilerVersion = (Select-String -LiteralPath $compilerInfo.FullName -Pattern 'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)').Matches.Groups[1].Value
if ([string]::IsNullOrEmpty($compilerVersion)) { throw 'Cannot resolve SDK compiler version.' }
$after = Get-SdkSourceIdentity
if ($after.Sha -cne $identity.Sha -or $after.Clean -ne $identity.Clean) { throw 'Source identity changed during SDK build.' }
$files = @(Get-ChildItem -LiteralPath $install -Recurse -File | Sort-Object FullName | ForEach-Object {
    [ordered]@{ path = [IO.Path]::GetRelativePath($install, $_.FullName).Replace('\', '/');
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() }
})
$manifest = [ordered]@{
    schema_version = 1; sdk_version = '0.2.0-candidate'; abi_version = 1;
    simulation_version = 1; protocol_version = 1; replay_version = 1;
    source_git_sha = $identity.Sha; source_clean = $identity.Clean;
    configuration = 'Release'; architecture = 'x64'; runtime = 'MD'; linkage = 'shared';
    compiler = 'MSVC'; compiler_version = $compilerVersion; files = $files
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $install 'manifest.json') -Encoding utf8NoBOM
$checksums = @(Get-ChildItem -LiteralPath $install -Recurse -File | Sort-Object FullName | ForEach-Object {
    (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' +
        [IO.Path]::GetRelativePath($install, $_.FullName).Replace('\', '/')
})
$checksums | Set-Content -LiteralPath (Join-Path $install 'checksums.sha256') -Encoding utf8NoBOM
$null = Test-SdkManifest -SdkRoot $install -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
$zip = Join-Path $packageRoot ('rollback_lab-sdk-' + $label + '-win64.zip')
Compress-Archive -Path (Join-Path $install '*') -DestinationPath $zip -Force
$zipSha = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
($zipSha + '  ' + [IO.Path]::GetFileName($zip)) | Set-Content -LiteralPath ($zip + '.sha256') -Encoding utf8NoBOM
Write-Output "SDK install: $install"
Write-Output "SDK archive: $zip"
Write-Output "SDK SHA-256: $zipSha"
