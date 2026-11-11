using UnrealBuildTool;

public class FPSAgent : ModuleRules
{
	public FPSAgent(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Includes are written relative to the module root, e.g. "Common/FPSAgentSettings.h".
		PublicIncludePaths.Add(ModuleDirectory);
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore",
			"EnhancedInput", "AIModule", "DeveloperSettings", "Json" });
	}
}
