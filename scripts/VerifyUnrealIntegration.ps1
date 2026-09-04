#requires -Version 7.0
param([Parameter(Mandatory)][string]$SdkRoot,[Parameter(Mandatory)][string]$DemoRoot,
    [Parameter(Mandatory)][string]$PluginRoot,[Parameter(Mandatory)][string]$SmokeRun,[switch]$AllowDirty)
. (Join-Path $PSScriptRoot 'UnrealEvidence.ps1')
$identity=Get-SdkSourceIdentity
$run=Get-SdkRepositoryPath ('artifacts/ue5-0.2/parity/'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8))
Assert-UnrealOrdinaryPath $run
New-Item -ItemType Directory -Path $run|Out-Null
$summaryPath=Join-Path $run 'verification.json'
$checks=[Collections.Generic.List[object]]::new()
$summary=[ordered]@{schema_version=1;source_git_sha=$identity.Sha;source_clean=$identity.Clean;
    success=$false;checks=$checks;failure_reason=''}
function Add-VerifiedCheck([string]$Name) {$checks.Add([ordered]@{name=$Name;passed=$true})}
function Get-EvidenceFile([string]$Path) {
    $path=Get-SdkRepositoryPath $Path;Assert-UnrealOrdinaryPath $path
    [ordered]@{path=[IO.Path]::GetRelativePath($script:SdkRepositoryRoot,$path).Replace('\','/');
        sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $path).Length}
}
function Assert-SameFile([string]$Expected,[string]$Actual,[string]$Label) {
    $expectedFile=Get-EvidenceFile $Expected;$actualFile=Get-EvidenceFile $Actual
    if($expectedFile.bytes -ne $actualFile.bytes -or $expectedFile.sha256 -cne $actualFile.sha256){throw "$Label bytes differ from the verified SDK or reference replay."}
}
try {
    if(-not $identity.Clean -and -not $AllowDirty){throw 'Final integration verification requires clean source; -AllowDirty is intermediate evidence only.'}
    $sdk=(Get-SdkRepositoryPath $SdkRoot).TrimEnd('\','/')
    $null=Get-UnrealFileInventory $sdk
    $sdkManifest=Test-SdkManifest -SdkRoot $sdk -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
    Add-VerifiedCheck 'sdk_manifest_and_complete_file_tree'
    $sdkArchives=@(Get-ChildItem -LiteralPath (Split-Path -Parent $sdk) -Filter '*.zip' -File -Force)
    if($sdkArchives.Count -ne 1){throw 'SDK archive discovery requires exactly one sibling ZIP.'}
    $sdkArchive=$sdkArchives[0].FullName
    $sdkChecksum=$sdkArchive+'.sha256'
    Assert-UnrealOrdinaryPath $sdkArchive
    Assert-UnrealOrdinaryPath $sdkChecksum
    Test-SdkArchive -SdkRoot $sdk -Archive $sdkArchive
    Add-VerifiedCheck 'sdk_archive_checksum_and_complete_zip_bytes'
    $plugin=Test-UnrealArtifactPackage -Root $PluginRoot -Kind plugin -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
    Add-VerifiedCheck 'plugin_manifest_checksums_and_zip_bytes'
    $demo=Test-UnrealArtifactPackage -Root $DemoRoot -Kind demo -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
    Add-VerifiedCheck 'demo_manifest_checksums_and_zip_bytes'
    $smoke=Test-UnrealSmokeEvidence -SmokeRun $SmokeRun -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
    Add-VerifiedCheck 'smoke_trace_report_rollback_replay_claim_and_materialized_pngs'

    # Bind the process record to the actual packaged bootstrap executable and its inputs.
    $program=Get-SdkRepositoryPath $smoke.Process.program
    $expectedProgram=Join-Path $demo.Root 'Windows/RollbackArena.exe'
    if($program -ine $expectedProgram -or $smoke.Process.arguments -cnotcontains '-RollbackLabSmoke') {
        throw 'Smoke process did not execute this packaged bootstrap with smoke enabled.'
    }
    if($smoke.Process.pid -lt 1 -or $smoke.Process.total_processes -lt 1){throw 'Packaged process record lacks a real process identity.'}
    foreach($argument in @(
        ('-RollbackLabScenarioSeed='+[string]$smoke.Report.scenario_seed),
        ('-RollbackLabTransportSeed='+[string]$smoke.Report.transport_seed),
        ('-RollbackLabFrames='+[string]$smoke.Report.frame_count),
        ('-RollbackLabGitSha='+$identity.Sha),
        ('-RollbackLabTrace='+(Join-Path $smoke.Root 'ue-trace.json')),
        ('-RollbackLabCaptureDir='+(Join-Path $smoke.Root 'captures')))) {
        if($smoke.Process.arguments -cnotcontains $argument){throw 'Packaged process arguments differ from the evidence inputs or outputs.'}
    }
    Add-VerifiedCheck 'packaged_bootstrap_exit_zero_no_timeout_no_live_children_and_exact_inputs'

    $pluginSdk=Join-Path $plugin.Root 'Binaries/ThirdParty/RollbackLab'
    $demoSdk=Join-Path $demo.Root 'Windows/RollbackArena/Plugins/RollbackLabBridge/Binaries/ThirdParty/RollbackLab'
    $null=Test-SdkManifest -SdkRoot $pluginSdk -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
    foreach($staged in @($pluginSdk,$demoSdk)) {
        Assert-SameFile (Join-Path $sdk 'manifest.json') (Join-Path $staged 'manifest.json') 'Staged SDK manifest'
        Assert-SameFile (Join-Path $sdk 'bin/rollback_lab_c.dll') (Join-Path $staged 'bin/rollback_lab_c.dll') 'Staged SDK DLL'
    }
    Assert-SameFile (Join-Path $sdk 'lib/rollback_lab_c.lib') (Join-Path $pluginSdk 'lib/rollback_lab_c.lib') 'Plugin SDK import library'
    Add-VerifiedCheck 'plugin_sdk_and_demo_runtime_dll_match_verified_sdk'

    $cli=Join-Path $sdk 'bin/rollback_lab.exe'
    $consumer=Join-Path $sdk 'bin/rollback_lab_c_demo.exe'
    foreach($binary in @('bin/rollback_lab.exe','bin/rollback_lab_c_demo.exe')) {
        if(@($sdkManifest.files|Where-Object{$_.path -ceq $binary}).Count -ne 1){throw 'Parity executable is not in the verified SDK inventory.'}
    }
    $cliRoot=Join-Path $run 'cli';$cRoot=Join-Path $run 'c11'
    New-Item -ItemType Directory -Path $cliRoot,$cRoot|Out-Null
    $context=[pscustomobject]@{Run=$run}
    $scenario=[string]$smoke.Report.scenario_seed;$transport=[string]$smoke.Report.transport_seed;$frames=[string]$smoke.Report.frame_count
    Invoke-UnrealProcess -Context $context -FilePath $cli -Name 'cli-simulate' -TimeoutSeconds 120 -Arguments @(
        'simulate','--scenario','default','--scenario-seed',$scenario,'--transport-seed',$transport,'--frames',$frames,'--out',$cliRoot)
    Add-VerifiedCheck 'installed_cli_simulate_exit_zero'
    Invoke-UnrealProcess -Context $context -FilePath $consumer -Name 'c11-live' -TimeoutSeconds 120 -Arguments @($scenario,$transport,$frames,$cRoot)
    Add-VerifiedCheck 'installed_c11_two_handle_consumer_exit_zero'
    $cliReport=Read-UnrealJson (Join-Path $cliRoot 'report.json')
    $cReport=Read-UnrealJson (Join-Path $cRoot 'report.json')
    foreach($report in @($cliReport,$cReport)){Assert-UnrealReport -Report $report -ExpectedGitSha $identity.Sha}
    Assert-UnrealJsonEqual -Expected $cliReport -Actual $cReport -Label 'CLI and C11 canonical reports'
    Assert-UnrealJsonEqual -Expected $cliReport -Actual $smoke.Report -Label 'CLI and packaged UE canonical reports'
    Add-VerifiedCheck 'all_canonical_report_fields_and_identity_equal_across_cli_c11_ue'
    $cliTrace=Read-UnrealJson (Join-Path $cliRoot 'trace.json')
    $cTrace=Read-UnrealJson (Join-Path $cRoot 'trace.json')
    Assert-UnrealJsonEqual -Expected $cliTrace -Actual $cTrace -Label 'CLI and C11 canonical traces'
    Assert-UnrealJsonEqual -Expected $cliTrace -Actual $smoke.Trace.core_trace -Label 'CLI and packaged UE canonical traces'
    Add-VerifiedCheck 'all_canonical_trace_fields_equal_across_cli_c11_ue'
    $cliReplay=Join-Path $cliRoot 'input.rlr';$cReplay=Join-Path $cRoot 'input.rlr'
    Assert-SameFile $cliReplay $cReplay 'CLI and C11 replay'
    Assert-SameFile $cliReplay $smoke.Replay 'CLI and UE replay'
    Add-VerifiedCheck 'cli_c11_ue_replays_byte_identical'
    foreach($entry in @(@{name='cli';path=$cliReplay},@{name='c11';path=$cReplay},@{name='ue';path=$smoke.Replay})) {
        Invoke-UnrealProcess -Context $context -FilePath $cli -Name ('replay-'+$entry.name) -TimeoutSeconds 120 -Arguments @('replay',$entry.path)
        Add-VerifiedCheck ($entry.name+'_replay_verified_by_installed_cli')
    }
    $after=Get-SdkSourceIdentity
    if($after.Sha -cne $identity.Sha -or $after.Clean -ne $identity.Clean){throw 'Source identity changed during integration verification.'}
    Add-VerifiedCheck 'source_identity_unchanged_during_verification'
    $summary.scenario_seed=$smoke.Report.scenario_seed;$summary.transport_seed=$smoke.Report.transport_seed
    $summary.confirmed_frame=$smoke.Report.confirmed_frame;$summary.final_hash_a=$smoke.Report.final_hash_a;$summary.final_hash_b=$smoke.Report.final_hash_b
    $summary.rollback_count=$smoke.Report.rollback_count;$summary.resimulated_frames=$smoke.Report.resimulated_frames
    $summary.identity_digest=$smoke.Report.identity_digest;$summary.core_report=$smoke.Report
    $summary.captures=$smoke.Captures
    $summary.artifacts=[ordered]@{
        sdk_manifest=(Get-EvidenceFile (Join-Path $sdk 'manifest.json'));
        sdk_zip=(Get-EvidenceFile $sdkArchive);sdk_checksum=(Get-EvidenceFile $sdkChecksum);
        plugin_manifest=(Get-EvidenceFile (Join-Path $plugin.Root 'manifest.json'));plugin_zip=(Get-EvidenceFile $plugin.Archive);
        demo_manifest=(Get-EvidenceFile (Join-Path $demo.Root 'manifest.json'));demo_zip=(Get-EvidenceFile $demo.Archive);
        packaged_process=(Get-EvidenceFile (Join-Path $smoke.Root 'packaged-arena.process.json'));
        ue_trace=(Get-EvidenceFile (Join-Path $smoke.Root 'ue-trace.json'));ue_report=(Get-EvidenceFile (Join-Path $smoke.Root 'report.json'));
        cli_report=(Get-EvidenceFile (Join-Path $cliRoot 'report.json'));c11_report=(Get-EvidenceFile (Join-Path $cRoot 'report.json'));
        cli_replay=(Get-EvidenceFile $cliReplay);c11_replay=(Get-EvidenceFile $cReplay);ue_replay=(Get-EvidenceFile $smoke.Replay)}
    $summary.success=$true
} catch {
    $summary.failure_reason=$_.Exception.Message.Replace($script:SdkRepositoryRoot,'<repository>')
    $checks.Add([ordered]@{name='verification_completed';passed=$false})
    throw
} finally {
    $summary|ConvertTo-Json -Depth 100|Set-Content -LiteralPath $summaryPath -Encoding utf8NoBOM
    Write-Output "Integration verification summary: $summaryPath"
}
Write-Output "CLI / C11 / packaged UE parity verified at $($identity.Sha); clean=$($identity.Clean)."
