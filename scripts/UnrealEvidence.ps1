#requires -Version 7.0
. (Join-Path $PSScriptRoot 'UnrealCommon.ps1')

function Assert-UnrealRelativeFilePath {
    param([Parameter(Mandatory)][string]$Path)
    if($Path -cnotmatch '^[A-Za-z0-9_. /-]+$' -or [IO.Path]::IsPathRooted($Path)) {
        throw 'Artifact paths must be portable relative file paths.'
    }
    foreach($part in $Path.Split('/')) {
        if($part -in @('','.','..') -or $part.EndsWith('.') -or $part.EndsWith(' ')) {
            throw 'Artifact paths cannot contain empty, traversal, or ambiguous components.'
        }
    }
}

function Get-UnrealArtifactRoot {
    param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][ValidateSet('plugin','demo')][string]$Kind)
    $full=(Get-SdkRepositoryPath $Root).TrimEnd('\','/')
    $dedicated=(Get-SdkRepositoryPath ('artifacts/ue5-0.2/'+$Kind)).TrimEnd('\','/')
    if($full -ine $dedicated -and -not $full.StartsWith($dedicated+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
        throw "Artifact root must stay in the dedicated $Kind directory."
    }
    Assert-UnrealOrdinaryPath $full
    if(-not(Test-Path -LiteralPath $full -PathType Container)){throw 'Artifact root does not exist.'}
    return $full
}

function Get-UnrealFileInventory {
    param([Parameter(Mandatory)][string]$Root)
    $root=Get-SdkRepositoryPath $Root
    Assert-UnrealOrdinaryPath $root
    $files=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($root)
    while($pending.Count) {
        foreach($item in Get-ChildItem -LiteralPath $pending.Pop() -Force) {
            if(($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Artifact trees cannot contain junctions or symbolic links.'}
            $relative=[IO.Path]::GetRelativePath($root,$item.FullName).Replace('\','/')
            Assert-UnrealRelativeFilePath $relative
            if($item.PSIsContainer){$pending.Push($item.FullName)}
            elseif(-not $files.TryAdd($relative,$item.FullName)){throw 'Artifact tree contains colliding file paths.'}
        }
    }
    return ,$files
}

function Assert-UnrealJsonElement {
    param([Text.Json.JsonElement]$Element)
    if($Element.ValueKind -eq [Text.Json.JsonValueKind]::Object) {
        $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($property in $Element.EnumerateObject()) {
            if(-not $names.Add($property.Name)){throw 'JSON contains duplicate or case-colliding properties.'}
            Assert-UnrealJsonElement $property.Value
        }
    } elseif($Element.ValueKind -eq [Text.Json.JsonValueKind]::Array) {
        foreach($value in $Element.EnumerateArray()){Assert-UnrealJsonElement $value}
    }
}

function Read-UnrealJson {
    param([Parameter(Mandatory)][string]$Path)
    $path=Get-SdkRepositoryPath $Path
    Assert-UnrealOrdinaryPath $path
    $json=Get-Content -LiteralPath $path -Raw
    $document=[Text.Json.JsonDocument]::Parse($json)
    try {
        if($document.RootElement.ValueKind -ne [Text.Json.JsonValueKind]::Object){throw 'Evidence JSON root must be an object.'}
        Assert-UnrealJsonElement $document.RootElement
    } finally {$document.Dispose()}
    return ($json|ConvertFrom-Json -AsHashtable -Depth 100)
}

function Assert-UnrealJsonEqual {
    param([Parameter(Mandatory)]$Expected,[Parameter(Mandatory)]$Actual,[string]$Label='JSON')
    $left=[Text.Json.Nodes.JsonNode]::Parse(($Expected|ConvertTo-Json -Depth 100 -Compress))
    $right=[Text.Json.Nodes.JsonNode]::Parse(($Actual|ConvertTo-Json -Depth 100 -Compress))
    if(-not [Text.Json.Nodes.JsonNode]::DeepEquals($left,$right)){throw "$Label differs, including nested fields or report identity."}
}

function Assert-UnrealRequiredFiles {
    param($Files,[ValidateSet('plugin','demo')][string]$Kind,[ValidateSet('Development','Shipping')][string]$Configuration)
    $sdk=if($Kind -eq 'plugin'){'Binaries/ThirdParty/RollbackLab'}else{'Windows/RollbackArena/Plugins/RollbackLabBridge/Binaries/ThirdParty/RollbackLab'}
    $required=@(($sdk+'/bin/rollback_lab_c.dll'),($sdk+'/manifest.json'))
    if($Kind -eq 'plugin') {
        $required+=@('RollbackLabBridge.uplugin','Binaries/Win64/UnrealEditor-RollbackLabBridge.dll',($sdk+'/lib/rollback_lab_c.lib'))
    } else {
        $required+='Windows/RollbackArena.exe'
        $required+=if($Configuration -eq 'Shipping'){'Windows/RollbackArena/Binaries/Win64/RollbackArena-Win64-Shipping.exe'}else{'Windows/RollbackArena/Binaries/Win64/RollbackArena.exe'}
    }
    foreach($path in $required){if(-not $Files.ContainsKey($path)){throw "Artifact omitted required file: $path"}}
}

function New-UnrealArtifactPackage {
    param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][ValidateSet('plugin','demo')][string]$Kind,
        [Parameter(Mandatory)][ValidateSet('Development','Shipping')][string]$Configuration,[switch]$AllowDirty)
    $root=Get-UnrealArtifactRoot -Root $Root -Kind $Kind
    $identity=Get-SdkSourceIdentity
    if(-not $identity.Clean -and -not $AllowDirty){throw 'Unreal packaging requires clean source; -AllowDirty is intermediate evidence only.'}
    $inventory=Get-UnrealFileInventory $root
    Assert-UnrealRequiredFiles -Files $inventory -Kind $Kind -Configuration $Configuration
    $entries=@(foreach($relative in ($inventory.Keys|Sort-Object -CaseSensitive)) {
        if($relative -in @('manifest.json','checksums.sha256')){continue}
        [ordered]@{path=$relative;sha256=(Get-FileHash -LiteralPath $inventory[$relative] -Algorithm SHA256).Hash.ToLowerInvariant()}
    })
    $manifest=[ordered]@{schema_version=1;product_version='0.2.0-candidate';kind=$Kind;
        source_git_sha=$identity.Sha;source_clean=$identity.Clean;configuration=$Configuration;
        engine_version='5.8';abi_version=1;architecture='Win64';files=$entries}
    if($Kind -eq 'plugin'){$manifest.configurations=@('UnrealEditor Development','UnrealGame Development','UnrealGame Shipping')}
    $manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'manifest.json') -Encoding utf8NoBOM
    $inventory=Get-UnrealFileInventory $root
    @(foreach($relative in ($inventory.Keys|Sort-Object -CaseSensitive)) {
        if($relative -eq 'checksums.sha256'){continue}
        (Get-FileHash -LiteralPath $inventory[$relative] -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$relative
    })|Set-Content -LiteralPath (Join-Path $root 'checksums.sha256') -Encoding utf8NoBOM
    $zipPath=$root+'.zip';Assert-UnrealOrdinaryPath $zipPath;Assert-UnrealOrdinaryPath ($zipPath+'.sha256')
    if(Test-Path -LiteralPath $zipPath){Remove-Item -LiteralPath $zipPath -Force}
    $zip=[IO.Compression.ZipFile]::Open($zipPath,[IO.Compression.ZipArchiveMode]::Create)
    try {
        $inventory=Get-UnrealFileInventory $root
        foreach($relative in ($inventory.Keys|Sort-Object -CaseSensitive)) {
            $null=[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,$inventory[$relative],$relative,[IO.Compression.CompressionLevel]::Optimal)
        }
    } finally {$zip.Dispose()}
    ((Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+[IO.Path]::GetFileName($zipPath))|
        Set-Content -LiteralPath ($zipPath+'.sha256') -Encoding utf8NoBOM
    $after=Get-SdkSourceIdentity
    if($after.Sha -cne $identity.Sha -or $after.Clean -ne $identity.Clean){throw 'Source identity changed while packaging Unreal artifact.'}
    return Test-UnrealArtifactPackage -Root $root -Kind $Kind -ExpectedGitSha $identity.Sha -AllowDirty:$AllowDirty
}

function Test-UnrealArtifactPackage {
    param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][ValidateSet('plugin','demo')][string]$Kind,
        [Parameter(Mandatory)][string]$ExpectedGitSha,[switch]$AllowDirty)
    $root=Get-UnrealArtifactRoot -Root $Root -Kind $Kind
    $inventory=Get-UnrealFileInventory $root
    $manifest=Read-UnrealJson (Join-Path $root 'manifest.json')
    if($manifest.schema_version -ne 1 -or $manifest.product_version -cne '0.2.0-candidate' -or
        $manifest.kind -cne $Kind -or $manifest.engine_version -cne '5.8' -or $manifest.abi_version -ne 1 -or
        $manifest.architecture -cne 'Win64' -or $manifest.configuration -cnotin @('Development','Shipping')){throw 'Unsupported Unreal artifact manifest contract.'}
    if($ExpectedGitSha -cnotmatch '^[0-9a-f]{40}$' -or $manifest.source_git_sha -cne $ExpectedGitSha){throw 'Unreal artifact source SHA mismatch.'}
    if($manifest.source_clean -isnot [bool] -or (-not $AllowDirty -and -not $manifest.source_clean)){throw 'Unreal artifact was built from dirty or unknown source.'}
    if($Kind -eq 'plugin') {
        Assert-UnrealJsonEqual -Expected @('UnrealEditor Development','UnrealGame Development','UnrealGame Shipping') -Actual $manifest.configurations -Label 'Plugin build configurations'
    }
    $expected=[Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    if($manifest.files -isnot [array] -or $manifest.files.Count -eq 0){throw 'Unreal artifact manifest has no file inventory.'}
    foreach($entry in $manifest.files) {
        Assert-UnrealRelativeFilePath $entry.path
        if($entry.path -in @('manifest.json','checksums.sha256') -or -not $expected.TryAdd($entry.path,$entry.sha256)){throw 'Invalid or duplicate Unreal manifest entry.'}
        if(-not $inventory.ContainsKey($entry.path)){throw "Missing Unreal artifact file: $($entry.path)"}
        if($entry.sha256 -cnotmatch '^[a-f0-9]{64}$' -or (Get-FileHash -LiteralPath $inventory[$entry.path] -Algorithm SHA256).Hash -ine $entry.sha256){throw "Unreal artifact checksum mismatch: $($entry.path)"}
    }
    Assert-UnrealRequiredFiles -Files $expected -Kind $Kind -Configuration $manifest.configuration
    $expected.Add('manifest.json',(Get-FileHash -LiteralPath (Join-Path $root 'manifest.json') -Algorithm SHA256).Hash)
    $seen=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach($line in Get-Content -LiteralPath (Join-Path $root 'checksums.sha256')) {
        if($line -cnotmatch '^([a-f0-9]{64})  (.+)$'){throw 'Malformed Unreal artifact checksum line.'}
        $hash=$Matches[1];$relative=$Matches[2]
        Assert-UnrealRelativeFilePath $relative
        if(-not $seen.Add($relative) -or -not $expected.ContainsKey($relative) -or $expected[$relative] -ine $hash){throw 'Unreal artifact checksum list differs from manifest and files.'}
    }
    if($seen.Count -ne $expected.Count){throw 'Unreal artifact checksum list is incomplete.'}
    $expected.Add('checksums.sha256','')
    if($inventory.Count -ne $expected.Count){throw 'Unreal artifact tree contains missing or unmanifested files.'}
    foreach($relative in $inventory.Keys){if(-not $expected.ContainsKey($relative)){throw "Unmanifested Unreal artifact file: $relative"}}
    $zipPath=$root+'.zip';Assert-UnrealOrdinaryPath $zipPath;Assert-UnrealOrdinaryPath ($zipPath+'.sha256')
    $zip=[IO.Compression.ZipFile]::OpenRead($zipPath)
    try {
        $zipNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
        foreach($entry in $zip.Entries) {
            Assert-UnrealRelativeFilePath $entry.FullName
            if(-not $zipNames.Add($entry.FullName) -or -not $inventory.ContainsKey($entry.FullName)){throw 'Unexpected or duplicate Unreal archive entry.'}
            if(($entry.ExternalAttributes -shr 16 -band 0xF000) -eq 0xA000){throw 'Unreal archive cannot contain symbolic links.'}
        }
    } finally {$zip.Dispose()}
    # Reuse the SDK archive verifier for the complete tree and decompressed bytes.
    Test-SdkArchive -SdkRoot $root -Archive $zipPath
    [pscustomobject]@{Root=$root;Archive=$zipPath;Checksum=$zipPath+'.sha256';Manifest=$manifest}
}

function Test-UnrealPng {
    param([Parameter(Mandatory)][string]$Path)
    $path=Get-SdkRepositoryPath $Path;Assert-UnrealOrdinaryPath $path
    $stream=[IO.File]::OpenRead($path)
    try {
        $header=[byte[]]::new(8)
        if($stream.Read($header,0,8) -ne 8 -or [Convert]::ToHexString($header) -cne '89504E470D0A1A0A'){throw 'Screenshot is not a PNG.'}
    } finally {$stream.Dispose()}
    $bitmap=[Drawing.Image]::FromFile($path)
    try {
        if($bitmap.Width -lt 16 -or $bitmap.Height -lt 16){throw 'Screenshot dimensions are too small for arena evidence.'}
        [pscustomobject]@{path=[IO.Path]::GetRelativePath($script:SdkRepositoryRoot,$path).Replace('\','/');
            width=$bitmap.Width;height=$bitmap.Height;bytes=(Get-Item -LiteralPath $path).Length;
            sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
    } finally {$bitmap.Dispose()}
}

function Assert-UnrealReport {
    param([Parameter(Mandatory)]$Report,[Parameter(Mandatory)][string]$ExpectedGitSha)
    if($Report.git_sha -cne $ExpectedGitSha -or $Report.build_type -cne 'Release' -or
        $Report.compiler -cne 'MSVC' -or $Report.os -cne 'Windows' -or $Report.simulation_version -ne 1 -or
        $Report.protocol_version -ne 1 -or $Report.pcg32_version -ne 1){throw 'Report source/build/version identity mismatch.'}
    if($Report.success -isnot [bool] -or -not $Report.success -or $Report.replay_verification -isnot [bool] -or
        -not $Report.replay_verification -or $Report.failure_reason -cne '' -or $Report.desync_result -cne 'none'){throw 'Report simulation or replay failed.'}
    if($Report.frame_count -lt 1 -or $Report.frame_count -gt 36000 -or $Report.confirmed_frame -ne $Report.frame_count -or
        $Report.rollback_count -lt 1 -or $Report.resimulated_frames -lt 1 -or $Report.predicted_input_count -lt 1 -or
        $Report.final_hash_a -cnotmatch '^0x[0-9A-F]{16}$' -or $Report.final_hash_b -cne $Report.final_hash_a -or
        $Report.identity_digest -cnotmatch '^0x[0-9A-F]{16}$'){throw 'Report does not prove rollback and confirmed convergence.'}
}

function Test-UnrealSmokeEvidence {
    param([Parameter(Mandatory)][string]$SmokeRun,[Parameter(Mandatory)][string]$ExpectedGitSha,[switch]$AllowDirty)
    $root=Get-SdkRepositoryPath $SmokeRun
    $null=Get-UnrealFileInventory $root
    if($ExpectedGitSha -cnotmatch '^[0-9a-f]{40}$'){throw 'Expected smoke source SHA is invalid.'}
    $trace=Read-UnrealJson (Join-Path $root 'ue-trace.json')
    $report=Read-UnrealJson (Join-Path $root 'report.json')
    $process=Read-UnrealJson (Join-Path $root 'packaged-arena.process.json')
    if($process.source_git_sha -cne $ExpectedGitSha -or $process.source_clean -isnot [bool] -or
        (-not $AllowDirty -and -not $process.source_clean)){throw 'Packaged process source identity is stale or dirty.'}
    if($process.exit_code -isnot [long] -and $process.exit_code -isnot [int]){throw 'Packaged process exit code is missing or malformed.'}
    if($process.exit_code -ne 0 -or $process.timed_out -isnot [bool] -or $process.timed_out -or
        $process.all_children_exited -isnot [bool] -or -not $process.all_children_exited -or $process.failure){throw 'Packaged process failed, timed out, or retained a child.'}
    if($trace.schema_version -ne 1 -or $trace.sdk_version -cne '0.2.0-candidate' -or $trace.abi_version -ne 1 -or
        $trace.source_git_sha -cne $ExpectedGitSha){throw 'Unreal smoke source/version mismatch.'}
    if($trace.success -isnot [bool] -or -not $trace.success -or $trace.replay_verified -isnot [bool] -or
        -not $trace.replay_verified -or $trace.failure_reason -cne ''){throw 'Unreal smoke or replay failed.'}
    Assert-UnrealReport -Report $report -ExpectedGitSha $ExpectedGitSha
    Assert-UnrealJsonEqual -Expected $report -Actual $trace.core_report -Label 'UE embedded and standalone reports'
    if($trace.target_frame -ne $report.frame_count -or $trace.confirmed_a -ne $trace.target_frame -or
        $trace.confirmed_b -ne $trace.target_frame -or $trace.final_hash_a -cne $report.final_hash_a -or
        $trace.final_hash_b -cne $report.final_hash_b -or $trace.rollback_count -ne $report.rollback_count -or
        $trace.scenario_seed -ne $report.scenario_seed -or $trace.transport_seed -ne $report.transport_seed){throw 'Unreal smoke summary differs from its canonical report.'}
    $core=$trace.core_trace
    if($null -eq $core -or $core.trace_version -ne 1 -or $core.scenario_seed -ne $report.scenario_seed -or
        @($core.rollbacks).Count -lt 1 -or @($core.frames|Where-Object{$_.predicted -is [bool] -and $_.predicted}).Count -lt 1 -or
        @($core.packets|Where-Object{$_.kind -ceq 'sent'}).Count -lt 1 -or
        @($core.packets|Where-Object{$_.kind -ceq 'delivered'}).Count -lt 1){throw 'Unreal trace is missing prediction, packet delivery, or correction evidence.'}
    foreach($event in $core.rollbacks) {
        if($event.depth -lt 1 -or $event.rollback_from -ge $event.observed_at){throw 'Unreal trace has an invalid rollback event.'}
    }
    $replay=Join-Path $root 'input.rlr'
    if(-not(Test-Path -LiteralPath $replay -PathType Leaf) -or (Get-Item -LiteralPath $replay).Length -eq 0){throw 'Unreal replay is missing or empty.'}
    $captures=@(foreach($name in @('start','correction','convergence')){Test-UnrealPng (Join-Path $root ('captures/'+$name+'.png'))})
    [pscustomobject]@{Root=$root;Trace=$trace;Report=$report;Process=$process;Captures=$captures;Replay=$replay;
        ReplaySha256=(Get-FileHash -LiteralPath $replay -Algorithm SHA256).Hash.ToLowerInvariant()}
}
