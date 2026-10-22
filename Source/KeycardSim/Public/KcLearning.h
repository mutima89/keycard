// Learning and growth: the Field Manual (procedure cards earned from debriefs), the glossary,
// skills, endings, the performance review and certificate, challenge modes and the local
// leaderboard. Content: Data/FieldManual.json, Data/Glossary.json, Data/Endings.json, Data/Classroom.json.
// Nothing here changes a rubric score or a promotion gate.
#pragma once

#include "KcApi.h"

#include "KcMission.h"
#include "KcPeople.h"
#include "KcWorld.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kc
{
struct GameState;

// ---------------------------------------------------------------- Field Manual
struct ProcedureCard
{
	std::string Id;                 // "room_theft_report"
	std::string Title;
	std::string Topic;              // same vocabulary as MissionDef::Topic
	std::vector<std::string> Steps; // the procedure, in order
	std::string Why;
	std::vector<std::string> Pitfalls;
};

KC_API bool FieldManualFromJson(const std::string& Text, const std::string& SourceName, std::vector<ProcedureCard>& Out, std::vector<std::string>& OutErrors);
// Unique ids, steps present, and every mission's ProcedureCards exist.
KC_API std::vector<std::string> ValidateFieldManual(const std::vector<ProcedureCard>& Cards, const MissionCatalogue* Missions);
KC_API const ProcedureCard* FindCard(const std::vector<ProcedureCard>& Cards, const std::string& Id);
KC_API bool HasCard(const GameState& State, const std::string& CardId);

// ---------------------------------------------------------------- glossary
struct GlossaryTerm
{
	std::string Id;
	std::string Term;
	std::string Category;
	std::string Definition;
	std::string Example;
	std::vector<std::string> Missions;
};

KC_API bool GlossaryFromJson(const std::string& Text, const std::string& SourceName, std::vector<GlossaryTerm>& Out, std::vector<std::string>& OutErrors);
KC_API std::vector<std::string> ValidateGlossary(const std::vector<GlossaryTerm>& Terms, const MissionCatalogue* Missions);
KC_API std::vector<const GlossaryTerm*> TermsForMission(const std::vector<GlossaryTerm>& Terms, const std::string& MissionId);

// ---------------------------------------------------------------- skills
// Ten skills unlocked by rank and choice: one point per promotion and one from the background.
// They change the living world (stress, relationships, fallout, crisis deadlines) and open extra
// replies, never a rubric score or a promotion gate.
struct SkillDef
{
	const char* Id;
	const char* Name;
	const char* Description;
	int MinLevel;
};

KC_API const std::vector<SkillDef>& SkillDefs();
KC_API const SkillDef* FindSkill(const std::string& Id);
KC_API int SkillPointsEarned(const GameState& State);
KC_API int SkillPointsAvailable(const GameState& State);

enum class ESkillError
{
	None,
	Unknown,
	AlreadyLearned,
	NeedsRank,
	NoPoints
};

KC_API const char* ToString(ESkillError E);
KC_API ESkillError LearnSkill(GameState& State, const std::string& Id);

// Modifiers the world rules read (1.0 / 0 without the skill).
KC_API double StressGainFactor(const GameState& State);   // crisis_command and first_aid_trainer ease pressure
KC_API int ExtraRecoveryPerWeek(const GameState& State);
KC_API double RelationGainFactor(const GameState& State); // de_escalation_trainer
KC_API double RelationLossFactor(const GameState& State); // investigations_with_hr
KC_API int ExtraCrisisWeeks(const GameState& State);      // crisis_command
KC_API double FalloutFactor(const GameState& State);      // key_systems, fire_systems, cctv_forensics

// ---------------------------------------------------------------- endings
struct EndingDef
{
	std::string Id;
	std::string Title;
	WorldCondition When;
	std::string Epilogue;
};

KC_API bool EndingsFromJson(const std::string& Text, const std::string& SourceName, std::vector<EndingDef>& Out, std::vector<std::string>& OutErrors);
KC_API std::vector<std::string> ValidateEndings(const std::vector<EndingDef>& Endings);
KC_API const EndingDef* ChooseEnding(const GameState& State, const std::vector<EndingDef>& Endings);

// ---------------------------------------------------------------- performance review and certificate
struct PerformanceReview
{
	int Level = 0;
	std::string Title;
	int MissionsPassed = 0;
	int AverageScore = 0;
	int IncidentsOccurred = 0;
	int IncidentsAvoided = 0;
	int CardsEarned = 0;
	std::vector<std::string> Strengths;
	std::vector<std::string> Growth;
	std::string Ally;
	std::string Strained;
	std::string Summary;
};

KC_API PerformanceReview MakeReview(const GameState& State, const PeopleCatalogue& People);
KC_API std::string CertificateMarkdown(const GameState& State, const PerformanceReview& Review, const EndingDef* Ending, const std::string& Date);

// ---------------------------------------------------------------- challenge modes
enum class EGameMode
{
	Career,
	Survival,  // a busy night shift: incident after incident until three are failed
	Daily,     // three cases chosen by the date, one attempt each
	Classroom  // a fixed list set by a trainer, everyone plays the same cases
};

KC_API const char* ToString(EGameMode M);

struct ChallengeState
{
	EGameMode Mode = EGameMode::Career;
	std::vector<std::string> Sequence;
	int Index = 0;
	int Lives = 3;
	int Points = 0;
	bool bOver = false;
	std::string Date;
};

KC_API void StartChallenge(GameState& State, const MissionCatalogue& C, EGameMode Mode, const std::string& Date = std::string());
KC_API void StartCustomChallenge(GameState& State, const std::vector<std::string>& Sequence);
KC_API std::string CurrentChallengeMission(const GameState& State);
KC_API bool AdvanceChallenge(GameState& State, const MissionDebrief& D);
KC_API bool ChallengeUnlocks(const GameState& State, const std::string& MissionId);

// A trainer's fixed list of cases (classroom mode): everyone in the room plays the same ones.
struct ClassroomSet
{
	std::string Id;
	std::string Title;
	std::string Description;
	std::vector<std::string> Missions;
};

KC_API bool ClassroomFromJson(const std::string& Text, const std::string& SourceName, std::vector<ClassroomSet>& Out, std::vector<std::string>& OutErrors);
KC_API std::vector<std::string> ValidateClassroom(const std::vector<ClassroomSet>& Sets, const MissionCatalogue* Missions);

struct LeaderboardEntry
{
	std::string Mode;
	std::string Profile;
	std::string Detail;
	int Points = 0;
	int Weeks = 0;
	std::string When;
};

KC_API std::string SerializeLeaderboard(const std::vector<LeaderboardEntry>& Entries);
KC_API std::vector<LeaderboardEntry> DeserializeLeaderboard(const std::string& Text);
KC_API void AddToLeaderboard(std::vector<LeaderboardEntry>& Entries, const LeaderboardEntry& E);
} // namespace kc
