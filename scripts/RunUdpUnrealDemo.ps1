#requires -Version 7.0
param([string]$EngineRoot,[string]$DemoRoot,[string]$SdkRoot,[switch]$AllowDirty,
    [ValidateSet('Normal','MissingPeer','ProtocolMismatch','SimulationMismatch','AbiMismatch','Watchdog','Desync')][string]$Case='Normal',
    [string]$ScenarioSeed='12648430',[string]$TransportSeed='1430540336',[ValidateRange(1,240)][uint32]$Frames=240,
    [ValidateRange(1,60000)][int]$HandshakeTimeoutMs=15000,[ValidateRange(1,60000)][int]$RunTimeoutMs=15000,
    [ValidateRange(1,180)][int]$ReadyTimeoutSeconds=60,[ValidateRange(1,300)][int]$PeerExitTimeoutSeconds=90,
    [ValidateRange(1,600)][int]$TimeoutSeconds=120,[ValidateRange(1,30)][int]$WatchdogSeconds=5,
    [string]$SupervisorConfig)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'UdpUnrealEvidence.ps1')

function New-UdpUnrealReservation {
    $socket=[Net.Sockets.UdpClient]::new([Net.Sockets.AddressFamily]::InterNetwork)
    try {
        $socket.Client.ExclusiveAddressUse=$true
        $socket.Client.Bind([Net.IPEndPoint]::new([Net.IPAddress]::Loopback,0))
        return $socket
    } catch {$socket.Dispose();throw}
}

function Send-UdpUnrealRelayStop([int]$Port) {
    $sender=[Net.Sockets.UdpClient]::new([Net.Sockets.AddressFamily]::InterNetwork)
    try {$bytes=[Text.Encoding]::ASCII.GetBytes('RLSTOP');$null=$sender.Send($bytes,$bytes.Length,[Net.IPEndPoint]::new([Net.IPAddress]::Loopback,$Port))}
    finally {$sender.Dispose()}
}

