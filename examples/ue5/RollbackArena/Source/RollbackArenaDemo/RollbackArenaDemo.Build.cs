using UnrealBuildTool;
public class RollbackArenaDemo : ModuleRules
{
    public RollbackArenaDemo(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PublicDependencyModuleNames.AddRange(new string[] {"Core", "CoreUObject", "Engine", "InputCore", "RollbackLabBridge"});
        PrivateDependencyModuleNames.AddRange(new string[] {"Json", "RenderCore"});
        if (Target.bBuildEditor) PrivateDependencyModuleNames.Add("UnrealEd");
    }
}
