using UnrealBuildTool;

// Engine-independent simulation rules (plain C++17, no UObjects). Also built standalone
// by the root CMakeLists.txt so unit tests run without the editor.
public class KeycardSim : ModuleRules
{
	public KeycardSim(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.NoPCHs;
		bUseRTTI = false;
		bEnableExceptions = false;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