function Invoke-UdpUnrealSupervisor($Config) {
    $children=[Collections.Generic.List[object]]::new();$relayReservation=$null;$missingReservation=$null;$relay=$null
    $journal=[ordered]@{schema_version=1;source_git_sha=$Config.source_git_sha;source_clean=$Config.source_clean;
        supervisor_pid=$PID;success=$false;failure_reason='';relay_port=0;peer_ports=@();relay_reservation_released=$false;missing_peer_reservation_released=$true;processes=@()}
    $journalPath=Join-Path $Config.run 'children.json'
    $identity=[pscustomobject]@{Sha=$Config.source_git_sha;Clean=$Config.source_clean}
    $exitCode=1
    try {
        # Only the relay port is reserved and later released. Each game owns its
        # ephemeral listen socket continuously from ready publication to exit.
        $relayReservation=New-UdpUnrealReservation
        $journal.relay_port=$relayReservation.Client.LocalEndPoint.Port
        Write-UdpUnrealJson $journal $journalPath
        foreach($client in $Config.clients) {
            $arguments=@($client.arguments)+@('-RollbackLabListenPort=0',('-RollbackLabRelayPort='+$journal.relay_port))
            $child=Start-UdpUnrealChild -Name $client.name -FilePath $Config.game_exe -Arguments $arguments -Directory $Config.run -Identity $identity -Visible
            $children.Add($child);$journal.processes=@($children|ForEach-Object{$_.Record})
            Write-UdpUnrealJson $journal $journalPath
        }
        $readyClock=[Diagnostics.Stopwatch]::StartNew();$readyByPeer=@{}
        do {
            foreach($client in $Config.clients) {
                if($readyByPeer.ContainsKey($client.name)){continue}
                $child=@($children|Where-Object{$_.Record.name -ceq $client.name})[0]
                if(Test-Path -LiteralPath $client.ready -PathType Leaf) {
                    $ready=Read-UnrealJson $client.ready
                    $null=Test-UdpUnrealReady -Ready $ready -ExpectedGitSha $Config.source_git_sha -LocalPeer $client.peer -RelayPort $journal.relay_port `
                        -ScenarioSeed $Config.scenario_seed -TransportSeed $Config.transport_seed -Frames $Config.frames `
                        -HelloProtocol $client.hello_protocol -HelloSimulation $client.hello_simulation -HelloAbi $client.hello_abi
                    $readyByPeer[$client.name]=$ready
                } elseif($child.Handle.HasExited){throw "UDP $($client.name) exited before publishing healthy ready.json."}
            }
            if($readyByPeer.Count -eq $Config.clients.Count){break}
            if($readyClock.Elapsed.TotalSeconds -ge $Config.ready_timeout_seconds){throw 'UDP clients did not publish bound ports within the startup deadline.'}
            Start-Sleep -Milliseconds 20
        } while($true)
        $aPort=[int]$readyByPeer['peer-a'].listen_port
        if($Config.case -eq 'MissingPeer') {
            $missingReservation=New-UdpUnrealReservation;$bPort=$missingReservation.Client.LocalEndPoint.Port
            $journal.missing_peer_reservation_released=$false
        } else {$bPort=[int]$readyByPeer['peer-b'].listen_port}
        if($aPort -eq $bPort -or $aPort -eq $journal.relay_port -or $bPort -eq $journal.relay_port){throw 'UDP clients/relay did not bind distinct ports.'}
        $journal.peer_ports=@($aPort,$bPort)
        # A single bounded bind attempt follows this close. Any competing bind
        # fails closed; there is no port-picking retry or replacement peer socket.
        $relayReservation.Dispose();$relayReservation=$null;$journal.relay_reservation_released=$true
        $readyPath=Join-Path $Config.run 'relay.ready'
        $relay=Start-UdpUnrealChild -Name relay -FilePath $Config.relay_exe -Arguments @('relay','--relay-port',[string]$journal.relay_port,
            '--peer-a-port',[string]$aPort,'--peer-b-port',[string]$bPort,'--max-ms',[string]($Config.peer_exit_timeout_seconds*1000+10000),'--ready',$readyPath) -Directory $Config.run -Identity $identity
        $children.Add($relay);$journal.processes=@($children|ForEach-Object{$_.Record});Write-UdpUnrealJson $journal $journalPath
        $relayClock=[Diagnostics.Stopwatch]::StartNew()
        do {
            if($relay.Handle.HasExited){throw 'UDP relay exited before its ready acknowledgement; reserved port handoff failed.'}
            if(Test-Path -LiteralPath $readyPath) {
                $readyText=[IO.File]::ReadAllText($readyPath).Trim()
                if($readyText.Length) {
                    if($readyText -cnotmatch '^[0-9]+$' -or $readyText -cne [string]$journal.relay_port){throw 'UDP relay ready acknowledgement has the wrong bound port.'}
                    break
                }
            }
            if($relayClock.Elapsed.TotalSeconds -ge 5){throw 'UDP relay ready acknowledgement timed out.'}
            Start-Sleep -Milliseconds 20
        } while($true)
        $peerClock=[Diagnostics.Stopwatch]::StartNew()
        do {
            $alive=0
            foreach($child in $children) {
                if($child.Record.name -eq 'relay'){continue}
                Complete-UdpUnrealChild $child
                if(-not $child.Record.exited){$alive++}
            }
            if($alive -eq 0){break}
            if($relay.Handle.HasExited){throw 'UDP relay exited while a game was still running.'}
            if($peerClock.Elapsed.TotalSeconds -ge $Config.peer_exit_timeout_seconds){throw 'UDP games exceeded the bounded completion deadline.'}
            Start-Sleep -Milliseconds 20
        } while($true)
        Send-UdpUnrealRelayStop $journal.relay_port
        if(-not $relay.Handle.WaitForExit(3000)){throw 'UDP relay did not exit after RLSTOP.'}
        Complete-UdpUnrealChild $relay
        if(@($children|Where-Object{$_.Record.exit_code -ne 0}).Count){throw 'One or more UDP game/relay processes reported failure; inspect each UE trace.'}
        $journal.success=$true;$exitCode=0
    } catch {$journal.failure_reason=$_.Exception.Message}
    finally {
        if($null -ne $relayReservation){$relayReservation.Dispose();$journal.relay_reservation_released=$true}
        if($null -ne $missingReservation){$missingReservation.Dispose();$journal.missing_peer_reservation_released=$true}
        # Exact owned handles only; the outer native Job remains the final owner
        # of every descendant, including if this finally is interrupted.
        foreach($child in $children) {
            try {
                if(-not $child.Handle.HasExited) {
                    if($child.Record.name -eq 'relay') {
                        Send-UdpUnrealRelayStop $journal.relay_port
                        $null=$child.Handle.WaitForExit(1000)
                    }
                    if(-not $child.Handle.HasExited){$child.Record.cleanup='terminated';$child.Handle.Kill();$null=$child.Handle.WaitForExit(2000)}
                }
                Complete-UdpUnrealChild $child
            } catch {$child.Record.cleanup='outer-job-required';$journal.success=$false;$exitCode=1}
        }
        $journal.processes=@($children|ForEach-Object{$_.Record})
        Write-UdpUnrealJson $journal $journalPath
        foreach($child in $children){$child.Handle.Dispose()}
    }
    return $exitCode
}

