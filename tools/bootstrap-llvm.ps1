param(
    [string]$Version = "22.1.8"
)

$ErrorActionPreference = "Stop"

if ($Version -ne "22.1.8") {
    throw "Only the audited LLVM version 22.1.8 is supported by this script."
}

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$toolsRoot = Join-Path $projectRoot ".tools"
$downloadRoot = Join-Path $toolsRoot "downloads"
$packageName = "clang+llvm-22.1.8-x86_64-pc-windows-msvc.tar.xz"
$packagePath = Join-Path $downloadRoot $packageName
$bundlePath = "$packagePath.jsonl"
$expectedSha256 = "d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234"
$compilerPath = Join-Path $toolsRoot "clang+llvm-22.1.8-x86_64-pc-windows-msvc\bin\clang++.exe"

if (Test-Path -LiteralPath $compilerPath) {
    & $compilerPath --version
    exit 0
}

New-Item -ItemType Directory -Force -Path $downloadRoot | Out-Null
gh release download llvmorg-22.1.8 `
    --repo llvm/llvm-project `
    --pattern $packageName `
    --pattern "$packageName.jsonl" `
    --dir $downloadRoot `
    --clobber
if ($LASTEXITCODE -ne 0) {
    throw "LLVM release download failed."
}

$actualSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagePath).Hash.ToLowerInvariant()
if ($actualSha256 -ne $expectedSha256) {
    throw "LLVM SHA256 mismatch: $actualSha256"
}

gh attestation verify --repo llvm/llvm-project $packagePath --bundle $bundlePath
if ($LASTEXITCODE -ne 0) {
    throw "LLVM GitHub attestation verification failed."
}

tar.exe -xf $packagePath -C $toolsRoot
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $compilerPath)) {
    throw "LLVM archive extraction failed."
}

& $compilerPath --version

