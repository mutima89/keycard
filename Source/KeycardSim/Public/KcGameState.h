// Everything that persists between sessions. Serialised by KcSave. Format: Docs/SaveFormat.md.
#pragma once

#include "KcApi.h"

#include "KcCareer.h"
#include "KcLearning.h"
#include "KcMission.h"
#include "KcProperty.h"
#include "KcWorld.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kc
{
enum class EDifficulty
{
	Trainee,
	Professional,
	Expert
};

struct PlayerSettings
{
	double MouseSensitivity = 1.0;   // 0.1..5
	double TextScale = 1.0;          // 0.8..1.6
	double MasterVolume = 0.8;       // 0..1
	bool bReducedMotion = false;     // cuts instead of camera moves; ceremonies still play
	bool bHints = true;
	bool bFullscreen = true;
	int GraphicsQuality = 2;         // 0 low .. 3 epic
	int Upscaling = 0;               // 0 native, 1 quality, 2 balanced, 3 performance (TSR)
	int FrameCap = 0;                // 0 unlimited, else 30 / 60 / 120
	bool bVSync = true;
	bool bInvertY = false;
	double GamepadSensitivity = 1.0; // 0.1..5
	bool bColourSafe = false;        // blue/orange instead of green/red for good and bad
};

// The one place settings limits live: out-of-range or non-finite values are brought into range.
// Applied when settings change and when a save loads.
KC_API PlayerSettings ClampSettings(const PlayerSettings& In);

struct GameState
{
	std::string ProfileName = "Officer";
	EDifficulty Difficulty = EDifficulty::Professional;
	uint64_t Seed = 20261022;
	CareerState Career;
	std::string Checkpoint = "M001:start";
	PlayerSettings Settings;
	std::vector<std::string> FieldManual;     // procedure card ids the player has earned, in order
	std::vector<std::string> LearningHistory; // one line per debrief
	std::vector<std::string> Achievements;
	// Save v2:
	PropertyState Property;                   // the hotel the player answers for (metrics, flags, consequences)
	CampaignState Campaign;                   // the case in progress and every attempt
	WorldState World;                         // relationships, stress, clues, inbox, news
	ChallengeState Challenge;                 // survival, daily and classroom modes
	bool bCeremonyPending = false;            // a promotion happened and its ceremony has not played yet
	int CeremonyLevel = 0;                    // the level the pending ceremony celebrates
};

constexpr int DifficultyIndex(EDifficulty D) { return D == EDifficulty::Trainee ? 0 : (D == EDifficulty::Expert ? 2 : 1); }

// Advances hotel time: resolves due consequences.
KC_API std::vector<ResolvedConsequence> AdvanceGameWeeks(GameState& State, int Weeks);

// New career: level 1 LP Officer on nights at the Harbourfront, background starting scores, seed for every random event.
KC_API GameState NewGame(const std::string& ProfileName, EBackground Background, EDifficulty Difficulty, uint64_t Seed);
} // namespace kc
