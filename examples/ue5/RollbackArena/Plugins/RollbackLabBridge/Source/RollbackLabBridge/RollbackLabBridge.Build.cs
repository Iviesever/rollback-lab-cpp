using System;
using System.Collections.Generic;
using System.IO;
using System.Security.Cryptography;
using System.Text.RegularExpressions;
using EpicGames.Core;
using UnrealBuildTool;

public class RollbackLabBridge : ModuleRules
{
    public RollbackLabBridge(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        if (Target.Platform != UnrealTargetPlatform.Win64)
            throw new BuildException("RollbackLab SDK ABI v1 supports Win64 x64 only.");
        if (Target.bDebugBuildsActuallyUseDebugCRT)
            throw new BuildException("RollbackLab Bridge requires the release CRT SDK (/MD), including Development and Shipping.");

        PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine" });
        PrivateDependencyModuleNames.AddRange(new[] { "Projects", "Json" });
        AddEngineThirdPartyPrivateStaticDependencies(Target, "OpenSSL");

        string SdkRoot = Path.Combine(PluginDirectory, "Binaries", "ThirdParty", "RollbackLab");
        string ManifestPath = Path.Combine(SdkRoot, "manifest.json");
        if (!File.Exists(ManifestPath))
            throw new BuildException("Stage the current verified RollbackLab SDK before building the plugin.");

        JsonObject Manifest = JsonObject.Read(new FileReference(ManifestPath));
        if (Manifest.GetIntegerField("schema_version") != 1 ||
            Manifest.GetStringField("sdk_version") != "0.2.0-candidate" ||
            Manifest.GetIntegerField("abi_version") != 1 ||
            Manifest.GetIntegerField("simulation_version") != 1 ||
            Manifest.GetIntegerField("protocol_version") != 1 ||
            Manifest.GetIntegerField("replay_version") != 1 ||
            !Manifest.GetBoolField("source_clean") ||
            Manifest.GetStringField("configuration") != "Release" ||
            Manifest.GetStringField("architecture") != "x64" ||
            Manifest.GetStringField("runtime") != "MD" ||
            Manifest.GetStringField("linkage") != "shared")
            throw new BuildException("RollbackLab SDK manifest violates the supported version/CRT/linkage contract.");

        string SourceSha = Manifest.GetStringField("source_git_sha");
        if (!Regex.IsMatch(SourceSha, "^[0-9a-f]{40}$"))
            throw new BuildException("RollbackLab SDK needs an exact source Git SHA.");

        HashSet<string> Seen = new(StringComparer.OrdinalIgnoreCase);
        foreach (JsonObject Entry in Manifest.GetObjectArrayField("files"))
        {
            string Relative = Entry.GetStringField("path");
            string Expected = Entry.GetStringField("sha256");
            if (!Regex.IsMatch(Relative, "^[A-Za-z0-9_./-]+$") || Relative.Contains("..") ||
                Path.IsPathRooted(Relative) || !Seen.Add(Relative) || !Regex.IsMatch(Expected, "^[0-9A-Fa-f]{64}$"))
                throw new BuildException("RollbackLab SDK manifest contains an invalid path or hash.");
            string FilePath = Path.Combine(SdkRoot, Relative);
            if (!File.Exists(FilePath) || !String.Equals(Hash(FilePath), Expected, StringComparison.OrdinalIgnoreCase))
                throw new BuildException("RollbackLab SDK checksum mismatch or missing file: " + Relative);
            ExternalDependencies.Add(FilePath);
        }
        foreach (string Required in new[] { "include/rollback_lab/c_api/rollback_lab_c.h", "lib/rollback_lab_c.lib", "bin/rollback_lab_c.dll" })
            if (!Seen.Contains(Required)) throw new BuildException("RollbackLab SDK manifest omitted " + Required);

        ExternalDependencies.Add(ManifestPath);
        PublicSystemIncludePaths.Add(Path.Combine(SdkRoot, "include"));
        PublicAdditionalLibraries.Add(Path.Combine(SdkRoot, "lib", "rollback_lab_c.lib"));
        PrivateDefinitions.Add("ROLLBACKLAB_EXPECTED_SOURCE_SHA=\"" + SourceSha + "\"");
        PrivateDefinitions.Add("ROLLBACKLAB_EXPECTED_MANIFEST_SHA256=\"" + Hash(ManifestPath) + "\"");

        // The verified import library is configured explicitly. UE intentionally resolves
        // all C exports through GetDllExport, with no static or delay-load import calls.
        // This makes absent exports a typed startup failure before any SDK invocation.
        RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/RollbackLab/bin/rollback_lab_c.dll", StagedFileType.NonUFS);
        RuntimeDependencies.Add("$(PluginDir)/Binaries/ThirdParty/RollbackLab/manifest.json", StagedFileType.NonUFS);
    }

    private static string Hash(string Path)
    {
        using FileStream Stream = File.OpenRead(Path);
        using SHA256 Hasher = SHA256.Create();
        return BitConverter.ToString(Hasher.ComputeHash(Stream)).Replace("-", "").ToLowerInvariant();
    }
}
