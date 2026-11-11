using UnrealBuildTool;

public class FPSAgentTarget : TargetRules
{
	public FPSAgentTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("FPSAgent");
	}
}