if($SupervisorConfig) {
    $config=Read-UnrealJson $SupervisorConfig
    # This entry point is launched suspended into ProcessRunner's kill-on-close
    # Job before it can create children. It must never acquire another UE lock.
    exit (Invoke-UdpUnrealSupervisor $config)
}
foreach($required in @('EngineRoot','DemoRoot','SdkRoot')){if([string]::IsNullOrWhiteSpace((Get-Variable -Name $required -ValueOnly))){throw "-$required is required."}}
if($Case -eq 'Desync' -and -not $PSBoundParameters.ContainsKey('ScenarioSeed')){$ScenarioSeed='1'}
foreach($seed in @($ScenarioSeed,$TransportSeed)) {
    [uint64]$parsed=0
    if($seed -cnotmatch '^[0-9]+$' -or -not [uint64]::TryParse($seed,[ref]$parsed)){throw 'Seeds must be complete unsigned 64-bit decimal integers.'}
}
$identity=Get-SdkSourceIdentity
if(-not $identity.Clean -and -not $AllowDirty){throw 'UDP UE demo requires clean source; -AllowDirty is for diagnostic evidence.'}
$demo=Get-UnrealDemoOutputPath $DemoRoot;$sdk=Get-SdkRepositoryPath $SdkRoot
$package=Test-UnrealArtifactPackage -Root $demo -Kind demo -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
if($package.Manifest.configuration -cne 'Shipping'){throw 'UDP UE demo requires the packaged Shipping inner executable.'}
$null=Test-SdkManifest -SdkRoot $sdk -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
$game=Join-Path $demo 'Windows/RollbackArena/Binaries/Win64/RollbackArena-Win64-Shipping.exe'
$cli=Join-Path $sdk 'bin/rollback_lab.exe'
if(-not(Test-Path -LiteralPath $cli -PathType Leaf)){throw 'SDK relay/replay CLI is missing.'}
$context=New-UnrealContext -EngineRoot $EngineRoot -Role ('udp-ue-'+$Case.ToLowerInvariant())
$summary=[ordered]@{schema_version=1;mode='udp_ue_group';case=$Case;source_git_sha=$identity.Sha;source_clean=[bool]$identity.Clean;
    sdk_version='0.2.0-candidate';abi_version=1;protocol_version=1;simulation_version=1;scenario_seed=[uint64]$ScenarioSeed;transport_seed=[uint64]$TransportSeed;target_frame=$Frames;
    success=$false;exit_code=1;failure_reason='';all_children_exited=$false;timed_out=$false;processes=@();group=$null;canonical_replay_verified=$false;
    sdk_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $sdk 'manifest.json')).Hash.ToLowerInvariant();
    demo_manifest_sha256=(Get-FileHash -LiteralPath (Join-Path $demo 'manifest.json')).Hash.ToLowerInvariant();files=@()}
