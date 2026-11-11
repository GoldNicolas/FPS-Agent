using UnrealBuildTool;

public class FPSAgentEditorTarget : TargetRules
{
	public FPSAgentEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FPSAgent");
	}
}
