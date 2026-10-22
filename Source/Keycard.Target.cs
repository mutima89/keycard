using UnrealBuildTool;

public class KeycardTarget : TargetRules
{
	public KeycardTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Keycard", "KeycardSim" });
	}
}
