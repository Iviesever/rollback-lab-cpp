using UnrealBuildTool;
public class RollbackArenaEditorTarget : TargetRules
{
    public RollbackArenaEditorTarget(TargetInfo Target) : base(Target)
    {
        Type = TargetType.Editor;
        DefaultBuildSettings = BuildSettingsVersion.Latest;
        IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
        ExtraModuleNames.Add("RollbackArenaDemo");
        ExtraModuleNames.Add("RollbackArenaTools");
    }
}
