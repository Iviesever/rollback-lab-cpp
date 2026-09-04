using UnrealBuildTool;

public class RollbackArenaTools : ModuleRules
{
    public RollbackArenaTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "UnrealEd", "AssetRegistry" });
    }
}
