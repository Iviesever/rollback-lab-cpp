param(
    [switch]$Full
)

$ErrorActionPreference = "Stop"

$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot ".."))
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "Visual Studio discovery tool vswhere.exe was not found."
}
$vsInstall = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($vsInstall)) {
    throw "A Visual Studio C++ toolchain was not found."
}
$vsDevShell = Join-Path $vsInstall "Common7\Tools\Launch-VsDevShell.ps1"

& $vsDevShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation | Out-Null
Set-Location -LiteralPath $projectRoot

cmake --workflow --preset msvc-debug-ci
if ($LASTEXITCODE -ne 0) { throw "MSVC Debug verification failed." }

cmake --workflow --preset msvc-release-ci
if ($LASTEXITCODE -ne 0) { throw "MSVC Release verification failed." }

$llvmRoot = Join-Path $projectRoot ".tools\clang+llvm-22.1.8-x86_64-pc-windows-msvc"
$llvmBin = Join-Path $llvmRoot "bin"
$sanitizerRuntime = Join-Path $llvmRoot "lib\clang\22\lib\windows"
if (-not (Test-Path -LiteralPath (Join-Path $llvmBin "clang++.exe"))) {
    throw "Run tools/bootstrap-llvm.ps1 before cross-compiler verification."
}
$env:PATH = "$llvmBin;$sanitizerRuntime;$env:PATH"

cmake --workflow --preset clang-debug-ci
if ($LASTEXITCODE -ne 0) { throw "Clang Debug verification failed." }

cmake --preset clang-release --fresh
if ($LASTEXITCODE -ne 0) { throw "Clang Release configure failed." }
cmake --build --preset clang-release
if ($LASTEXITCODE -ne 0) { throw "Clang Release build failed." }
ctest --preset clang-release --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Clang Release tests failed." }

$env:ASAN_OPTIONS = "halt_on_error=1:detect_leaks=0"
$env:UBSAN_OPTIONS = "halt_on_error=1:print_stacktrace=1"
cmake --preset clang-asan --fresh
if ($LASTEXITCODE -ne 0) { throw "Sanitizer configure failed." }
cmake --build --preset clang-asan
if ($LASTEXITCODE -ne 0) { throw "Sanitizer build failed." }
ctest --preset clang-asan --output-on-failure
if ($LASTEXITCODE -ne 0) { throw "Sanitizer tests failed." }

if ($Full) {
    $sweep = Join-Path $projectRoot "build\msvc-release\rollback_lab_property_sweep.exe"
    $evidence = Join-Path $projectRoot "tasks\20260903-215400-rollback-netcode-0.1\evidence\PACT-70\property-sweep-10000.json"
    & $sweep --seeds 10000 --repeat-samples 128 --repeat-full --out $evidence
    if ($LASTEXITCODE -ne 0) { throw "10,000-seed property sweep failed." }
}

Write-Output "Verification completed successfully."
