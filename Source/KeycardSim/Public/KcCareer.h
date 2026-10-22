// Career levels, competencies, backgrounds and promotion gates for hotel Loss Prevention.
// XP is tracked for feedback only and is never an input to promotion. Canonical rules: Docs/CareerModel.md.
#pragma once

#include "KcApi.h"

#include <array>
#include <string>
#include <vector>

namespace kc
{
enum class ECompetency
{
	IncidentResponse,
	Investigation,
	EvidenceReporting,
	FireLifeSafety,
	KeyAccessControl,
	GuestServiceDeescalation,
	PeopleLeadership,
	ComplianceAudit,
	FinancialAcumen,
	CrisisLeadership,
	Count
};

constexpr int CompetencyCount = static_cast<int>(ECompetency::Count);
KC_API const char* ToString(ECompetency C);
// Stable id used in data files ("IncidentResponse"). False if unknown.
KC_API bool CompetencyFromName(const std::string& Name, ECompetency& Out);

enum class EBackground
{
	ExPolice,
	FrontOffice,
	Military,
	CriminologyGraduate,
	Count
};

constexpr int BackgroundCount = static_cast<int>(EBackground::Count);
KC_API const char* ToString(EBackground B);

struct CompetencyRequirement
{
	ECompetency Competency;
	int MinScore; // 0..100
};

struct CareerLevelDef
{
	int Level = 1; // 1..10
	std::string Title;
	std::string Responsibilities;
	std::string NewTools;
	double DecisionAuthority = 0.0;                           // max spend approved without escalation
	std::vector<CompetencyRequirement> PromotionRequirements; // to move to Level+1
	std::vector<std::string> RequiredMissions;                // passed missions needed for promotion
	int RequiredCrises = 0;                                   // passed career crises (C001-C004) needed at this level
	std::vector<std::string> CrisisPool;                      // crises that count for this level
	int MinAssessmentsPerRequirement = 1;                     // evidence count per required competency
	// Who promotes the player: "property" (GM and executive committee), "area" (regional leadership), "board".
	std::string CeremonyStage;
};

// The ten-level ladder. Index 0 = level 1.
KC_API const std::vector<CareerLevelDef>& CareerLadder();
KC_API const CareerLevelDef& LevelDef(int Level);
constexpr int TopLevel = 10;

struct CompetencyRecord
{
	int Score = 0;       // 0..100, blended from assessments
	int Assessments = 0; // number of assessed pieces of evidence
};

struct AssessmentResult
{
	ECompetency Competency;
	int Score; // 0..100 for this piece of work
};

// A background's starting scores. Starting scores are not assessed evidence: promotion still needs
// work done in the game (Assessments stays 0 until the first mission).
struct BackgroundDef
{
	EBackground Id;
	std::string Title;
	std::string Summary;
	std::array<int, CompetencyCount> StartScores{};
};

KC_API const BackgroundDef& Background(EBackground B);

struct PromotionCheck
{
	bool bEligible = false;
	std::vector<std::string> UnmetGates; // human-readable reasons
};

class KC_API CareerState
{
public:
	static constexpr double BlendWeight = 0.4; // weight of newest assessment

	int Level = 1; // every career starts as LP Officer on nights
	int Xp = 0;
	EBackground Origin = EBackground::FrontOffice;
	std::array<CompetencyRecord, CompetencyCount> Competencies{};
	std::vector<std::string> PassedMissions;
	std::vector<std::string> AttemptedMissions;
	std::vector<std::string> Skills; // unlocked skill ids (Phase 2)

	void ApplyBackground(EBackground B);
	void ApplyAssessment(const AssessmentResult& R);
	void RecordMission(const std::string& MissionId, bool bPassed, int XpAward);
	bool HasPassed(const std::string& MissionId) const;

	PromotionCheck CheckPromotion() const;
	// Promotes only when CheckPromotion() is eligible. Returns true on promotion.
	bool TryPromote();

	const CompetencyRecord& Get(ECompetency C) const { return Competencies[static_cast<size_t>(C)]; }
};
} // namespace kc