$supervisedClock=$null;$supervisedBudgetMs=0
try {
    $clients=@()
    foreach($peer in 0,1) {
        if($Case -eq 'MissingPeer' -and $peer -eq 1){continue}
        $label=if($peer -eq 0){'a'}else{'b'};$directory=Join-Path $context.Run ('peer-'+$label)
        New-Item -ItemType Directory -Path $directory -Force|Out-Null
        $helloProtocol=1;$helloSimulation=1;$helloAbi=1;$variant=0
        if($peer -eq 1){switch($Case){'ProtocolMismatch'{$helloProtocol=2};'SimulationMismatch'{$helloSimulation=2};'AbiMismatch'{$helloAbi=2};'Desync'{$variant=1}}}
        $arguments=@('RollbackArena','-windowed','-ResX=1280','-ResY=720','-ForceRes','-RollbackLabUdpSmoke',('-RollbackLabUdpPeer='+$label.ToUpperInvariant()),
            ('-RollbackLabScenarioSeed='+$ScenarioSeed),('-RollbackLabTransportSeed='+$TransportSeed),('-RollbackLabFrames='+$Frames),
            ('-RollbackLabGitSha='+$identity.Sha),('-RollbackLabTrace='+(Join-Path $directory 'ue-trace.json')),
            ('-RollbackLabCaptureDir='+(Join-Path $directory 'captures')),('-RollbackLabUdpReady='+(Join-Path $directory 'ready.json')),
            ('-RollbackLabUdpHelloProtocol='+$helloProtocol),('-RollbackLabUdpHelloSimulation='+$helloSimulation),('-RollbackLabUdpHelloAbi='+$helloAbi),
            ('-RollbackLabUdpVariant='+$variant),('-RollbackLabUdpHandshakeTimeoutMs='+$HandshakeTimeoutMs),('-RollbackLabUdpRunTimeoutMs='+$RunTimeoutMs))+
            (Get-UnrealRuntimeArguments -Context @{Run=$directory;Cache=$context.Cache})
        $clients+=@{name='peer-'+$label;peer=$peer;ready=(Join-Path $directory 'ready.json');arguments=$arguments;hello_protocol=$helloProtocol;hello_simulation=$helloSimulation;hello_abi=$helloAbi}
    }
    $config=[ordered]@{schema_version=1;case=$Case;run=$context.Run;source_git_sha=$identity.Sha;source_clean=[bool]$identity.Clean;
        game_exe=$game;relay_exe=$cli;scenario_seed=[uint64]$ScenarioSeed;transport_seed=[uint64]$TransportSeed;frames=$Frames;
        ready_timeout_seconds=$ReadyTimeoutSeconds;peer_exit_timeout_seconds=$PeerExitTimeoutSeconds;clients=$clients}
    $configPath=Join-Path $context.Run 'run-config.json';Write-UdpUnrealJson $config $configPath
    $budget=if($Case -eq 'Watchdog'){$WatchdogSeconds}else{$TimeoutSeconds}
    Write-Output "UDP UE $Case evidence: $($context.Run)"
    $supervisorFailure=''
    $supervisedBudgetMs=$budget*1000;$supervisedClock=[Diagnostics.Stopwatch]::StartNew()
    try {Invoke-UnrealProcess -Context $context -FilePath (Get-Process -Id $PID).Path -Arguments @('-NoProfile','-File',$PSCommandPath,'-SupervisorConfig',$configPath) -Name udp-supervisor -TimeoutSeconds $budget}
    catch {$supervisorFailure=$_.Exception.Message}
    $outer=Read-UnrealJson (Join-Path $context.Run 'udp-supervisor.process.json')
    $summary.all_children_exited=$outer.all_children_exited -eq $true;$summary.timed_out=$outer.timed_out -eq $true
    if(Test-Path -LiteralPath (Join-Path $context.Run 'children.json')) {
        $journal=Read-UnrealJson (Join-Path $context.Run 'children.json')
        $summary.processes=@($journal.processes|ForEach-Object {
            $_.outer_job_reaped=$summary.all_children_exited
            if(-not $_.exited -and $summary.all_children_exited){$_.cleanup='outer-job-terminated'}
            $_
        })
    }
    if($supervisorFailure){throw $supervisorFailure}
    if($Case -ne 'Normal'){throw "Negative case $Case unexpectedly completed; diagnostics must fail closed."}
    $summary.group=Test-UdpUnrealGroupEvidence -Run $context.Run -ExpectedGitSha $identity.Sha -ScenarioSeed ([uint64]$ScenarioSeed) -TransportSeed ([uint64]$TransportSeed) -Frames $Frames -AllowDirty:$AllowDirty
    foreach($peer in $summary.group.peers) {
        $name='replay-'+$peer.local_peer
        Invoke-UnrealProcess -Context $context -FilePath $cli -Arguments @('replay',$peer.replay) -Name $name -TimeoutSeconds 15
        $expectedHash=[Convert]::ToUInt64($peer.final_hash.Substring(2),16)
        $output=[IO.File]::ReadAllText((Join-Path $context.Run ($name+'.stdout.txt'))).Trim()
        if($output -cne "replay verified: frame $Frames, hash $expectedHash"){throw 'Canonical CLI replay result differs from UE confirmed frame/hash.'}
    }
    $summary.canonical_replay_verified=$true;$summary.success=$true;$summary.exit_code=0
} catch {$summary.failure_reason=$_.Exception.Message}
finally {
    try {
        # Share the existing native watchdog's 10-second cleanup allowance. This
        # is a remaining readiness deadline, never an unconditional extra delay.
        $readinessMs=10000
        if($summary.timed_out -and $null -ne $supervisedClock) {
            $readinessMs=[Math]::Max(0,[Math]::Min(10000,$supervisedBudgetMs+10000-$supervisedClock.ElapsedMilliseconds))
        }
        $null=Complete-UdpUnrealSummary -Summary $summary -Run $context.Run -ReadinessTimeoutMs $readinessMs
    } finally {$context.Lock.Dispose()}
}
if(-not $summary.success){Write-Error -Message ("UDP UE failed: "+$summary.failure_reason+"; evidence: "+$context.Run) -ErrorAction Continue;exit 1}
Write-Output "UDP UE confirmed frame $Frames; replay verified; all children reaped. Evidence: $($context.Run)"
