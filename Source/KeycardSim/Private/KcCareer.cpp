#include "KcCareer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kc
{
static const char* const CompetencyNames[CompetencyCount] = {
	"IncidentResponse", "Investigation", "EvidenceReporting", "FireLifeSafety", "KeyAccessControl",
	"GuestServiceDeescalation", "PeopleLeadership", "ComplianceAudit", "FinancialAcumen", "CrisisLeadership"};

const char* ToString(ECompetency C)
{
	switch (C)
	{
	case ECompetency::IncidentResponse: return "Incident Response";
	case ECompetency::Investigation: return "Investigation";
	case ECompetency::EvidenceReporting: return "Evidence & Reporting";
	case ECompetency::FireLifeSafety: return "Fire & Life Safety";
	case ECompetency::KeyAccessControl: return "Key & Access Control";
	case ECompetency::GuestServiceDeescalation: return "Guest Service & De-escalation";
	case ECompetency::PeopleLeadership: return "People Leadership";
	case ECompetency::ComplianceAudit: return "Compliance & Audit";
	case ECompetency::FinancialAcumen: return "Financial Acumen";
	case ECompetency::CrisisLeadership: return "Crisis Leadership";
	case ECompetency::Count: break;
	}
	return "?";
}

bool CompetencyFromName(const std::string& Name, ECompetency& Out)
{
	for (int i = 0; i < CompetencyCount; ++i)
	{
		if (Name == CompetencyNames[i])
		{
			Out = static_cast<ECompetency>(i);
			return true;
		}
	}
	return false;
}

const char* ToString(EBackground B)
{
	switch (B)
	{
	case EBackground::ExPolice: return "Ex-police officer";
	case EBackground::FrontOffice: return "Hotel front office";
	case EBackground::Military: return "Military";
	case EBackground::CriminologyGraduate: return "Criminology graduate";
	case EBackground::Count: break;
	}
	return "?";
}

using C = ECompetency;

static std::vector<CareerLevelDef> BuildLadder()
{
	std::vector<CareerLevelDef> L;
	L.push_back({1, "LP Officer",
		"Night shift patrols, first response to incidents, observe and report, write incident reports.",
		"Radio, patrol tour, incident report writer, evidence bags", 0.0,
		{{C::IncidentResponse, 45}, {C::EvidenceReporting, 45}, {C::GuestServiceDeescalation, 40}},
		{"M001", "M002", "M003"}, 0, {}, 1, "property"});
	L.push_back({2, "Senior LP Officer",
		"Shift lead on the radio, train new officers, run the CCTV control room.",
		"CCTV review console, clip export with chain of custody, daily activity report", 500.0,
		{{C::IncidentResponse, 55}, {C::EvidenceReporting, 55}, {C::KeyAccessControl, 45}, {C::FireLifeSafety, 45}},
		{"M004", "M005", "M006", "M007"}, 0, {}, 2, "property"});
	L.push_back({3, "LP Supervisor",
		"Run a shift, schedule officers, first-line investigations, key control audits.",
		"Lock interrogation and key log viewer, evidence locker, shift schedule", 2500.0,
		{{C::Investigation, 55}, {C::KeyAccessControl, 58}, {C::PeopleLeadership, 45}, {C::EvidenceReporting, 60}},
		{"M008", "M009", "M010", "M011"}, 0, {}, 2, "property"});
	L.push_back({4, "Assistant LP Manager",
		"Fire and life safety programme, claims files, contractor control.",
		"Fire panel and fire watch log, claims tracker, contractor badges", 10000.0,
		{{C::FireLifeSafety, 62}, {C::ComplianceAudit, 55}, {C::FinancialAcumen, 45}, {C::Investigation, 60}},
		{"M012", "M013", "M014", "M015"}, 0, {}, 2, "property"});
	L.push_back({5, "LP Manager",
		"Department budget, policies, workplace investigations with HR, audits.",
		"Investigation case board, LP audit checklist, staffing and budget planner", 50000.0,
		{{C::PeopleLeadership, 62}, {C::ComplianceAudit, 62}, {C::FinancialAcumen, 58}, {C::Investigation, 65}},
		{"M016", "M017", "M018", "M019"}, 0, {}, 2, "property"});
	L.push_back({6, "Director of Loss Prevention",
		"Sits on the hotel's executive committee; crisis lead for the property.",
		"Crisis command log, shrink and loss analytics", 150000.0,
		{{C::CrisisLeadership, 62}, {C::PeopleLeadership, 66}, {C::FinancialAcumen, 64}, {C::ComplianceAudit, 66}},
		{"M020", "M021", "M022"}, 1, {"C001"}, 2, "property"});
	L.push_back({7, "Cluster Director of LP",
		"Two or three properties; one standard across sites.",
		"Cross-property dashboard, standards rollout", 400000.0,
		{{C::CrisisLeadership, 68}, {C::ComplianceAudit, 70}, {C::FinancialAcumen, 68}, {C::PeopleLeadership, 70}},
		{"M023", "M024", "M025", "M026"}, 1, {"C002"}, 2, "area"});
	L.push_back({8, "Area Director of Safety & Security",
		"An area of hotels, insurer and audit relationships, major incidents.",
		"Insurer and audit portal, area incident trends", 1000000.0,
		{{C::CrisisLeadership, 74}, {C::FinancialAcumen, 72}, {C::ComplianceAudit, 74}, {C::GuestServiceDeescalation, 70}},
		{"M027", "M028", "M029", "M030"}, 1, {"C003"}, 3, "area"});
	L.push_back({9, "Regional Director of Safety & Security",
		"Programmes, technology roll-outs, executive and legal.",
		"Technology business case, regional programme planner", 3000000.0,
		{{C::CrisisLeadership, 78}, {C::PeopleLeadership, 78}, {C::FinancialAcumen, 76}, {C::ComplianceAudit, 78}},
		{"M031", "M032", "M033", "M034"}, 1, {"C004"}, 3, "board"});
	L.push_back({10, "Vice President, Global Safety & Security",
		"Answers to the board, insurers and the public for safety and security across Veldane.",
		"Board paper and post-incident review", 10000000.0,
		{}, {}, 0, {}, 0, "board"});
	return L;
}

const std::vector<CareerLevelDef>& CareerLadder()
{
	static const std::vector<CareerLevelDef> Ladder = BuildLadder();
	return Ladder;
}

const CareerLevelDef& LevelDef(int Level)
{
	const auto& L = CareerLadder();
	const int Idx = std::max(1, std::min(static_cast<int>(L.size()), Level)) - 1;
	return L[static_cast<size_t>(Idx)];
}

// Order of StartScores follows ECompetency.
static std::vector<BackgroundDef> BuildBackgrounds()
{
	return {
		{EBackground::ExPolice, "Ex-police officer",
			"Strong on scenes, statements and evidence; used to powers a hotel officer does not have.",
			{{40, 40, 40, 25, 20, 20, 20, 20, 15, 15}}},
		{EBackground::FrontOffice, "Hotel front office",
			"Knows guests, systems and the keycard encoder; new to investigations.",
			{{25, 20, 25, 20, 35, 45, 25, 20, 25, 15}}},
		{EBackground::Military, "Military",
			"Calm under pressure, disciplined patrols and drills; less practice with guests.",
			{{40, 20, 25, 35, 30, 15, 30, 20, 15, 25}}},
		{EBackground::CriminologyGraduate, "Criminology graduate",
			"Methodical and analytical; little time on the floor.",
			{{20, 35, 35, 25, 20, 25, 15, 35, 30, 15}}},
	};
}

const BackgroundDef& Background(EBackground B)
{
	static const std::vector<BackgroundDef> Defs = BuildBackgrounds();
	const int Idx = std::max(0, std::min(BackgroundCount - 1, static_cast<int>(B)));
	return Defs[static_cast<size_t>(Idx)];
}

void CareerState::ApplyBackground(EBackground B)
{
	Origin = B;
	const BackgroundDef& Def = Background(B);
	for (int i = 0; i < CompetencyCount; ++i)
	{
		Competencies[static_cast<size_t>(i)].Score = Def.StartScores[static_cast<size_t>(i)];
		Competencies[static_cast<size_t>(i)].Assessments = 0;
	}
}

void CareerState::ApplyAssessment(const AssessmentResult& R)
{
	CompetencyRecord& Rec = Competencies[static_cast<size_t>(R.Competency)];
	const int Clamped = std::max(0, std::min(100, R.Score));
	if (Rec.Assessments == 0)
	{
		// The first real evidence replaces the background estimate.
		Rec.Score = Clamped;
	}
	else
	{
		Rec.Score = static_cast<int>(std::lround(Rec.Score * (1.0 - BlendWeight) + Clamped * BlendWeight));
	}
	++Rec.Assessments;
}

void CareerState::RecordMission(const std::string& MissionId, bool bPassed, int XpAward)
{
	if (std::find(AttemptedMissions.begin(), AttemptedMissions.end(), MissionId) == AttemptedMissions.end())
	{
		AttemptedMissions.push_back(MissionId);
	}
	if (bPassed && !HasPassed(MissionId))
	{
		PassedMissions.push_back(MissionId);
	}
	Xp += std::max(0, XpAward);
}

bool CareerState::HasPassed(const std::string& MissionId) const
{
	return std::find(PassedMissions.begin(), PassedMissions.end(), MissionId) != PassedMissions.end();
}

PromotionCheck CareerState::CheckPromotion() const
{
	PromotionCheck Out;
	const CareerLevelDef& Def = LevelDef(Level);
	if (Level >= TopLevel)
	{
		Out.UnmetGates.push_back("Already at the top of the ladder");
		return Out;
	}
	for (const CompetencyRequirement& Req : Def.PromotionRequirements)
	{
		const CompetencyRecord& Rec = Get(Req.Competency);
		if (Rec.Score < Req.MinScore)
		{
			Out.UnmetGates.push_back(std::string(ToString(Req.Competency)) + " below " + std::to_string(Req.MinScore));
		}
		if (Rec.Assessments < Def.MinAssessmentsPerRequirement)
		{
			Out.UnmetGates.push_back(std::string(ToString(Req.Competency)) + " needs more assessed evidence");
		}
	}
	for (const std::string& M : Def.RequiredMissions)
	{
		if (!HasPassed(M))
		{
			Out.UnmetGates.push_back("Mission " + M + " not passed");
		}
	}
	int Crises = 0;
	for (const std::string& Cr : Def.CrisisPool)
	{
		if (HasPassed(Cr)) ++Crises;
	}
	if (Crises < Def.RequiredCrises)
	{
		Out.UnmetGates.push_back("A career crisis must be led and passed at this level");
	}
	Out.bEligible = Out.UnmetGates.empty();
	return Out;
}

bool CareerState::TryPromote()
{
	if (!CheckPromotion().bEligible)
	{
		return false;
	}
	++Level;
	return true;
}
} // namespace kc
