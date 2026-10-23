#include "KcGameState.h"

#include <algorithm>
#include <cmath>

namespace kc
{
PlayerSettings ClampSettings(const PlayerSettings& In)
{
	const PlayerSettings Def;
	auto Clamp = [](double V, double Lo, double Hi, double Fallback) { return std::isfinite(V) ? std::max(Lo, std::min(Hi, V)) : Fallback; };
	PlayerSettings Out = In;
	Out.MouseSensitivity = Clamp(In.MouseSensitivity, 0.1, 5.0, Def.MouseSensitivity);
	Out.TextScale = Clamp(In.TextScale, 0.8, 1.6, Def.TextScale);
	Out.MasterVolume = Clamp(In.MasterVolume, 0.0, 1.0, Def.MasterVolume);
	Out.GraphicsQuality = std::max(0, std::min(3, In.GraphicsQuality));
	Out.Upscaling = std::max(0, std::min(3, In.Upscaling));
	if (Out.FrameCap != 0 && Out.FrameCap != 30 && Out.FrameCap != 60 && Out.FrameCap != 120) Out.FrameCap = 0;
	Out.GamepadSensitivity = Clamp(In.GamepadSensitivity, 0.1, 5.0, Def.GamepadSensitivity);
	return Out;
}

GameState NewGame(const std::string& ProfileName, EBackground Background, EDifficulty Difficulty, uint64_t Seed)
{
	GameState S;
	S.ProfileName = ProfileName;
	S.Difficulty = Difficulty;
	S.Seed = Seed;
	S.Career.ApplyBackground(Background);
	S.Property = PropertyState(PropertyProfiles().front());
	S.Property.EventSeed = Seed ^ 0xC0FFEE5EEDULL;
	S.World.Stress = StressStart;
	return S;
}

std::vector<ResolvedConsequence> AdvanceGameWeeks(GameState& State, int Weeks)
{
	return State.Property.AdvanceWeeks(Weeks);
}
} // namespace kc
