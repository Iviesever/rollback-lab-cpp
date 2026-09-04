#requires -Version 7.0
param([string]$SmokeFixture='artifacts/ue5-0.2/runs/20260904-131148-230-editor-arena',[switch]$SdkArchiveOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot '../../scripts/UnrealCommon.ps1')
$implementation=Join-Path $PSScriptRoot '../../scripts/UnrealEvidence.ps1'
if(Test-Path -LiteralPath $implementation){. $implementation}
$results=[Collections.Generic.List[object]]::new()
function Check([string]$Name,[scriptblock]$Action) {
    if($SdkArchiveOnly -and $Name -notlike 'SDK archive *'){return}
    try {& $Action; $results.Add([ordered]@{name=$Name;passed=$true}); Write-Output "PASS $Name"}
    catch {$results.Add([ordered]@{name=$Name;passed=$false;error=$_.Exception.Message}); Write-Output "FAIL $Name`: $($_.Exception.Message)"}
}
function Reject([scriptblock]$Action,[string]$Reason) {
    $rejected=$false
    try {& $Action|Out-Null} catch {$rejected=$true}
    if(-not $rejected){throw "Accepted invalid evidence: $Reason"}
}
function Write-Json($Value,[string]$Path) {$Value|ConvertTo-Json -Depth 100|Set-Content -LiteralPath $Path -Encoding utf8NoBOM}
$identity=Get-SdkSourceIdentity
$label='evidence-tests-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff')+'-'+[guid]::NewGuid().ToString('N').Substring(0,8)
$testRoot=Get-SdkRepositoryPath ('artifacts/ue5-0.2/'+$label)
$packageRoot=Get-SdkRepositoryPath ('artifacts/ue5-0.2/plugin/'+$label)
New-Item -ItemType Directory -Path $testRoot,$packageRoot|Out-Null
Check 'artifact and smoke verifier APIs exist' {
    foreach($name in @('New-UnrealArtifactPackage','Test-UnrealArtifactPackage','Test-UnrealSmokeEvidence','Assert-UnrealJsonEqual')) {
        if(-not(Get-Command $name -ErrorAction SilentlyContinue)){throw "Missing $name"}
    }
}
if(@($results|Where-Object{-not $_.passed}).Count){throw 'Unreal evidence API contract RED: implementation is missing.'}

