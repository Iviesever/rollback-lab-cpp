#requires -Version 7.0
param()

$ErrorActionPreference = 'Stop'
$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$fixture = [IO.Path]::GetFullPath((Join-Path $repository 'artifacts/process-runner-tests'))
$expectedPrefix = $repository.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
if (-not $fixture.StartsWith($expectedPrefix, [StringComparison]::OrdinalIgnoreCase)) { throw 'Fixture path escaped repository.' }
New-Item -ItemType Directory -Force -Path $fixture | Out-Null
$env:TEMP = Join-Path $fixture 'temp'
$env:TMP = $env:TEMP
New-Item -ItemType Directory -Force -Path $env:TEMP | Out-Null
$source = Join-Path $repository 'scripts/native/ProcessRunner.cs'
if (-not (Test-Path -LiteralPath $source)) { throw 'Process supervisor implementation is missing.' }
if (-not ('RollbackLab.Tools.ProcessRunner' -as [type])) { Add-Type -Path $source }
$pwsh = Join-Path $PSHOME 'pwsh.exe'
$echoScript = Join-Path $fixture 'echo arguments.ps1'
@'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
[Console]::Out.WriteLine((ConvertTo-Json -InputObject @($args) -Compress))
[Console]::Out.WriteLine([Environment]::CurrentDirectory)
[Console]::Out.WriteLine($env:ROLLBACK_RUNNER_TEST)
[Console]::Error.WriteLine('stderr-marker')
exit 0
'@ | Set-Content -LiteralPath $echoScript -Encoding utf8NoBOM
$exitScript = Join-Path $fixture 'exit-seven.ps1'
"[Console]::Out.WriteLine('before-exit-seven'); exit 7" | Set-Content -LiteralPath $exitScript -Encoding utf8NoBOM
$treeScript = Join-Path $fixture 'bounded-tree.ps1'
@'
param([string]$Directory, [string]$Role, [string]$Mode)
[IO.File]::WriteAllText((Join-Path $Directory ($Role + '.pid')), [string]$PID)
[Console]::Out.WriteLine($Role + '-stdout')
if ($Role -ne 'grandchild') {
    $next = if ($Role -eq 'root') { 'child' } else { 'grandchild' }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = Join-Path $PSHOME 'pwsh.exe'
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($item in @('-NoLogo', '-NoProfile', '-File', $PSCommandPath, $Directory, $next, $Mode)) { $start.ArgumentList.Add($item) }
    $process = [Diagnostics.Process]::Start($start)
    $process.Dispose()
}
if ($Role -eq 'root' -and $Mode -eq 'root-exits') {
    $deadline = [Diagnostics.Stopwatch]::StartNew()
    while (-not (Test-Path -LiteralPath (Join-Path $Directory 'grandchild.pid')) -and $deadline.ElapsedMilliseconds -lt 10000) {
        Start-Sleep -Milliseconds 20
    }
    if (-not (Test-Path -LiteralPath (Join-Path $Directory 'grandchild.pid'))) { exit 9 }
    exit 7
}
[Threading.Thread]::Sleep(30000)
exit 0
'@ | Set-Content -LiteralPath $treeScript -Encoding utf8NoBOM
$sentinelScript = Join-Path $fixture 'unrelated-sentinel.ps1'
"[Threading.Thread]::Sleep(30000); exit 0" | Set-Content -LiteralPath $sentinelScript -Encoding utf8NoBOM

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw "ASSERT: $Message" }
}
function Invoke-Runner([string]$Name, [string[]]$ProcessArguments, [int]$Timeout = 10000) {
    [RollbackLab.Tools.ProcessRunner]::Run($pwsh, $ProcessArguments, $fixture,
        (Join-Path $fixture ($Name + '.stdout.txt')), (Join-Path $fixture ($Name + '.stderr.txt')), $Timeout, $false)
}
function Assert-TreeGone([string]$Directory, $Result) {
    foreach ($role in @('root', 'child', 'grandchild')) {
        $pidPath = Join-Path $Directory ($role + '.pid')
        Assert-True (Test-Path -LiteralPath $pidPath) "Missing $role PID evidence"
        $childId = [int](Get-Content -LiteralPath $pidPath -Raw)
        $surviving = Get-Process -Id $childId -ErrorAction SilentlyContinue
        Assert-True ($null -eq $surviving) "Owned $role process $childId survived"
    }
    Assert-True $Result.AllChildrenExited 'Job accounting did not reach zero active processes'
    Assert-True ($Result.TotalProcesses -ge 3) 'Job did not own the full root/child/grandchild tree'
}

