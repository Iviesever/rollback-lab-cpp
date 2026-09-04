$ErrorActionPreference = 'Stop'
$script:SdkRepositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

function Get-SdkRepositoryPath {
    param([Parameter(Mandatory)][string]$Path)
    $full = if ([IO.Path]::IsPathRooted($Path)) { [IO.Path]::GetFullPath($Path) }
            else { [IO.Path]::GetFullPath((Join-Path $script:SdkRepositoryRoot $Path)) }
    $prefix = $script:SdkRepositoryRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "SDK path must remain inside the repository: $full"
    }
    return $full
}

function Initialize-SdkToolchain {
    $tempPath = Get-SdkRepositoryPath '.cache/sdk/temp'
    New-Item -ItemType Directory -Force -Path $tempPath | Out-Null
    $env:TEMP = $tempPath
    $env:TMP = $tempPath
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio discovery tool was not found.' }
    $installation = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installation)) { throw 'MSVC x64 toolchain was not found.' }
    & (Join-Path $installation 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
}

function Invoke-SdkCommand {
    param([Parameter(Mandatory)][string]$FilePath, [string[]]$Arguments)
    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$FilePath failed with exit code $LASTEXITCODE." }
}

function Get-SdkSourceIdentity {
    $sha = & git -C $script:SdkRepositoryRoot rev-parse HEAD
    if ($LASTEXITCODE -ne 0 -or $sha -notmatch '^[0-9a-f]{40}$') { throw 'Cannot resolve exact source SHA.' }
    $changes = & git -C $script:SdkRepositoryRoot status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source cleanliness.' }
    [pscustomobject]@{ Sha = $sha; Clean = @($changes).Count -eq 0 }
}

function Remove-SdkGeneratedDirectory {
    param([Parameter(Mandatory)][string]$Path)
    $full = Get-SdkRepositoryPath $Path
    $artifacts = (Get-SdkRepositoryPath 'artifacts').TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $full.StartsWith($artifacts, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Generated-directory cleanup is restricted to artifacts: $full"
    }
    if (Test-Path -LiteralPath $full) {
        $resolved = (Resolve-Path -LiteralPath $full).Path
        if (-not $resolved.StartsWith($artifacts, [StringComparison]::OrdinalIgnoreCase)) { throw 'Resolved cleanup target escaped artifacts.' }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Test-SdkManifest {
    param([Parameter(Mandatory)][string]$SdkRoot, [Parameter(Mandatory)][string]$ExpectedGitSha, [switch]$AllowDirty)
    $root = Get-SdkRepositoryPath $SdkRoot
    $manifestPath = Join-Path $root 'manifest.json'
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schema_version -ne 1 -or $manifest.sdk_version -ne '0.2.0-candidate' -or
        $manifest.abi_version -ne 1 -or $manifest.simulation_version -ne 1 -or
        $manifest.protocol_version -ne 1 -or $manifest.replay_version -ne 1) { throw 'Unsupported SDK manifest version.' }
    if ($manifest.source_git_sha -cne $ExpectedGitSha -or $ExpectedGitSha -notmatch '^[0-9a-f]{40}$') { throw 'SDK source SHA mismatch.' }
    if (-not $AllowDirty -and $manifest.source_clean -ne $true) { throw 'SDK was built from dirty source.' }
    if ($manifest.configuration -ne 'Release' -or $manifest.architecture -ne 'x64' -or
        $manifest.runtime -ne 'MD' -or $manifest.linkage -ne 'shared') { throw 'Unsupported SDK build contract.' }
    $expected = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in $manifest.files) {
        if ($entry.path -notmatch '^[A-Za-z0-9_./-]+$' -or $entry.path.Contains('..') -or
            [IO.Path]::IsPathRooted($entry.path) -or -not $expected.Add($entry.path)) { throw 'Invalid or duplicate SDK manifest file path.' }
        $filePath = Join-Path $root $entry.path
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) { throw "Missing SDK file: $($entry.path)" }
        if ($entry.sha256 -notmatch '^[0-9A-Fa-f]{64}$' -or
            (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash -ine $entry.sha256) { throw "SDK checksum mismatch: $($entry.path)" }
    }
    foreach ($required in @('include/rollback_lab/c_api/rollback_lab_c.h', 'bin/rollback_lab_c.dll',
                           'lib/rollback_lab_c.lib', 'lib/rollback_lab_core.lib', 'LICENSE', 'README.md',
                           'lib/cmake/rollback_lab/rollback_labConfig.cmake')) {
        if (-not $expected.Contains($required)) { throw "SDK manifest omitted required file: $required" }
    }
    $null = $expected.Add('manifest.json')
    $checksumEntries = @{}
    foreach ($line in Get-Content -LiteralPath (Join-Path $root 'checksums.sha256')) {
        if ($line -notmatch '^([0-9A-Fa-f]{64})  (.+)$') { throw 'Malformed SDK checksum line.' }
        $relative = $Matches[2]
        if (-not $expected.Contains($relative) -or $checksumEntries.ContainsKey($relative)) { throw 'Unexpected or duplicate checksum entry.' }
        $checksumEntries[$relative] = $Matches[1]
        if ((Get-FileHash -LiteralPath (Join-Path $root $relative) -Algorithm SHA256).Hash -ine $Matches[1]) { throw "SDK checksum-list mismatch: $relative" }
    }
    if ($checksumEntries.Count -ne $expected.Count) { throw 'SDK checksum list is incomplete.' }
    $null = $expected.Add('checksums.sha256')
    $actual = @(Get-ChildItem -LiteralPath $root -Recurse -File)
    if ($actual.Count -ne $expected.Count) { throw 'SDK install tree contains missing or unmanifested files.' }
    foreach ($file in $actual) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        if (-not $expected.Contains($relative)) { throw "Unmanifested SDK file: $relative" }
    }
    return $manifest
}

function Test-SdkArchive {
    param([Parameter(Mandatory)][string]$SdkRoot, [Parameter(Mandatory)][string]$Archive)
    $root = Get-SdkRepositoryPath $SdkRoot
    $zipPath = Get-SdkRepositoryPath $Archive
    $checksum = (Get-Content -LiteralPath ($zipPath + '.sha256') -Raw).Trim()
    $actualSha = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($checksum -cne ($actualSha + '  ' + [IO.Path]::GetFileName($zipPath))) { throw 'SDK archive SHA-256 mismatch.' }
    $files = @{}
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
        $relative = [IO.Path]::GetRelativePath($root, $file.FullName).Replace('\', '/')
        $files[$relative] = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash
    }
    $archiveReader = [IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        foreach ($entry in $archiveReader.Entries) {
            $relative = $entry.FullName.Replace('\', '/')
            if ($relative.EndsWith('/')) { continue }
            if (-not $files.ContainsKey($relative)) { throw "Unexpected or duplicate SDK archive entry: $relative" }
            $stream = $entry.Open()
            $hasher = [Security.Cryptography.SHA256]::Create()
            try { $entryHash = [BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
            finally { $hasher.Dispose(); $stream.Dispose() }
            if ($entryHash -ine $files[$relative]) { throw "SDK archive content mismatch: $relative" }
            $files.Remove($relative)
        }
        if ($files.Count -ne 0) { throw 'SDK archive is missing install-tree files.' }
    } finally { $archiveReader.Dispose() }
}