function New-PluginFixture([string]$Name) {
    $root=Join-Path $packageRoot $Name
    foreach($relative in @('RollbackLabBridge.uplugin','Binaries/Win64/UnrealEditor-RollbackLabBridge.dll',
        'Binaries/ThirdParty/RollbackLab/bin/rollback_lab_c.dll','Binaries/ThirdParty/RollbackLab/lib/rollback_lab_c.lib',
        'Binaries/ThirdParty/RollbackLab/manifest.json')) {
        $path=Join-Path $root $relative
        New-Item -ItemType Directory -Path (Split-Path -Parent $path) -Force|Out-Null
        Set-Content -LiteralPath $path -Value ('test fixture: '+$relative) -Encoding utf8NoBOM
    }
    $null=New-UnrealArtifactPackage -Root $root -Kind plugin -Configuration Development -AllowDirty
    return $root
}
function Seal-Fixture([string]$Root) {
    $lines=Get-ChildItem -LiteralPath $Root -Recurse -File -Force|Where-Object{$_.FullName -ine (Join-Path $Root 'checksums.sha256')}|Sort-Object FullName|ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetRelativePath($Root,$_.FullName).Replace('\','/')
    }
    $lines|Set-Content -LiteralPath (Join-Path $Root 'checksums.sha256') -Encoding utf8NoBOM
    if(Test-Path -LiteralPath ($Root+'.zip')){Remove-Item -LiteralPath ($Root+'.zip') -Force}
    [IO.Compression.ZipFile]::CreateFromDirectory($Root,$Root+'.zip')
    ((Get-FileHash -LiteralPath ($Root+'.zip')).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($Root+'.zip'))|
        Set-Content -LiteralPath ($Root+'.zip.sha256') -Encoding utf8NoBOM
}
Check 'complete plugin package verifies and contains no absolute manifest paths' {
    $root=New-PluginFixture 'valid'
    $package=Test-UnrealArtifactPackage -Root $root -Kind plugin -ExpectedGitSha $identity.Sha -AllowDirty
    if($package.Manifest.source_git_sha -cne $identity.Sha){throw 'Source binding lost.'}
    if((Get-Content -LiteralPath (Join-Path $root 'manifest.json') -Raw) -match '[A-Za-z]:[\\/]'){throw 'Machine path leaked.'}
}
foreach($case in @('corrupt-file','extra-file','missing-file','stale-sha','wrong-kind','traversal','duplicate-manifest','missing-checksum','duplicate-zip','zip-traversal','zip-corruption','dirty-manifest')) {
    Check "package rejects $case" {
        $root=New-PluginFixture $case
        $manifestPath=Join-Path $root 'manifest.json'
        $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
        switch($case) {
            'corrupt-file' {Add-Content -LiteralPath (Join-Path $root 'RollbackLabBridge.uplugin') 'tampered'}
            'extra-file' {Set-Content -LiteralPath (Join-Path $root '.unmanifested') 'hidden'}
            'missing-file' {Remove-Item -LiteralPath (Join-Path $root 'RollbackLabBridge.uplugin')}
            'stale-sha' {$manifest.source_git_sha='0'*40;Write-Json $manifest $manifestPath;Seal-Fixture $root}
            'wrong-kind' {$manifest.kind='demo';Write-Json $manifest $manifestPath;Seal-Fixture $root}
            'traversal' {$manifest.files[0].path='../outside';Write-Json $manifest $manifestPath;Seal-Fixture $root}
            'duplicate-manifest' {$manifest.files+=@($manifest.files[0]);Write-Json $manifest $manifestPath;Seal-Fixture $root}
            'missing-checksum' {Set-Content -LiteralPath (Join-Path $root 'checksums.sha256') ''}
            'duplicate-zip' {
                $zip=[IO.Compression.ZipFile]::Open($root+'.zip',[IO.Compression.ZipArchiveMode]::Update)
                try {$null=$zip.CreateEntry('RollbackLabBridge.uplugin')} finally {$zip.Dispose()}
                ((Get-FileHash -LiteralPath ($root+'.zip')).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($root+'.zip'))|
                    Set-Content -LiteralPath ($root+'.zip.sha256') -Encoding utf8NoBOM
            }
            'zip-traversal' {
                $zip=[IO.Compression.ZipFile]::Open($root+'.zip',[IO.Compression.ZipArchiveMode]::Update)
                try {$null=$zip.CreateEntry('../outside')} finally {$zip.Dispose()}
                ((Get-FileHash -LiteralPath ($root+'.zip')).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($root+'.zip'))|
                    Set-Content -LiteralPath ($root+'.zip.sha256') -Encoding utf8NoBOM
            }
            'zip-corruption' {Add-Content -LiteralPath ($root+'.zip') 'corrupted'}
            'dirty-manifest' {$manifest.source_clean=$false;Write-Json $manifest $manifestPath;Seal-Fixture $root}
        }
        if($case -eq 'dirty-manifest'){Reject {Test-UnrealArtifactPackage -Root $root -Kind plugin -ExpectedGitSha $identity.Sha} $case}
        else {Reject {Test-UnrealArtifactPackage -Root $root -Kind plugin -ExpectedGitSha $identity.Sha -AllowDirty} $case}
    }
}
Check 'package rejects junction before hashing or writing manifest' {
    $root=Join-Path $packageRoot 'junction'
    New-Item -ItemType Directory -Path $root|Out-Null
    $link=Join-Path $root 'redirect'
    try {
        New-Item -ItemType Junction -Path $link -Target $testRoot|Out-Null
        Reject {New-UnrealArtifactPackage -Root $root -Kind plugin -Configuration Development -AllowDirty} 'junction'
        if(Test-Path -LiteralPath (Join-Path $root 'manifest.json')){throw 'Manifest written before rejecting unsafe tree.'}
    } finally {if(Test-Path -LiteralPath $link){Remove-Item -LiteralPath $link -Force}}
}
Check 'package output stays in dedicated kind directory' {
    Reject {New-UnrealArtifactPackage -Root $testRoot -Kind plugin -Configuration Development -AllowDirty} 'arbitrary artifact path'
    Reject {New-UnrealArtifactPackage -Root 'docs' -Kind demo -Configuration Development -AllowDirty} 'tracked source directory'
}