$results = [Collections.Generic.List[object]]::new()
$payload = @('plain', 'two words', 'quote"inside', 'tail\', 'slash\\"quoted', '', '$HOME;$(whoami)', 'Unicode-雪')
$env:ROLLBACK_RUNNER_TEST = 'inherited-environment-marker'
$echo = Invoke-Runner 'echo' (@('-NoLogo', '-NoProfile', '-File', $echoScript) + $payload)
$lines = @(Get-Content -LiteralPath (Join-Path $fixture 'echo.stdout.txt'))
$actualArguments = @(ConvertFrom-Json -InputObject $lines[0])
Assert-True ($echo.ExitCode -eq 0 -and -not $echo.TimedOut -and $echo.AllChildrenExited) 'Simple process result incorrect'
Assert-True ($actualArguments.Count -eq $payload.Count) 'Argument count changed'
for ($index = 0; $index -lt $payload.Count; $index++) {
    Assert-True ($actualArguments[$index] -ceq $payload[$index]) "Argument $index changed"
}
Assert-True ($lines[1] -ieq $fixture) 'Working directory was not applied'
Assert-True ($lines[2] -ceq $env:ROLLBACK_RUNNER_TEST) 'Environment was not inherited'
Assert-True ((Get-Content -LiteralPath (Join-Path $fixture 'echo.stderr.txt') -Raw).Trim() -ceq 'stderr-marker') 'stderr not written to file'
$results.Add([ordered]@{ name = 'arguments-stdout-stderr-working-directory-environment'; result = $echo })

$nonzero = Invoke-Runner 'exit-seven' @('-NoLogo', '-NoProfile', '-File', $exitScript)
Assert-True ($nonzero.ExitCode -eq 7 -and -not $nonzero.TimedOut -and $nonzero.AllChildrenExited) 'Exit 7 was lost'
$results.Add([ordered]@{ name = 'nonzero-root-exit'; result = $nonzero })

$sentinelStart = [Diagnostics.ProcessStartInfo]::new()
$sentinelStart.FileName = $pwsh
$sentinelStart.UseShellExecute = $false
$sentinelStart.CreateNoWindow = $true
foreach ($argument in @('-NoLogo', '-NoProfile', '-File', $sentinelScript)) { $sentinelStart.ArgumentList.Add($argument) }
$sentinel = [Diagnostics.Process]::Start($sentinelStart)
try {
    foreach ($mode in @('root-exits', 'watchdog')) {
        $directory = Join-Path $fixture $mode
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
        # Only remove this fixture's known leaf PID files, never a process-name group.
        foreach ($role in @('root', 'child', 'grandchild')) {
            $pidPath = Join-Path $directory ($role + '.pid')
            if (Test-Path -LiteralPath $pidPath) { Remove-Item -LiteralPath $pidPath }
        }
        $limit = if ($mode -eq 'watchdog') { 4000 } else { 12000 }
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $result = Invoke-Runner $mode @('-NoLogo', '-NoProfile', '-File', $treeScript, $directory, 'root', $mode) $limit
        Assert-True ($clock.ElapsedMilliseconds -lt $limit + 12000) 'Supervisor exceeded its bounded cleanup allowance'
        Assert-TreeGone $directory $result
        if ($mode -eq 'watchdog') { Assert-True $result.TimedOut 'Watchdog did not fire' }
        else { Assert-True ($result.ExitCode -eq 7 -and -not $result.TimedOut) 'Child cleanup replaced the true root exit' }
        Assert-True (-not $sentinel.HasExited) 'A process outside the owned job was killed'
        $results.Add([ordered]@{ name = $mode; elapsed_ms = $clock.ElapsedMilliseconds; result = $result })
    }
} finally {
    if (-not $sentinel.HasExited) {
        $sentinel.Kill()
        Assert-True ($sentinel.WaitForExit(10000)) 'Sentinel cleanup failed'
    }
    $sentinel.Dispose()
}

$stdoutPath = Join-Path $fixture 'start-failure.stdout.txt'
$stderrPath = Join-Path $fixture 'start-failure.stderr.txt'
# Warm exception/JIT paths before measuring handles owned by repeated failures.
try { $null = [RollbackLab.Tools.ProcessRunner]::Run((Join-Path $fixture 'missing.exe'), [string[]]@(), $fixture, $stdoutPath, $stderrPath, 1000) }
catch { }
$current = [Diagnostics.Process]::GetCurrentProcess()
$current.Refresh()
$beforeHandles = $current.HandleCount
for ($index = 0; $index -lt 12; $index++) {
    $rejected = $false
    try { $null = [RollbackLab.Tools.ProcessRunner]::Run((Join-Path $fixture 'missing.exe'), [string[]]@(), $fixture, $stdoutPath, $stderrPath, 1000) }
    catch { $rejected = $true }
    Assert-True $rejected 'Missing executable was accepted'
    foreach ($path in @($stdoutPath, $stderrPath)) {
        $exclusive = [IO.File]::Open($path, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
        $exclusive.Dispose()
    }
}
$current.Refresh()
$afterHandles = $current.HandleCount
$current.Dispose()
Assert-True ($afterHandles -le $beforeHandles + 2) "Failed starts leaked handles: $beforeHandles -> $afterHandles"
$results.Add([ordered]@{ name = 'start-failure-cleanup'; iterations = 12; handles_before = $beforeHandles; handles_after = $afterHandles })
$results | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $fixture 'results.json') -Encoding utf8NoBOM
Write-Output "ProcessRunner tests passed: $($results.Count) cases. Evidence: $fixture"
