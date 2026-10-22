using UnrealBuildTool;

// Gameplay and presentation: first-person character, interaction, Slate screens, people,
// UObject adapters over KeycardSim. LP rules and formulas must not live here.
public class Keycard : ModuleRules
{
	public Keycard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "KeycardSim" });
		// Phase 1 adds Data/ runtime dependencies (missions, people, dialogue, voice cast) as in Riskbound.
	}
}
