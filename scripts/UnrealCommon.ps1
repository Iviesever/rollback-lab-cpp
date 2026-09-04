#requires -Version 7.0
. (Join-Path $PSScriptRoot 'SdkCommon.ps1')
$script:UnrealProjectRelative = 'examples/ue5/RollbackArena/RollbackArena.uproject'
$script:UnrealPluginRelative = 'examples/ue5/RollbackArena/Plugins/RollbackLabBridge'

function Get-UnrealPluginOutputPath {
    param([Parameter(Mandatory)][string]$Path)
    $full=(Get-SdkRepositoryPath $Path).TrimEnd('\','/')
    $dedicated=(Get-SdkRepositoryPath 'artifacts/ue5-0.2/plugin').TrimEnd('\','/')
    if($full -ine $dedicated -and -not $full.StartsWith($dedicated+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'BuildPlugin output must stay in the dedicated plugin artifact directory.'
    }
    Assert-UnrealOrdinaryPath $full
    return $full
}

function Get-UnrealDemoOutputPath {
    param([Parameter(Mandatory)][string]$Path)
    $full=(Get-SdkRepositoryPath $Path).TrimEnd('\','/')
    $dedicated=(Get-SdkRepositoryPath 'artifacts/ue5-0.2/demo').TrimEnd('\','/')
    if($full -ine $dedicated -and -not $full.StartsWith($dedicated+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'Demo output must stay in the dedicated demo artifact directory.'
    }
    Assert-UnrealOrdinaryPath $full
    return $full
}

function Assert-UnrealOrdinaryPath {
    param([Parameter(Mandatory)][string]$Path)
    $full=Get-SdkRepositoryPath $Path
    $cursor=[IO.DirectoryInfo]::new($full)
    while($null -ne $cursor) {
        if(Test-Path -LiteralPath $cursor.FullName) {
            $item=Get-Item -LiteralPath $cursor.FullName -Force
            if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw 'Unreal generated paths cannot pass through a junction or symbolic link.'
            }
        }
        if($cursor.FullName -ieq $script:SdkRepositoryRoot){break}
        $cursor=$cursor.Parent
    }
}

function Stage-RollbackSdk {
    param([string]$SdkRoot)
    $identity = Get-SdkSourceIdentity
    if ([string]::IsNullOrEmpty($SdkRoot)) {
        $SdkRoot = 'artifacts/sdk/0.2.0-' + $identity.Sha.Substring(0,12) + '/install'
        if (-not (Test-Path -LiteralPath (Get-SdkRepositoryPath $SdkRoot))) {
            & (Join-Path $PSScriptRoot 'BuildSdk.ps1') | Out-Host
        }
    }
    $root = (Get-SdkRepositoryPath $SdkRoot).TrimEnd('\','/')
    Assert-UnrealOrdinaryPath $root
    $manifest = Test-SdkManifest -SdkRoot $root -ExpectedGitSha $identity.Sha
    $destination = Get-SdkRepositoryPath ($script:UnrealPluginRelative + '/Binaries/ThirdParty/RollbackLab')
    Assert-UnrealOrdinaryPath $destination
    if($root -ieq $destination) {
        return [pscustomobject]@{Root=$destination; SourceGitSha=$identity.Sha; Manifest=$manifest}
    }
    $separator=[IO.Path]::DirectorySeparatorChar
    if($root.StartsWith($destination+$separator,[StringComparison]::OrdinalIgnoreCase) -or
       $destination.StartsWith($root+$separator,[StringComparison]::OrdinalIgnoreCase)) {
        throw 'SDK staging source and destination cannot partially overlap.'
    }
    if (Test-Path -LiteralPath $destination) {
        $resolved = (Resolve-Path -LiteralPath $destination).Path
        if ($resolved -ine $destination) { throw 'SDK staging target resolution mismatch.' }
        # This exact ignored, generated SDK directory has been validated in-repo.
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
    New-Item -ItemType Directory -Path $destination -Force | Out-Null
    Get-ChildItem -LiteralPath $root -Force | Copy-Item -Destination $destination -Recurse -Force
    $null = Test-SdkManifest -SdkRoot $destination -ExpectedGitSha $identity.Sha
    [pscustomobject]@{Root=$destination; SourceGitSha=$identity.Sha; Manifest=$manifest}
}

function New-UnrealContext {
    param([Parameter(Mandatory)][string]$EngineRoot, [Parameter(Mandatory)][string]$Role)
    $engine = (Resolve-Path -LiteralPath $EngineRoot).Path
    $version = Get-Content -LiteralPath (Join-Path $engine 'Engine/Build/Build.version') -Raw | ConvertFrom-Json
    if ($version.MajorVersion -ne 5 -or $version.MinorVersion -ne 8) { throw 'This integration requires UE 5.8.' }
    $cache = Get-SdkRepositoryPath '.cache/ue5'
    New-Item -ItemType Directory -Path $cache -Force | Out-Null
    $lockPath = Join-Path $cache 'unreal-tool.lock'
    try { $lock = [IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None) }
    catch { throw 'Another RollbackLab Unreal tool pipeline already holds the repository lock.' }
    try {
        $editors = @(Get-Process UnrealEditor,UnrealEditor-Cmd -ErrorAction SilentlyContinue)
        if ($editors.Count) { throw 'An Unreal Editor process is already running; tool pipelines must be serialized.' }
        $run = Get-SdkRepositoryPath ('artifacts/ue5-0.2/runs/' + (Get-Date -Format 'yyyyMMdd-HHmmss-fff') + '-' + $Role)
        New-Item -ItemType Directory -Path $run -Force | Out-Null
        $env:TEMP = Join-Path $cache 'temp'; $env:TMP=$env:TEMP
        $env:UBA_ROOT = Join-Path $cache 'uba'
        $env:DOTNET_CLI_HOME = Join-Path $cache 'dotnet'
        $env:NUGET_PACKAGES = Join-Path $cache 'nuget'
        $env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'; $env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
        $env:VSLANG='1033'
        $env:PYTHONDONTWRITEBYTECODE='1'
        $env:uebp_EngineSavedFolder = Join-Path $cache 'EngineSaved'
        $env:uebp_LogFolder = Join-Path $run 'uat'; $env:uebp_FinalLogFolder=$env:uebp_LogFolder
        $dotnetRoot=Join-Path $engine 'Engine/Binaries/ThirdParty/DotNet/10.0/win-x64'
        $env:UE_DOTNET_VERSION='10.0'; $env:UE_DOTNET_DIR=$dotnetRoot; $env:DOTNET_ROOT=$dotnetRoot
        $env:DOTNET_MULTILEVEL_LOOKUP='0'; $env:DOTNET_ROLL_FORWARD='LatestMajor'
        $env:PATH=$dotnetRoot+[IO.Path]::PathSeparator+$env:PATH
        New-Item -ItemType Directory -Path $env:TEMP,$env:UBA_ROOT,$env:DOTNET_CLI_HOME,$env:NUGET_PACKAGES -Force | Out-Null
        $xmlCache=Join-Path $cache 'BuildConfiguration.bin'
        # UE 5.8 XmlConfigData v2: no input files or overrides; retain tool defaults.
        [IO.File]::WriteAllBytes($xmlCache,[byte[]](2,0,0,0,0,0,0,0,0,0,0,0))
        $env:UBT_EXTRA_ARGS='-XmlConfigCache="'+$xmlCache+'" -WaitMutex -NoUBA -NoXGE -MaxParallelActions=4'
        [pscustomobject]@{Engine=$engine; Run=$run; Cache=$cache; Lock=$lock;
            UbtArguments=@(('-XmlConfigCache='+$xmlCache),'-WaitMutex','-NoUBA','-NoXGE','-MaxParallelActions=4');
            Dotnet=(Join-Path $dotnetRoot 'dotnet.exe');
            Ubt=(Join-Path $engine 'Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll');
            Uat=(Join-Path $engine 'Engine/Binaries/DotNET/AutomationTool/AutomationTool.dll');
            Editor=(Join-Path $engine 'Engine/Binaries/Win64/UnrealEditor-Cmd.exe');
            Project=(Get-SdkRepositoryPath $script:UnrealProjectRelative)}
    } catch { $lock.Dispose(); throw }
}

function Invoke-UnrealProcess {
    param([Parameter(Mandatory)]$Context, [Parameter(Mandatory)][string]$FilePath,
          [Parameter(Mandatory)][string[]]$Arguments, [string]$Name='process',
          [ValidateRange(1,7200)][int]$TimeoutSeconds=1800, [switch]$Visible)
    if (-not ('RollbackLab.Tools.ProcessRunner' -as [type])) {
        Add-Type -Path (Join-Path $PSScriptRoot 'native/ProcessRunner.cs')
    }
    $identity=Get-SdkSourceIdentity
    $record=[ordered]@{source_git_sha=$identity.Sha;source_clean=$identity.Clean;
        program=$FilePath;arguments=$Arguments;timeout_seconds=$TimeoutSeconds}
    try {
        $result=[RollbackLab.Tools.ProcessRunner]::Run($FilePath,$Arguments,$script:SdkRepositoryRoot,
            (Join-Path $Context.Run ($Name+'.stdout.txt')),(Join-Path $Context.Run ($Name+'.stderr.txt')),
            ($TimeoutSeconds*1000),[bool]$Visible)
        $record.pid=$result.ProcessId; $record.exit_code=$result.ExitCode
        $record.timed_out=$result.TimedOut; $record.all_children_exited=$result.AllChildrenExited
        $record.total_processes=$result.TotalProcesses
        if($result.TimedOut){throw "Unreal watchdog expired after $TimeoutSeconds seconds."}
        if(-not $result.AllChildrenExited){throw 'Unreal process job retained a child after bounded cleanup.'}
        if($result.ExitCode -ne 0){throw "Unreal $Name exited with $($result.ExitCode). See $($Context.Run)."}
    } catch {
        $record.failure=$_.Exception.Message
        throw
    } finally {
        $record|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $Context.Run ($Name+'.process.json')) -Encoding utf8NoBOM
    }
}

function Get-UnrealRuntimeArguments {
    param([Parameter(Mandatory)]$Context)
    @('-unattended','-nop4','-nosplash','-nosound','-nowrite','-culture=en','-notraceserver','-NoZenAutoLaunch','-DDC=(Local)',
      ('-UserDir='+ (Join-Path $Context.Run 'user')),
      ('-ShaderWorkingDir='+ (Join-Path $Context.Cache 'shaders')),
      ('-LocalDataCachePath='+ (Join-Path $Context.Cache 'ddc')),
      ('-AbsCrashReportClientLog='+ (Join-Path $Context.Run 'crash-reporter.log')),
      ('-abslog='+ (Join-Path $Context.Run 'editor.log')))
}
