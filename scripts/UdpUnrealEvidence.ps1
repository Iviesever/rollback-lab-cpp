#requires -Version 7.0
. (Join-Path $PSScriptRoot 'UnrealEvidence.ps1')

function Write-UdpUnrealJson {
    param([Parameter(Mandatory)]$Value,[Parameter(Mandatory)][string]$Path)
    $path=Get-SdkRepositoryPath $Path;Assert-UnrealOrdinaryPath $path
    $temporary=$path+'.tmp-'+$PID+'-'+[guid]::NewGuid().ToString('N')
    try {
        [IO.File]::WriteAllText($temporary,($Value|ConvertTo-Json -Depth 100),[Text.UTF8Encoding]::new($false))
        [IO.File]::Move($temporary,$path,$true)
    } finally {if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary -Force}}
}

function Complete-UdpUnrealSummary {
    param([Parameter(Mandatory)]$Summary,[Parameter(Mandatory)][string]$Run,
        [ValidateRange(0,10000)][int]$ReadinessTimeoutMs=10000)
    $root=Get-SdkRepositoryPath $Run;$summaryPath=Join-Path $root 'summary.json'
    $wasSuccessful=$Summary.success -eq $true
    $originalFailure=[string]$Summary.failure_reason
    $clock=[Diagnostics.Stopwatch]::StartNew()
    $Summary.success=$false;$Summary.exit_code=1
    $Summary.file_hashes_complete=$false;$Summary.evidence_status='collecting'
    $Summary.evidence_error='';$Summary.evidence_wait_ms=0;$Summary.files=@()
    if([string]::IsNullOrEmpty($Summary.failure_reason)){$Summary.failure_reason='Evidence collection has not completed.'}
    # Materialize fail-closed diagnostics before touching any possibly locked
    # redirected output. Even an interrupted finalizer leaves an honest summary.
    Write-UdpUnrealJson $Summary $summaryPath
    $entries=[Collections.Generic.List[object]]::new();$inventoryComplete=$false
    try {
        $inventory=Get-UnrealFileInventory $root
        $pending=[Collections.Generic.List[string]]::new()
        $byPath=@{}
        foreach($relative in ($inventory.Keys|Sort-Object -CaseSensitive)) {
            if($relative -eq 'summary.json'){continue}
            $entry=[ordered]@{path=$relative;sha256=$null;bytes=$null;status='unavailable';error='File has not been opened.';win32_error=$null;attempts=0}
            $entries.Add($entry);$byPath[$relative]=$entry;$pending.Add($relative)
        }
        $inventoryComplete=$true
        do {
            $retry=[Collections.Generic.List[string]]::new()
            foreach($relative in $pending) {
                $entry=$byPath[$relative];$entry.attempts++
                $stream=$null;$hasher=$null
                try {
                    # Job accounting proves ownership cleanup, but forced process
                    # teardown can briefly retain redirected-file handles. Open
                    # with read sharing only: deny writes/deletes while hashing
                    # the same handle, rather than accepting a changing snapshot.
                    $stream=[IO.File]::Open($inventory[$relative],[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
                    $hasher=[Security.Cryptography.SHA256]::Create()
                    $entry.sha256=[Convert]::ToHexString($hasher.ComputeHash($stream)).ToLowerInvariant()
                    $entry.bytes=$stream.Length;$entry.status='hashed';$entry.error='';$entry.win32_error=$null
                } catch {
                    $failure=$_.Exception
                    while($null -ne $failure.InnerException){$failure=$failure.InnerException}
                    $entry.sha256=$null;$entry.bytes=$null;$entry.status='unavailable'
                    $entry.error=$failure.Message;$entry.win32_error=$failure.HResult -band 0xffff
                    # Only readiness conditions retry. Missing files, access
                    # denial, malformed paths and other I/O failures stay explicit.
                    if($entry.win32_error -in @(32,33)){$retry.Add($relative)}
                } finally {
                    if($null -ne $hasher){$hasher.Dispose()}
                    if($null -ne $stream){$stream.Dispose()}
                }
            }
            $pending=$retry
            $remaining=$ReadinessTimeoutMs-$clock.ElapsedMilliseconds
            if($pending.Count -eq 0 -or $remaining -le 0){break}
            Start-Sleep -Milliseconds ([int][Math]::Min(20,$remaining))
        } while($true)
    } catch {$Summary.evidence_error=$_.Exception.Message}
    finally {
        $Summary.files=@($entries)
        $unavailable=@($entries|Where-Object{$_.status -cne 'hashed'})
        $Summary.file_hashes_complete=$inventoryComplete -and $unavailable.Count -eq 0 -and [string]::IsNullOrEmpty($Summary.evidence_error)
        if($unavailable.Count) {
            $Summary.evidence_error='Unavailable evidence files: '+(($unavailable|ForEach-Object{$_.path+' (Win32 '+$_.win32_error+')'}) -join ', ')
        }
        $Summary.evidence_wait_ms=$clock.ElapsedMilliseconds
        $Summary.evidence_status=if($Summary.timed_out -eq $true -or -not $Summary.file_hashes_complete){'diagnostic-partial'}else{'complete'}
        if($wasSuccessful -and $Summary.file_hashes_complete -and $Summary.timed_out -ne $true) {
            $Summary.success=$true;$Summary.exit_code=0;$Summary.failure_reason=$originalFailure
        } elseif(-not [string]::IsNullOrEmpty($originalFailure)) {$Summary.failure_reason=$originalFailure}
        else {$Summary.failure_reason='Final evidence integrity is incomplete. '+$Summary.evidence_error}
        Write-UdpUnrealJson $Summary $summaryPath
    }
    return $Summary
}

function ConvertTo-UdpUnrealArgumentString {
    param([AllowEmptyCollection()][AllowEmptyString()][string[]]$Arguments)
    # Microsoft CRT argv rules, also used by native/ProcessRunner.cs. Start-Process
    # joins ArgumentList itself, so supply one complete, quoted command line.
    $quoted=foreach($value in $Arguments) {
        if($value.Contains([char]0)){throw 'Process arguments cannot contain NUL.'}
        $builder=[Text.StringBuilder]::new('"');$slashes=0
        foreach($character in $value.ToCharArray()) {
            if($character -eq '\'){$slashes++;continue}
            if($character -eq '"'){$null=$builder.Append('\',2*$slashes+1).Append('"')}
            else {$null=$builder.Append('\',$slashes).Append($character)}
            $slashes=0
        }
        $builder.Append('\',2*$slashes).Append('"').ToString()
    }
    return ($quoted -join ' ')
}

function Start-UdpUnrealChild {
    param([Parameter(Mandatory)][string]$Name,[Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][AllowEmptyString()][string[]]$Arguments,[Parameter(Mandatory)][string]$Directory,
        $Identity,[switch]$Visible)
    if($Name -cnotmatch '^[a-z0-9-]+$'){throw 'Invalid owned process name.'}
    $directory=Get-SdkRepositoryPath $Directory;Assert-UnrealOrdinaryPath $directory
    if(-not [IO.Path]::IsPathFullyQualified($FilePath) -or -not(Test-Path -LiteralPath $FilePath -PathType Leaf)){throw 'Owned executable must be an existing absolute path.'}
    $filePath=[IO.Path]::GetFullPath($FilePath)
    if($null -eq $Identity){$Identity=Get-SdkSourceIdentity}
    $record=[ordered]@{name=$Name;source_git_sha=$Identity.Sha;source_clean=[bool]$Identity.Clean;
        program=$filePath;arguments=@($Arguments);pid=$null;exit_code=$null;exited=$false;cleanup='running';
        stdout=(Join-Path $directory ($Name+'.stdout.txt'));stderr=(Join-Path $directory ($Name+'.stderr.txt'))}
    $style=if($Visible){'Normal'}else{'Hidden'}
    $process=Start-Process -FilePath $filePath -ArgumentList (ConvertTo-UdpUnrealArgumentString $Arguments) `
        -WorkingDirectory $script:SdkRepositoryRoot -WindowStyle $style -RedirectStandardOutput $record.stdout `
        -RedirectStandardError $record.stderr -PassThru
    # Retain this exact handle even if the OS later recycles its numeric PID.
    $null=$process.Handle
    $record.pid=$process.Id
    return [pscustomobject]@{Handle=$process;Record=$record}
}

function Complete-UdpUnrealChild {
    param([Parameter(Mandatory)]$Child)
    if($Child.Handle.HasExited) {
        $Child.Record.exit_code=$Child.Handle.ExitCode
        $Child.Record.exited=$true
        if($Child.Record.cleanup -eq 'running'){$Child.Record.cleanup='exited'}
    }
}

function Assert-UdpUnrealInteger {
    param($Value,[string]$Label,[decimal]$Minimum=0,[decimal]$Maximum=4294967295)
    if($Value -isnot [byte] -and $Value -isnot [int16] -and $Value -isnot [uint16] -and $Value -isnot [int] -and
       $Value -isnot [uint32] -and $Value -isnot [long] -and $Value -isnot [uint64] -and $Value -isnot [System.Numerics.BigInteger]) {
        throw "Missing or noninteger UDP $Label."
    }
    if([decimal]$Value -lt $Minimum -or [decimal]$Value -gt $Maximum){throw "UDP $Label outside bounds."}
}

function Assert-UdpUnrealIdentity {
    param($Status,[string]$ExpectedGitSha,[int]$LocalPeer,[int]$RelayPort,[uint64]$ScenarioSeed,[uint64]$TransportSeed,[uint32]$Frames,
        [uint32]$HelloProtocol=1,[uint32]$HelloSimulation=1,[uint32]$HelloAbi=1)
    if($null -eq $Status -or $Status.source_git_sha -cne $ExpectedGitSha -or $Status.schema_version -ne 1 -or
       $Status.udp_profile -cne 'engine-udp-v1' -or $Status.profile -cne 'engine-udp-v1' -or
       $Status.abi_version -ne 1 -or $Status.expected_abi_version -ne 1 -or $Status.protocol_version -ne 1 -or $Status.simulation_version -ne 1){throw 'UDP SDK status source/profile/version mismatch.'}
    Assert-UdpUnrealInteger $Status.listen_port 'listen port' 1 65535
    Assert-UdpUnrealInteger $Status.local_peer 'local peer' 0 1
    Assert-UdpUnrealInteger $Status.relay_port 'relay port' 1 65535
    Assert-UdpUnrealInteger $Status.scenario_seed 'scenario seed' 0 ([decimal]::Parse('18446744073709551615'))
    Assert-UdpUnrealInteger $Status.transport_seed 'transport seed' 0 ([decimal]::Parse('18446744073709551615'))
    Assert-UdpUnrealInteger $Status.advertised_config_digest 'configuration digest' 1 ([decimal]::Parse('18446744073709551615'))
    if($Status.local_peer -ne $LocalPeer -or $Status.relay_port -ne $RelayPort -or $Status.listen_port -eq $RelayPort -or
       $Status.scenario_seed -ne $ScenarioSeed -or $Status.transport_seed -ne $TransportSeed -or $Status.target_frame -ne $Frames -or
       $Status.advertised_protocol_version -ne $HelloProtocol -or $Status.advertised_simulation_version -ne $HelloSimulation -or
       $Status.advertised_abi_profile_version -ne $HelloAbi){throw 'UDP SDK status requested peer/port/scenario/profile mismatch.'}
}

function Test-UdpUnrealReady {
    param([Parameter(Mandatory)]$Ready,[Parameter(Mandatory)][string]$ExpectedGitSha,[Parameter(Mandatory)][int]$LocalPeer,
        [Parameter(Mandatory)][int]$RelayPort,[uint64]$ScenarioSeed,[uint64]$TransportSeed,[uint32]$Frames,
        [uint32]$HelloProtocol=1,[uint32]$HelloSimulation=1,[uint32]$HelloAbi=1)
    $identity=@{};foreach($key in $PSBoundParameters.Keys){if($key -ne 'Ready'){$identity[$key]=$PSBoundParameters[$key]}}
    Assert-UdpUnrealIdentity @identity -Status $Ready
    if($Ready.sdk_status -ne 0 -or $Ready.phase -ne 0 -or $Ready.error_code -ne 0 -or $Ready.context -cne 'none' -or
       $Ready.handshake_complete -isnot [bool] -or $Ready.handshake_complete){throw 'UDP ready file is not a healthy bound handshake status.'}
    return $Ready
}

function Assert-UdpUnrealProcessRecord {
    param($Record,[string]$ExpectedGitSha,[switch]$AllowDirty,[switch]$RequireSuccess)
    if($null -eq $Record -or $Record.source_git_sha -cne $ExpectedGitSha -or $Record.source_clean -isnot [bool] -or
       (-not $AllowDirty -and -not $Record.source_clean)){throw 'UDP process source is stale, dirty, or unknown.'}
    Assert-UdpUnrealInteger $Record.pid 'process PID' 1
    Assert-UdpUnrealInteger $Record.exit_code 'exit code' -2147483648 4294967295
    if($Record.exited -isnot [bool] -or -not $Record.exited -or $Record.cleanup -notin @('exited','terminated')){throw 'UDP process was not completely cleaned up.'}
    if($RequireSuccess -and ($Record.exit_code -ne 0 -or $Record.cleanup -cne 'exited')){throw 'UDP child did not exit successfully on its own.'}
}

function Test-UdpUnrealGroupEvidence {
    param([Parameter(Mandatory)][string]$Run,[Parameter(Mandatory)][string]$ExpectedGitSha,
        [uint64]$ScenarioSeed=12648430,[uint64]$TransportSeed=1430540336,[ValidateRange(1,240)][uint32]$Frames=240,[switch]$AllowDirty)
    $root=Get-SdkRepositoryPath $Run;$null=Get-UnrealFileInventory $root
    $outer=Read-UnrealJson (Join-Path $root 'udp-supervisor.process.json')
    if($outer.source_git_sha -cne $ExpectedGitSha -or $outer.source_clean -isnot [bool] -or (-not $AllowDirty -and -not $outer.source_clean) -or
       $outer.exit_code -ne 0 -or $outer.timed_out -isnot [bool] -or $outer.timed_out -or
       $outer.all_children_exited -isnot [bool] -or -not $outer.all_children_exited -or $outer.total_processes -lt 4){throw 'UDP outer job failed, timed out, or retained descendants.'}
    $children=(Read-UnrealJson (Join-Path $root 'children.json')).processes
    if(@($children).Count -ne 3){throw 'UDP demo requires exactly two owned games and one owned relay.'}
    $pids=[Collections.Generic.HashSet[long]]::new();$ports=[Collections.Generic.HashSet[int]]::new()
    foreach($name in @('peer-a','peer-b','relay')) {
        $matching=@($children|Where-Object{$_.name -ceq $name})
        if($matching.Count -ne 1){throw 'UDP child identities are missing or duplicated.'}
        Assert-UdpUnrealProcessRecord $matching[0] $ExpectedGitSha -AllowDirty:$AllowDirty -RequireSuccess
        if(-not $pids.Add($matching[0].pid) -or $matching[0].pid -eq $outer.pid){throw 'UDP process PIDs are not independent.'}
    }
    $peers=@();[uint64]$rollbacks=0;[uint64]$predictions=0;[uint64]$resimulated=0;$relayPort=0
    foreach($peer in 0,1) {
        $label=if($peer -eq 0){'a'}else{'b'};$directory=Join-Path $root ('peer-'+$label)
        $trace=Read-UnrealJson (Join-Path $directory 'ue-trace.json')
        $report=Read-UnrealJson (Join-Path $directory 'report.json')
        $ready=Read-UnrealJson (Join-Path $directory 'ready.json')
        $process=@($children|Where-Object{$_.name -ceq ('peer-'+$label)})[0]
        if($trace.schema_version -ne 2 -or $trace.mode -cne 'udp_peer' -or $trace.source_git_sha -cne $ExpectedGitSha -or
           $trace.sdk_version -cne '0.2.0-candidate' -or $trace.abi_version -ne 1 -or $trace.protocol_version -ne 1 -or $trace.simulation_version -ne 1 -or
           $trace.pid -ne $process.pid -or $trace.local_peer -ne $peer -or $trace.session_count -ne 1){throw 'UDP wrapper source/PID/peer/session identity mismatch.'}
        foreach($key in @('success','replay_verified','handshake_complete')) {
            if($trace[$key] -isnot [bool] -or -not $trace[$key]){throw "UDP peer did not prove $key."}
        }
        if($trace.failure_reason -cne '' -or $trace.desync_detected -isnot [bool] -or $trace.desync_detected -or
           $trace.confirmed_frame -ne $Frames -or $trace.target_frame -ne $Frames -or $trace.scenario_seed -ne $ScenarioSeed -or
           $trace.transport_seed -ne $TransportSeed -or $trace.final_hash -cnotmatch '^0x[0-9A-F]{16}$'){throw 'UDP peer did not confirm the requested scenario.'}
        if($peer -eq 0){$relayPort=$trace.relay_port;$null=$ports.Add($relayPort)}
        $identity=@{ExpectedGitSha=$ExpectedGitSha;LocalPeer=$peer;RelayPort=$relayPort;ScenarioSeed=$ScenarioSeed;TransportSeed=$TransportSeed;Frames=$Frames}
        $null=Test-UdpUnrealReady -Ready $ready @identity
        Assert-UdpUnrealIdentity -Status $trace.core_status @identity
        if($trace.core_status.phase -ne 3 -or $trace.core_status.sdk_status -ne 0 -or $trace.core_status.error_code -ne 0 -or
           $trace.core_status.handshake_complete -isnot [bool] -or -not $trace.core_status.handshake_complete -or
           $trace.local_port -ne $ready.listen_port -or $trace.local_port -ne $trace.core_status.listen_port -or $trace.relay_port -ne $relayPort -or
           $trace.core_status.advertised_config_digest -ne $ready.advertised_config_digest -or -not $ports.Add($trace.local_port)){throw 'UDP peer ports/profile or terminal SDK status mismatch.'}
        Assert-UnrealJsonEqual -Expected $report -Actual $trace.core_report -Label 'UDP embedded/standalone report'
        if($report.git_sha -cne $ExpectedGitSha -or $report.build_type -cne 'Release' -or $report.compiler -cne 'MSVC' -or $report.os -cne 'Windows' -or
           $report.simulation_version -ne 1 -or $report.protocol_version -ne 1 -or $report.pcg32_version -ne 1 -or $report.abi_version -ne 1 -or
           $report.udp_profile -cne 'engine-udp-v1' -or $report.local_peer -ne $peer -or $report.listen_port -ne $trace.local_port -or
           $report.relay_port -ne $relayPort -or $report.advertised_config_digest -ne $ready.advertised_config_digest -or
           $report.advertised_protocol_version -ne 1 -or $report.advertised_simulation_version -ne 1 -or $report.advertised_abi_profile_version -ne 1){throw 'UDP canonical report identity mismatch.'}
        if($report.success -isnot [bool] -or -not $report.success -or $report.replay_verification -isnot [bool] -or -not $report.replay_verification -or
           $report.failure_reason -cne '' -or $report.desync_result -cne 'none' -or $report.confirmed_frame -ne $Frames -or $report.frame_count -ne $Frames -or
           $report.scenario_seed -ne $ScenarioSeed -or $report.transport_seed -ne $TransportSeed -or $report.final_hash_a -cne $trace.final_hash -or
           $report.final_hash_b -cne $trace.final_hash -or $report.rollback_count -ne $trace.rollback_count -or $report.resimulated_frames -ne $trace.resimulated_frames -or
           $report.sent_packets -lt 1 -or $report.delivered_packets -lt 1 -or $report.identity_digest -cnotmatch '^0x[0-9A-F]{16}$'){throw 'UDP canonical report does not confirm convergence/replay/packets.'}
        foreach($key in @('rollback_count','resimulated_frames','predicted_input_count')){Assert-UdpUnrealInteger $report[$key] $key 0 ([decimal]::Parse('18446744073709551615'))}
        $captures=@(foreach($name in @('start','convergence')){Test-UnrealPng (Join-Path $directory ('captures/'+$name+'.png'))})
        if($trace.rollback_count -gt 0){$captures+=Test-UnrealPng (Join-Path $directory 'captures/correction.png')}
        $replay=Join-Path $directory 'input.rlr'
        if((Get-Item -LiteralPath $replay).Length -lt 1){throw 'UDP replay is empty.'}
        $rollbacks+=$report.rollback_count;$predictions+=$report.predicted_input_count;$resimulated+=$report.resimulated_frames
        $peers+=@([ordered]@{name='peer-'+$label;pid=$trace.pid;local_peer=$peer;session_count=$trace.session_count;local_port=$trace.local_port;relay_port=$relayPort;
            confirmed_frame=$trace.confirmed_frame;final_hash=$trace.final_hash;rollback_count=$trace.rollback_count;predicted_input_count=$report.predicted_input_count;
            resimulated_frames=$trace.resimulated_frames;replay=$replay;replay_sha256=(Get-FileHash -LiteralPath $replay -Algorithm SHA256).Hash.ToLowerInvariant();captures=$captures})
    }
    if($rollbacks -lt 1 -or $predictions -lt 1 -or $resimulated -lt 1){throw 'UDP group has no actual prediction and rollback evidence.'}
    if($peers[0].final_hash -cne $peers[1].final_hash -or $peers[0].replay_sha256 -cne $peers[1].replay_sha256){throw 'UDP peer hashes/replay bytes differ.'}
    # Compare the actual bounded replay bytes as well as their recorded SHA-256.
    if([Convert]::ToHexString([IO.File]::ReadAllBytes($peers[0].replay)) -cne [Convert]::ToHexString([IO.File]::ReadAllBytes($peers[1].replay))){throw 'UDP replay bytes differ.'}
    return [ordered]@{peers=$peers;relay_port=$relayPort;confirmed_frame=$Frames;final_hash=$peers[0].final_hash;
        rollback_count=$rollbacks;predicted_input_count=$predictions;resimulated_frames=$resimulated;replay_bytes_equal=$true}
}

function Test-UdpUnrealCaseEvidence {
    param([Parameter(Mandatory)][string]$Run,[Parameter(Mandatory)][ValidateSet('Normal','MissingPeer','ProtocolMismatch','SimulationMismatch','AbiMismatch','Watchdog','Desync')][string]$ExpectedCase)
    $root=Get-SdkRepositoryPath $Run;$summary=Read-UnrealJson (Join-Path $root 'summary.json')
    $outer=Read-UnrealJson (Join-Path $root 'udp-supervisor.process.json')
    if($summary.schema_version -ne 1 -or $summary.mode -cne 'udp_ue_group' -or $summary.case -cne $ExpectedCase -or
       $summary.source_git_sha -cnotmatch '^[a-f0-9]{40}$' -or $summary.source_clean -isnot [bool] -or
       $outer.source_git_sha -cne $summary.source_git_sha -or $outer.source_clean -isnot [bool] -or $outer.source_clean -ne $summary.source_clean -or
       $summary.all_children_exited -isnot [bool] -or -not $summary.all_children_exited -or
       $outer.all_children_exited -isnot [bool] -or -not $outer.all_children_exited){throw 'UDP case lacks complete outer job cleanup.'}
    if($ExpectedCase -eq 'Normal') {
        if($summary.success -isnot [bool] -or -not $summary.success -or $summary.exit_code -ne 0 -or
           $summary.canonical_replay_verified -isnot [bool] -or -not $summary.canonical_replay_verified){throw 'Normal UDP run failed.'}
        $null=Test-UdpUnrealGroupEvidence -Run $root -ExpectedGitSha $summary.source_git_sha -ScenarioSeed $summary.scenario_seed -TransportSeed $summary.transport_seed -Frames $summary.target_frame -AllowDirty:(-not $summary.source_clean)
    } else {
        if($summary.success -isnot [bool] -or $summary.success -or $summary.exit_code -eq 0 -or [string]::IsNullOrWhiteSpace($summary.failure_reason)){throw 'Negative UDP run did not fail closed.'}
        if($ExpectedCase -eq 'Watchdog') {
            if($outer.timed_out -isnot [bool] -or -not $outer.timed_out -or $outer.total_processes -lt 3){throw 'Watchdog did not interrupt actual owned game processes.'}
        } else {
            $children=(Read-UnrealJson (Join-Path $root 'children.json')).processes
            $expectedCount=if($ExpectedCase -eq 'MissingPeer'){2}else{3}
            if(@($children).Count -ne $expectedCount){throw 'Negative UDP run has wrong child count.'}
            $matched=$false;$ports=[Collections.Generic.HashSet[int]]::new();$pids=[Collections.Generic.HashSet[long]]::new();$relayPort=0
            $names=@($children|ForEach-Object{$_.name}|Sort-Object)
            $expectedNames=if($ExpectedCase -eq 'MissingPeer'){@('peer-a','relay')}else{@('peer-a','peer-b','relay')}
            Assert-UnrealJsonEqual -Expected $expectedNames -Actual $names -Label 'Negative owned child names'
            foreach($child in $children) {
                Assert-UdpUnrealProcessRecord $child $summary.source_git_sha -AllowDirty:(-not $summary.source_clean)
                if(-not $pids.Add($child.pid) -or $child.pid -eq $outer.pid){throw 'Negative UDP processes are not independent.'}
                if($child.name -eq 'relay'){if($child.exit_code -ne 0){throw 'Negative UDP relay failed.'};continue}
                $trace=Read-UnrealJson (Join-Path $root ($child.name+'/ue-trace.json'))
                $localPeer=if($child.name -eq 'peer-a'){0}else{1}
                if($trace.schema_version -ne 2 -or $trace.pid -ne $child.pid -or $trace.session_count -ne 1 -or
                   $trace.mode -cne 'udp_peer' -or $trace.local_peer -ne $localPeer -or $trace.abi_version -ne 1 -or $trace.sdk_version -cne '0.2.0-candidate' -or
                   $trace.source_git_sha -cne $summary.source_git_sha -or $trace.success -isnot [bool] -or $trace.success -or $child.exit_code -eq 0 -or
                   $trace.target_frame -ne $summary.target_frame -or $trace.scenario_seed -ne $summary.scenario_seed -or $trace.transport_seed -ne $summary.transport_seed -or
                   [string]::IsNullOrWhiteSpace($trace.failure_reason)){throw 'Negative UDP wrapper identity/failure missing.'}
                if($relayPort -eq 0){$relayPort=$trace.relay_port;$null=$ports.Add($relayPort)}
                $status=$trace.core_status
                $helloProtocol=1;$helloSimulation=1;$helloAbi=1
                if($localPeer -eq 1){switch($ExpectedCase){'ProtocolMismatch'{$helloProtocol=2};'SimulationMismatch'{$helloSimulation=2};'AbiMismatch'{$helloAbi=2}}}
                $identity=@{ExpectedGitSha=$summary.source_git_sha;LocalPeer=$localPeer;RelayPort=$relayPort;ScenarioSeed=$summary.scenario_seed;
                    TransportSeed=$summary.transport_seed;Frames=$summary.target_frame;HelloProtocol=$helloProtocol;HelloSimulation=$helloSimulation;HelloAbi=$helloAbi}
                Assert-UdpUnrealIdentity -Status $status @identity
                $ready=Read-UnrealJson (Join-Path $root ($child.name+'/ready.json'))
                $null=Test-UdpUnrealReady -Ready $ready @identity
                if($trace.local_port -ne $ready.listen_port -or $status.listen_port -ne $ready.listen_port -or $trace.relay_port -ne $relayPort -or
                   $status.advertised_config_digest -ne $ready.advertised_config_digest -or -not $ports.Add($trace.local_port)){throw 'Negative UDP bound port/profile identity differs from readiness.'}
                switch($ExpectedCase) {
                    'MissingPeer' {$matched=$matched -or ($status.context -ceq 'peer_handshake' -and -not $trace.handshake_complete)}
                    'ProtocolMismatch' {$matched=$matched -or ($status.context -ceq 'version' -and $status.detail -eq 2)}
                    'SimulationMismatch' {$matched=$matched -or ($status.context -ceq 'udp_simulation_version' -and $status.detail -eq 2)}
                    'AbiMismatch' {$matched=$matched -or ($status.context -ceq 'udp_engine_profile')}
                    'Desync' {
                        $diagnostic=$status.diagnostic
                        if($trace.desync_detected -is [bool] -and $trace.desync_detected -and $null -ne $diagnostic -and
                           $trace.handshake_complete -eq $true -and $trace.earliest_divergent_frame -ge 1 -and
                           $trace.earliest_divergent_frame -le $trace.confirmed_frame -and
                           $diagnostic.earliest_divergent_frame -eq $trace.earliest_divergent_frame -and
                           $diagnostic.local_hash -cne $diagnostic.remote_hash){$matched=$true}
                    }
                }
            }
            if(-not $matched){throw "UDP case did not produce expected $ExpectedCase SDK diagnostic."}
        }
    }
    return [ordered]@{case=$ExpectedCase;verified=$true;success=$summary.success;failure_reason=$summary.failure_reason;all_children_exited=$true;run=$root}
}
