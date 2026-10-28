using System.IO;
using UnrealBuildTool;

// Gameplay and presentation: first-person character, interaction, Slate screens, people,
// UObject adapters over KeycardSim. LP rules and formulas must not live here.
public class Keycard : ModuleRules
{
	public Keycard(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] {
			"Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "UMG", "Slate", "SlateCore", "RHI", "KeycardSim" });
		// RenderCore: the self-test tour reads GGameThreadTime/GRenderThreadTime. ImageCore: the walk test's reach maps.
		PrivateDependencyModuleNames.AddRange(new string[] { "RenderCore", "ImageCore" });

		// Content read from disk at startup (validated on load): missions, people, story, events,
		// the Field Manual, the glossary, endings, the audio manifest, and the UI fonts when present.
		string ProjectDir = Path.GetFullPath(Path.Combine(ModuleDirectory, "..", ".."));
		RuntimeDependencies.Add(Path.Combine(ProjectDir, "Data", "*.json"));
		RuntimeDependencies.Add(Path.Combine(ProjectDir, "Data", "Missions", "*.json"));
		if (Directory.Exists(Path.Combine(ProjectDir, "Resources", "Fonts")))
		{
			RuntimeDependencies.Add(Path.Combine(ProjectDir, "Resources", "Fonts", "*.ttf"));
		}
	}
}