$fixture=Get-SdkRepositoryPath $SmokeFixture
if(-not $SdkArchiveOnly -and -not(Test-Path -LiteralPath (Join-Path $fixture 'ue-trace.json'))){throw 'Supply an existing real smoke run using -SmokeFixture.'}
function New-SmokeFixture([string]$Name) {
    $root=Join-Path $testRoot $Name
    New-Item -ItemType Directory -Path (Join-Path $root 'captures') -Force|Out-Null
    foreach($name in @('ue-trace.json','report.json','input.rlr')){Copy-Item -LiteralPath (Join-Path $fixture $name) -Destination $root}
    Get-ChildItem -LiteralPath (Join-Path $fixture 'captures') -Filter '*.png'|Copy-Item -Destination (Join-Path $root 'captures')
    # This is explicitly an isolated negative-test fixture, never packaged-run evidence.
    $trace=Get-Content -LiteralPath (Join-Path $root 'ue-trace.json') -Raw|ConvertFrom-Json
    $trace.source_git_sha=$identity.Sha; $trace.core_report.git_sha=$identity.Sha
    Write-Json $trace (Join-Path $root 'ue-trace.json')
    Write-Json $trace.core_report (Join-Path $root 'report.json')
    Write-Json ([ordered]@{source_git_sha=$identity.Sha;source_clean=$true;exit_code=0;timed_out=$false;all_children_exited=$true;
        pid=1;total_processes=1;program='Windows/RollbackArena.exe';arguments=@('-RollbackLabSmoke')}) (Join-Path $root 'packaged-arena.process.json')
    return $root
}
Check 'real smoke fixture report, rollback trace, and materialized PNGs validate' {
    $root=New-SmokeFixture 'smoke-valid'
    $evidence=Test-UnrealSmokeEvidence -SmokeRun $root -ExpectedGitSha $identity.Sha
    if($evidence.Report.rollback_count -lt 1 -or $evidence.Captures.Count -ne 3){throw 'Evidence result lost verified facts.'}
}
foreach($case in @('missing-png','invalid-png','wrong-hash','stale-sha','no-rollback','failed-replay','malformed-json','duplicate-json-key','string-success','missing-exit-code','missing-replay',
    'watchdog','nonzero-exit','live-child','dirty-source','report-mismatch','identity-mismatch','trace-without-prediction','trace-without-packets')) {
    Check "smoke rejects $case" {
        $root=New-SmokeFixture $case
        $path=Join-Path $root 'ue-trace.json'
        $trace=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
        $processPath=Join-Path $root 'packaged-arena.process.json'
        $process=Get-Content -LiteralPath $processPath -Raw|ConvertFrom-Json
        switch($case) {
            'missing-png' {Remove-Item -LiteralPath (Join-Path $root 'captures/correction.png')}
            'invalid-png' {Set-Content -LiteralPath (Join-Path $root 'captures/start.png') 'not PNG'}
            'wrong-hash' {$trace.final_hash_b='0x0000000000000000'}
            'stale-sha' {$trace.source_git_sha='0'*40}
            'no-rollback' {$trace.rollback_count=0}
            'failed-replay' {$trace.replay_verified=$false}
            'string-success' {$trace.success='true'}
            'missing-exit-code' {$process.PSObject.Properties.Remove('exit_code')}
            'missing-replay' {Remove-Item -LiteralPath (Join-Path $root 'input.rlr')}
            'watchdog' {$process.timed_out=$true}
            'nonzero-exit' {$process.exit_code=3}
            'live-child' {$process.all_children_exited=$false}
            'dirty-source' {$process.source_clean=$false}
            'report-mismatch' {$trace.core_report.transport_config.loss_percent=6}
            'identity-mismatch' {$trace.core_report.identity_digest='0x0000000000000000'}
            'trace-without-prediction' {foreach($frame in $trace.core_trace.frames){$frame.predicted=$false}}
            'trace-without-packets' {$trace.core_trace.packets=@()}
        }
        Write-Json $trace $path;Write-Json $process $processPath
        if($case -eq 'malformed-json'){Set-Content -LiteralPath $path '{broken'}
        if($case -eq 'duplicate-json-key') {
            $json=Get-Content -LiteralPath $path -Raw
            Set-Content -LiteralPath $path -Value ('{"success":true,'+$json.Substring($json.IndexOf('{')+1))
        }
        Reject {Test-UnrealSmokeEvidence -SmokeRun $root -ExpectedGitSha $identity.Sha} $case
    }
}
Check 'whole-report parity catches nested difference and ignores object key order' {
    Assert-UnrealJsonEqual -Expected ([ordered]@{x=1;y=@{z=@(2,3)}}) -Actual ([ordered]@{y=@{z=@(2,3)};x=1}) -Label 'ordered fixture'
    Reject {Assert-UnrealJsonEqual -Expected @{x=1;y=@{z=@(2,3)}} -Actual @{x=1;y=@{z=@(2,4)}} -Label 'nested fixture'} 'nested field mismatch'
}
Check 'three-way verification command is available' {
    if(-not(Test-Path -LiteralPath (Join-Path $PSScriptRoot '../../scripts/VerifyUnrealIntegration.ps1'))){throw 'Missing three-way verification command.'}
}
Check 'three-way verification records source-bound failure for missing SDK' {
    $before=@(Get-ChildItem -LiteralPath (Get-SdkRepositoryPath 'artifacts/ue5-0.2/parity') -Directory -ErrorAction SilentlyContinue|ForEach-Object{$_.FullName})
    Reject {& (Join-Path $PSScriptRoot '../../scripts/VerifyUnrealIntegration.ps1') -SdkRoot $testRoot -DemoRoot $testRoot -PluginRoot $packageRoot -SmokeRun $fixture -AllowDirty} 'missing SDK'
    $created=@(Get-ChildItem -LiteralPath (Get-SdkRepositoryPath 'artifacts/ue5-0.2/parity') -Directory|Where-Object{$_.FullName -notin $before})
    if($created.Count -ne 1){throw 'Failed verification did not create one unique run directory.'}
    $summary=Get-Content -LiteralPath (Join-Path $created[0].FullName 'verification.json') -Raw|ConvertFrom-Json
    if($summary.success -ne $false -or $summary.source_git_sha -cne $identity.Sha -or -not $summary.failure_reason){throw 'Failed verification lacks source-bound failure evidence.'}
}
$sdkOriginal=Get-SdkRepositoryPath ('artifacts/sdk/0.2.0-'+$identity.Sha.Substring(0,12)+'/install')
foreach($case in @('missing','multiple','corrupt-digest','corrupt-content','valid')) {
    Check "SDK archive $case is checked before plugin verification" {
        $sdkFixtureParent=Join-Path $testRoot ('sdk-'+$case)
        New-Item -ItemType Directory -Path $sdkFixtureParent|Out-Null
        $sdkFixture=Join-Path $sdkFixtureParent 'install'
        Copy-Item -LiteralPath $sdkOriginal -Destination $sdkFixture -Recurse
        if($case -ne 'missing') {
            $originalZip=@(Get-ChildItem -LiteralPath (Split-Path -Parent $sdkOriginal) -Filter '*.zip' -File)
            if($originalZip.Count -ne 1){throw 'SDK archive tests need one real source archive.'}
            $zip=Join-Path $sdkFixtureParent $originalZip[0].Name
            Copy-Item -LiteralPath $originalZip[0].FullName -Destination $zip
            Copy-Item -LiteralPath ($originalZip[0].FullName+'.sha256') -Destination ($zip+'.sha256')
            switch($case) {
                'multiple' {Copy-Item -LiteralPath $zip -Destination (Join-Path $sdkFixtureParent 'second.zip')}
                'corrupt-digest' {Set-Content -LiteralPath ($zip+'.sha256') -Value (('0'*64)+'  '+[IO.Path]::GetFileName($zip))}
                'corrupt-content' {
                    $archive=[IO.Compression.ZipFile]::Open($zip,[IO.Compression.ZipArchiveMode]::Update)
                    try {
                        $archive.GetEntry('bin/rollback_lab_c.dll').Delete()
                        $entry=$archive.CreateEntry('bin/rollback_lab_c.dll')
                        $writer=[IO.StreamWriter]::new($entry.Open())
                        try {$writer.Write('tampered SDK DLL fixture')} finally {$writer.Dispose()}
                    } finally {$archive.Dispose()}
                    ((Get-FileHash -LiteralPath $zip).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($zip))|
                        Set-Content -LiteralPath ($zip+'.sha256') -Encoding utf8NoBOM
                }
            }
        }
        $failure=''
        try {& (Join-Path $PSScriptRoot '../../scripts/VerifyUnrealIntegration.ps1') -SdkRoot $sdkFixture -DemoRoot $testRoot -PluginRoot $testRoot -SmokeRun $fixture -AllowDirty|Out-Null}
        catch {$failure=$_.Exception.Message}
        $expected=if($case -eq 'valid'){'Artifact root must stay in the dedicated plugin directory'}else{'SDK archive'}
        if($failure -notmatch [regex]::Escape($expected)){throw "Expected $expected failure boundary for $case, got: $failure"}
    }
}
$failed=@($results|Where-Object{-not $_.passed})
Write-Json ([ordered]@{source_git_sha=$identity.Sha;source_clean=$identity.Clean;test_fixture_only=$true;total=$results.Count;failed=$failed.Count;tests=@($results)}) (Join-Path $testRoot 'results.json')
if($failed.Count){throw "$($failed.Count) of $($results.Count) Unreal evidence tests failed. See $testRoot"}
Write-Output "$($results.Count) Unreal evidence tests passed. Evidence: $testRoot"
