#requires -Version 7.0
[CmdletBinding(PositionalBinding=$false)]
param([string]$EvidenceRun,[ValidateSet('Normal','MissingPeer','ProtocolMismatch','SimulationMismatch','AbiMismatch','Watchdog','Desync')][string]$ExpectedCase='Normal',
      [string]$CoreFixture='artifacts/pact85-core/paced-dll',[string]$SdkRoot,
      [switch]$WatchdogOnly,
      [ValidateSet('None','QuoteSupervisor','QuoteChild','OrphanSupervisor','OrphanChild','WatchdogSupervisor','WritingChild')][string]$FixtureMode='None',
      [string]$FixtureRoot,[Parameter(ValueFromRemainingArguments)][string[]]$FixtureArguments)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/UnrealEvidence.ps1')
$implementation=Join-Path $PSScriptRoot '../../scripts/UdpUnrealEvidence.ps1'
if(Test-Path -LiteralPath $implementation){. $implementation}
if($FixtureMode -ne 'None') {
    if($FixtureMode -eq 'WritingChild') {
        $writerClock=[Diagnostics.Stopwatch]::StartNew();$sequence=0
        while($writerClock.Elapsed.TotalSeconds -lt 30) {
            [Console]::Out.WriteLine("owned stdout $PID $sequence");[Console]::Out.Flush()
            [Console]::Error.WriteLine("owned stderr $PID $sequence");[Console]::Error.Flush()
            $sequence++;Start-Sleep -Milliseconds 2
        }
        exit 0
    }
    if($FixtureMode -eq 'WatchdogSupervisor') {
        $children=@()
        foreach($number in 0,1,2) {
            $children+=Start-UdpUnrealChild -Name ('writer-'+$number) -FilePath (Get-Process -Id $PID).Path -Arguments @('-NoProfile','-File',$PSCommandPath,'-FixtureMode','WritingChild','-FixtureRoot',$FixtureRoot) -Directory $FixtureRoot
            Write-UdpUnrealJson @{processes=@($children|ForEach-Object{$_.Record})} (Join-Path $FixtureRoot 'children.json')
        }
        $rootClock=[Diagnostics.Stopwatch]::StartNew()
        while($rootClock.Elapsed.TotalSeconds -lt 30){Start-Sleep -Milliseconds 20}
        throw 'Watchdog fixture unexpectedly outlived its outer deadline.'
    }
    if($FixtureMode -eq 'QuoteChild') {
        Write-UdpUnrealJson -Value @{arguments=@($FixtureArguments)} -Path (Join-Path $FixtureRoot 'argv.json')
        exit 0
    }
    if($FixtureMode -eq 'OrphanChild'){Start-Sleep -Seconds 30;exit 0}
    $values=@('with spaces','embedded"quote','C:\trailing slash\','', 'literal$()`backtick')
    $mode=if($FixtureMode -eq 'QuoteSupervisor'){'QuoteChild'}else{'OrphanChild'}
    $child=Start-UdpUnrealChild -Name 'fixture' -FilePath (Get-Process -Id $PID).Path -Arguments (@('-NoProfile','-File',$PSCommandPath,'-FixtureMode',$mode,'-FixtureRoot',$FixtureRoot)+$values) -Directory $FixtureRoot
    Write-UdpUnrealJson -Value $child.Record -Path (Join-Path $FixtureRoot 'child.json')
    if($FixtureMode -eq 'QuoteSupervisor') {
        if(-not $child.Handle.WaitForExit(10000)){throw 'Owned quoting fixture timed out.'}
        Complete-UdpUnrealChild $child
        $child.Handle.Dispose()
    }
    # OrphanSupervisor deliberately exits while its child is alive: the existing outer job must reap it.
    exit 0
}
if($EvidenceRun) {
    if(-not(Get-Command Test-UdpUnrealCaseEvidence -ErrorAction SilentlyContinue)){throw 'Missing UDP case evidence verifier.'}
    Test-UdpUnrealCaseEvidence -Run $EvidenceRun -ExpectedCase $ExpectedCase
    exit 0
}
$results=[Collections.Generic.List[object]]::new()
$testRoot=Get-SdkRepositoryPath ('artifacts/ue5-0.2/udp-evidence-tests-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
New-Item -ItemType Directory -Path $testRoot -Force|Out-Null
function Check([string]$Name,[scriptblock]$Action) {
    if($WatchdogOnly -and $Name -notmatch '^(summary |watchdog )'){return}
    try {& $Action|Out-Null;$results.Add([ordered]@{name=$Name;passed=$true});Write-Output "PASS $Name"}
    catch {$results.Add([ordered]@{name=$Name;passed=$false;error=$_.Exception.Message});Write-Output "FAIL $Name`: $($_.Exception.Message)"}
}
function Reject([scriptblock]$Action) {try {& $Action|Out-Null}catch{return};throw 'Invalid evidence was accepted.'}
Check 'UDP supervisor and evidence APIs exist' {
    foreach($name in @('Test-UdpUnrealReady','Test-UdpUnrealGroupEvidence','Test-UdpUnrealCaseEvidence','Start-UdpUnrealChild','Complete-UdpUnrealChild','Write-UdpUnrealJson')) {
        if(-not(Get-Command $name -ErrorAction SilentlyContinue)){throw "Missing $name"}
    }
}
if(@($results|Where-Object{-not $_.passed}).Count) {
    $results|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $testRoot 'results.json')
    throw 'UDP evidence API contract RED: implementation is missing.'
}
$fixture=Get-SdkRepositoryPath $CoreFixture
$fixtureReport=Read-UnrealJson (Join-Path $fixture 'peer-0-report.json')
$sha=$fixtureReport.git_sha
function New-GroupFixture([string]$Name) {
    $root=Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path $root -Force|Out-Null
    $children=@()
    foreach($peer in 0,1) {
        $label=if($peer -eq 0){'a'}else{'b'}
        $directory=Join-Path $root ('peer-'+$label)
        New-Item -ItemType Directory -Path (Join-Path $directory 'captures') -Force|Out-Null
        $report=Read-UnrealJson (Join-Path $fixture "peer-$peer-report.json")
        $status=Read-UnrealJson (Join-Path $fixture "peer-$peer-terminal.json")
        $ready=Read-UnrealJson (Join-Path $fixture "peer-$peer-initial.json")
        $trace=[ordered]@{schema_version=2;mode='udp_peer';source_git_sha=$sha;sdk_version='0.2.0-candidate';abi_version=1;protocol_version=1;simulation_version=1;
            pid=1001+$peer;local_peer=$peer;session_count=1;local_port=$report.listen_port;relay_port=$report.relay_port;
            scenario_seed=$report.scenario_seed;transport_seed=$report.transport_seed;target_frame=$report.frame_count;logical_tick=500;
            confirmed_frame=$report.confirmed_frame;final_hash=$report.final_hash_a;rollback_count=$report.rollback_count;resimulated_frames=$report.resimulated_frames;
            handshake_complete=$true;desync_detected=$false;earliest_divergent_frame=0;replay_verified=$true;success=$true;failure_reason='';core_report=$report;core_status=$status}
        Write-UdpUnrealJson $trace (Join-Path $directory 'ue-trace.json')
        Write-UdpUnrealJson $report (Join-Path $directory 'report.json')
        Write-UdpUnrealJson $ready (Join-Path $directory 'ready.json')
        Copy-Item -LiteralPath (Join-Path $fixture "peer-$peer.rlr") -Destination (Join-Path $directory 'input.rlr')
        foreach($capture in @('start','convergence')+$(if($report.rollback_count -gt 0){'correction'})) {
            $bitmap=[Drawing.Bitmap]::new(32,32)
            try {$bitmap.SetPixel(1,1,[Drawing.Color]::Red);$bitmap.Save((Join-Path $directory "captures/$capture.png"),[Drawing.Imaging.ImageFormat]::Png)}finally{$bitmap.Dispose()}
        }
        $children+=@{name='peer-'+$label;pid=1001+$peer;exit_code=0;exited=$true;cleanup='exited';source_git_sha=$sha;source_clean=$true;program='synthetic owned process fixture';arguments=@()}
    }
    $children+=@{name='relay';pid=1003;exit_code=0;exited=$true;cleanup='exited';source_git_sha=$sha;source_clean=$true;program='synthetic owned relay fixture';arguments=@()}
    $outer=@{source_git_sha=$sha;source_clean=$true;pid=1000;exit_code=0;timed_out=$false;all_children_exited=$true;total_processes=4}
    Write-UdpUnrealJson @{processes=$children} (Join-Path $root 'children.json')
    Write-UdpUnrealJson $outer (Join-Path $root 'udp-supervisor.process.json')
    return $root
}
function Verify([string]$Root) {Test-UdpUnrealGroupEvidence -Run $Root -ExpectedGitSha $sha -ScenarioSeed $fixtureReport.scenario_seed -TransportSeed $fixtureReport.transport_seed -Frames $fixtureReport.frame_count}
Check 'asymmetric actual Core reports accept one peer without rollback and distinct report identities' {
    $root=New-GroupFixture 'asymmetric'
    $result=Verify $root
    if($result.rollback_count -lt 1 -or $result.predicted_input_count -lt 1 -or $result.peers.Count -ne 2){throw 'Aggregate rollback evidence lost.'}
}
foreach($mutation in @('wrong-pid','two-sessions','wrong-peer','same-port','stale-source','profile-mismatch','advertised-mismatch','report-disagreement','unconfirmed','false-replay','no-prediction','no-rollback','missing-correction','missing-start','changed-replay','live-child','dirty-source','nonzero-exit','absent-boolean','duplicate-json')) {
    Check "group rejects $mutation" {
        $root=New-GroupFixture $mutation
        $path=Join-Path $root 'peer-b/ue-trace.json';$trace=Read-UnrealJson $path
        $records=Read-UnrealJson (Join-Path $root 'children.json')
        switch($mutation) {
            'wrong-pid' {$trace.pid=9999}
            'two-sessions' {$trace.session_count=2}
            'wrong-peer' {$trace.local_peer=0}
            'same-port' {$trace.local_port=$fixtureReport.listen_port}
            'stale-source' {$trace.source_git_sha='0'*40}
            'profile-mismatch' {$trace.core_status.udp_profile='legacy-udp-v1'}
            'advertised-mismatch' {$trace.core_status.advertised_abi_profile_version=2}
            'report-disagreement' {$trace.core_report.sent_packets++}
            'unconfirmed' {$trace.confirmed_frame--}
            'false-replay' {$trace.replay_verified=$false}
            'no-prediction' {$trace.core_report.predicted_input_count=0;Write-UdpUnrealJson $trace.core_report (Join-Path $root 'peer-b/report.json')}
            'no-rollback' {$trace.rollback_count=0;$trace.resimulated_frames=0;$trace.core_report.rollback_count=0;$trace.core_report.resimulated_frames=0;Write-UdpUnrealJson $trace.core_report (Join-Path $root 'peer-b/report.json')}
            'missing-correction' {Remove-Item -LiteralPath (Join-Path $root 'peer-b/captures/correction.png')}
            'missing-start' {Remove-Item -LiteralPath (Join-Path $root 'peer-a/captures/start.png')}
            'changed-replay' {[IO.File]::AppendAllText((Join-Path $root 'peer-b/input.rlr'),'x')}
            'live-child' {$records.processes[1].exited=$false}
            'dirty-source' {$records.processes[1].source_clean=$false}
            'nonzero-exit' {$records.processes[1].exit_code=1}
            'absent-boolean' {$trace.Remove('handshake_complete')}
            'duplicate-json' {}
        }
        Write-UdpUnrealJson $trace $path
        Write-UdpUnrealJson $records (Join-Path $root 'children.json')
        if($mutation -eq 'duplicate-json'){[IO.File]::WriteAllText($path,'{"success":true,"Success":false}')}
        Reject {Verify $root}
    }
}
Check 'ready status requires healthy handshake phase and actual ephemeral port' {
    $ready=Read-UnrealJson (Join-Path $fixture 'peer-0-initial.json')
    $parameters=@{Ready=$ready;ExpectedGitSha=$sha;LocalPeer=0;RelayPort=$ready.relay_port;ScenarioSeed=$ready.scenario_seed;TransportSeed=$ready.transport_seed;Frames=$ready.target_frame}
    Test-UdpUnrealReady @parameters
    $ready.listen_port=0;Reject {Test-UdpUnrealReady @parameters}
    $ready.listen_port=59864;$ready.phase=1;Reject {Test-UdpUnrealReady @parameters}
}
function New-NegativeFixture([string]$Name,[string]$CaseName) {
    $root=New-GroupFixture $Name
    $journal=Read-UnrealJson (Join-Path $root 'children.json')
    if($CaseName -eq 'MissingPeer'){$journal.processes=@($journal.processes|Where-Object{$_.name -ne 'peer-b'})}
    foreach($child in $journal.processes) {
        if($child.name -eq 'relay'){continue}
        $child.exit_code=1
        $path=Join-Path $root ($child.name+'/ue-trace.json');$trace=Read-UnrealJson $path
        $trace.success=$false;$trace.replay_verified=$false;$trace.failure_reason='Synthetic SDK failure fixture';$trace.core_report=$null
        $trace.core_status.phase=4;$trace.core_status.error_code=1;$trace.core_status.context='peer_handshake';$trace.core_status.detail=15000
        $trace.core_status.handshake_complete=$false;$trace.handshake_complete=$false
        $readyPath=Join-Path $root ($child.name+'/ready.json');$ready=Read-UnrealJson $readyPath
        if($child.name -eq 'peer-b') {
            switch($CaseName) {
                'ProtocolMismatch' {$trace.core_status.advertised_protocol_version=2;$ready.advertised_protocol_version=2}
                'SimulationMismatch' {$trace.core_status.advertised_simulation_version=2;$ready.advertised_simulation_version=2}
                'AbiMismatch' {$trace.core_status.advertised_abi_profile_version=2;$ready.advertised_abi_profile_version=2}
            }
        }
        if($child.name -eq 'peer-a') {
            switch($CaseName) {
                'ProtocolMismatch' {$trace.core_status.context='version';$trace.core_status.detail=2}
                'SimulationMismatch' {$trace.core_status.context='udp_simulation_version';$trace.core_status.detail=2}
                'AbiMismatch' {$trace.core_status.context='udp_engine_profile'}
                'Desync' {
                    $trace.desync_detected=$true;$trace.earliest_divergent_frame=100;$trace.confirmed_frame=100;$trace.handshake_complete=$true
                    $trace.core_status.handshake_complete=$true;$trace.core_status.context='confirmed_hash'
                    $trace.core_status.diagnostic=@{earliest_divergent_frame=100;local_hash='0x0123456789ABCDEF';remote_hash='0xFEDCBA9876543210';scenario_seed=$fixtureReport.scenario_seed}
                }
            }
        }
        Write-UdpUnrealJson $trace $path;Write-UdpUnrealJson $ready $readyPath
    }
    Write-UdpUnrealJson $journal (Join-Path $root 'children.json')
    $outer=Read-UnrealJson (Join-Path $root 'udp-supervisor.process.json');$outer.exit_code=1
    Write-UdpUnrealJson $outer (Join-Path $root 'udp-supervisor.process.json')
    Write-UdpUnrealJson @{schema_version=1;mode='udp_ue_group';case=$CaseName;source_git_sha=$sha;source_clean=$true;scenario_seed=$fixtureReport.scenario_seed;
        transport_seed=$fixtureReport.transport_seed;target_frame=$fixtureReport.frame_count;success=$false;exit_code=1;failure_reason='Synthetic failure fixture';all_children_exited=$true} (Join-Path $root 'summary.json')
    return $root
}
foreach($negative in @('MissingPeer','ProtocolMismatch','SimulationMismatch','AbiMismatch','Desync')) {
    Check "negative validator accepts bounded $negative diagnostic" {
        $root=New-NegativeFixture ('negative-'+$negative) $negative
        Test-UdpUnrealCaseEvidence -Run $root -ExpectedCase $negative
    }
}
foreach($mutation in @('wrong-status-source','wrong-ready-port','wrong-negative-peer','speculative-desync')) {
    Check "negative validator rejects $mutation" {
        $root=New-NegativeFixture ('negative-'+$mutation) Desync
        $path=Join-Path $root 'peer-a/ue-trace.json';$trace=Read-UnrealJson $path
        switch($mutation) {
            'wrong-status-source' {$trace.core_status.source_git_sha='0'*40}
            'wrong-ready-port' {$readyPath=Join-Path $root 'peer-a/ready.json';$ready=Read-UnrealJson $readyPath;$ready.listen_port=12345;Write-UdpUnrealJson $ready $readyPath}
            'wrong-negative-peer' {$trace.local_peer=1}
            'speculative-desync' {$trace.confirmed_frame=99}
        }
        Write-UdpUnrealJson $trace $path
        Reject {Test-UdpUnrealCaseEvidence -Run $root -ExpectedCase Desync}
    }
}
Check 'Start-Process argv preserves spaces quotes trailing slashes empty arguments and shell metacharacters' {
    $root=Join-Path $testRoot 'quoting';New-Item -ItemType Directory -Path $root|Out-Null
    Invoke-UnrealProcess -Context @{Run=$root} -FilePath (Get-Process -Id $PID).Path -Arguments @('-NoProfile','-File',$PSCommandPath,'-FixtureMode','QuoteSupervisor','-FixtureRoot',$root) -Name quote -TimeoutSeconds 15
    $actual=Read-UnrealJson (Join-Path $root 'argv.json')
    Assert-UnrealJsonEqual -Expected @('with spaces','embedded"quote','C:\trailing slash\','', 'literal$()`backtick') -Actual $actual.arguments
}
Check 'existing outer job reaps a real descendant after supervisor exits' {
    $root=Join-Path $testRoot 'orphan';New-Item -ItemType Directory -Path $root|Out-Null
    Invoke-UnrealProcess -Context @{Run=$root} -FilePath (Get-Process -Id $PID).Path -Arguments @('-NoProfile','-File',$PSCommandPath,'-FixtureMode','OrphanSupervisor','-FixtureRoot',$root) -Name orphan -TimeoutSeconds 10
    $record=Read-UnrealJson (Join-Path $root 'orphan.process.json')
    if($record.total_processes -lt 2 -or -not $record.all_children_exited){throw 'Owned descendant cleanup not proven.'}
}
Check 'summary persists without claiming a hash for an exclusively locked diagnostic file' {
    $root=Join-Path $testRoot 'locked-summary';New-Item -ItemType Directory -Path $root|Out-Null
    $path=Join-Path $root 'writer.stderr.txt'
    $locked=[IO.File]::Open($path,[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $locked.Write([Text.Encoding]::UTF8.GetBytes('incomplete diagnostic bytes'));$locked.Flush()
        $summary=@{schema_version=1;success=$false;exit_code=1;failure_reason='Watchdog fixture expired.';timed_out=$true;all_children_exited=$true;files=@()}
        $null=Complete-UdpUnrealSummary -Summary $summary -Run $root -ReadinessTimeoutMs 50
        $actual=Read-UnrealJson (Join-Path $root 'summary.json')
        $entry=@($actual.files|Where-Object{$_.path -ceq 'writer.stderr.txt'})[0]
        if($actual.success -ne $false -or $actual.evidence_status -cne 'diagnostic-partial' -or $actual.file_hashes_complete -ne $false -or
           $entry.status -cne 'unavailable' -or $null -ne $entry.sha256 -or $entry.win32_error -ne 32 -or [string]::IsNullOrWhiteSpace($entry.error)) {throw 'Locked bytes were omitted or falsely certified.'}
    } finally {$locked.Dispose()}
}
Check 'summary converts apparent Normal success to failure when final integrity is unavailable' {
    $root=Join-Path $testRoot 'normal-locked-summary';New-Item -ItemType Directory -Path $root|Out-Null
    $locked=[IO.File]::Open((Join-Path $root 'log.txt'),[IO.FileMode]::Create,[IO.FileAccess]::Write,[IO.FileShare]::None)
    try {
        $summary=@{schema_version=1;success=$true;exit_code=0;failure_reason='';timed_out=$false;all_children_exited=$true;files=@()}
        $null=Complete-UdpUnrealSummary -Summary $summary -Run $root -ReadinessTimeoutMs 0
        $actual=Read-UnrealJson (Join-Path $root 'summary.json')
        if($actual.success -ne $false -or $actual.exit_code -eq 0 -or $actual.file_hashes_complete -ne $false -or [string]::IsNullOrWhiteSpace($actual.failure_reason)){throw 'Normal success survived incomplete final integrity.'}
    } finally {$locked.Dispose()}
}
Check 'watchdog reaps multiple real redirected children and always preserves a diagnostic summary' {
    for($attempt=0;$attempt -lt 3;$attempt++) {
        $root=Join-Path $testRoot ('watchdog-'+$attempt);New-Item -ItemType Directory -Path $root|Out-Null
        $watch=[Diagnostics.Stopwatch]::StartNew();$failure=''
        try {Invoke-UnrealProcess -Context @{Run=$root} -FilePath (Get-Process -Id $PID).Path -Arguments @('-NoProfile','-File',$PSCommandPath,'-FixtureMode','WatchdogSupervisor','-FixtureRoot',$root) -Name watchdog -TimeoutSeconds 5}
        catch {$failure=$_.Exception.Message}
        $outer=Read-UnrealJson (Join-Path $root 'watchdog.process.json')
        if(-not $outer.timed_out -or -not $outer.all_children_exited -or $outer.total_processes -lt 4){throw 'Actual redirected descendant watchdog was not exercised.'}
        $children=(Read-UnrealJson (Join-Path $root 'children.json')).processes
        if($children.Count -ne 3){throw 'Watchdog fired before all redirected writers started.'}
        # Observe the original operation immediately after Job completion. Keep
        # any real transient sharing failures as evidence rather than inventing them.
        $probe=@(foreach($child in $children){foreach($path in @($child.stdout,$child.stderr)) {
            try {$hash=(Get-FileHash -LiteralPath $path -ErrorAction Stop).Hash;@{path=$path;readable=$true;sha256=$hash}}
            catch {@{path=$path;readable=$false;error=$_.Exception.Message}}
        }})
        Write-UdpUnrealJson @{elapsed_ms=$watch.ElapsedMilliseconds;files=$probe} (Join-Path $root 'immediate-hash-probe.json')
        $summary=@{schema_version=1;success=$false;exit_code=1;failure_reason=$failure;timed_out=$true;all_children_exited=$outer.all_children_exited;processes=$children;files=@()}
        $remaining=[Math]::Max(0,[Math]::Min(10000,15000-$watch.ElapsedMilliseconds))
        $null=Complete-UdpUnrealSummary -Summary $summary -Run $root -ReadinessTimeoutMs $remaining
        $actual=Read-UnrealJson (Join-Path $root 'summary.json')
        if($actual.success -ne $false -or $actual.evidence_status -cne 'diagnostic-partial' -or $actual.file_hashes_complete -ne $true -or -not $actual.all_children_exited){throw 'Watchdog finalization lost cleanup or available bytes.'}
        foreach($child in $children){foreach($path in @($child.stdout,$child.stderr)) {
            $entry=@($actual.files|Where-Object{$_.path -ceq [IO.Path]::GetFileName($path)})[0]
            if($entry.sha256 -ine (Get-FileHash -LiteralPath $path).Hash){throw 'Post-watchdog file hash is not its actual stable bytes.'}
        }}
    }
}
if($SdkRoot) {
    Check 'existing canonical replay CLI verifies both real Core fixture replays' {
        $cli=Join-Path (Get-SdkRepositoryPath $SdkRoot) 'bin/rollback_lab.exe'
        foreach($peer in 0,1){Invoke-UnrealProcess -Context @{Run=$testRoot} -FilePath $cli -Arguments @('replay',(Join-Path $fixture "peer-$peer.rlr")) -Name "replay-$peer" -TimeoutSeconds 10}
    }
}
[ordered]@{schema_version=1;fixture_only=$true;note='Synthetic UE wrappers/captures and owned helper processes; real Core replay inputs. This is not packaged UE proof.';results=@($results)}|
    ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $testRoot 'results.json') -Encoding utf8NoBOM
$failed=@($results|Where-Object{-not $_.passed}).Count
Write-Output "UDP evidence tests: $($results.Count-$failed)/$($results.Count) passed; $testRoot"
if($failed){throw "$failed UDP evidence tests failed."}
