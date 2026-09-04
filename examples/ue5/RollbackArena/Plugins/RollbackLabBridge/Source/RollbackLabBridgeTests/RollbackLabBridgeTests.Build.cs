using UnrealBuildTool;

public class RollbackLabBridgeTests : ModuleRules
{
    public RollbackLabBridgeTests(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
        PrivateDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "Json", "UnrealEd", "RollbackLabBridge" });
    }
}
