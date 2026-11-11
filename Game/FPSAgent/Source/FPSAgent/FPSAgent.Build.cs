using UnrealBuildTool;

public class FPSAgent : ModuleRules
{
	public FPSAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"EnhancedInput", "AIModule", "DeveloperSettings" });
	}
}
