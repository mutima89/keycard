using UnrealBuildTool;

public class KeycardEditorTarget : TargetRules
{
	public KeycardEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "Keycard", "KeycardSim" });
	}
}
