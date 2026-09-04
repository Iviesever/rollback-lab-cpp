using UnrealBuildTool;
public class RollbackArenaTarget : TargetRules
{
    public RollbackArenaTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Game;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("RollbackArenaDemo");
    }
}
