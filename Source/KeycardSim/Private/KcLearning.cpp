#include "KcLearning.h"

#include "KcGameState.h"
#include "KcJsonReader.h"
#include "KcRng.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <sstream>

namespace kc
{
using json::Reader;

// ---------------------------------------------------------------- Field Manual

bool FieldManualFromJson(const std::string& Text, const std::string& SourceName, std::vector<ProcedureCard>& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	std::vector<ProcedureCard> Cards;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		for (const JsonValue& V : R.Items("cards", true))
		{
			ProcedureCard C;
			Reader CR(V, SourceName + " card", OutErrors);
			CR.Str("id", C.Id, true);
			CR.Str("title", C.Title, true);
			CR.Str("topic", C.Topic, true);
			CR.Strings("steps", C.Steps, true);
			CR.Text("why", C.Why, true);
			CR.Strings("pitfalls", C.Pitfalls);
			Cards.push_back(C);
		}
	}
	Out = std::move(Cards);
	return OutErrors.size() == Before;
}

std::vector<std::string> ValidateFieldManual(const std::vector<ProcedureCard>& Cards, const MissionCatalogue* Missions)
{
	std::vector<std::string> Out;
	std::set<std::string> Ids;
	for (const ProcedureCard& C : Cards)
	{
		const std::string P = "FieldManual.json '" + C.Id + "'";
		if (!json::IsLowerId(C.Id) || !Ids.insert(C.Id).second) Out.push_back(P + ": id missing, not lower-case or duplicated");
		if (C.Steps.size() < 3) Out.push_back(P + ": a procedure needs at least three steps");
		for (const std::string& S : C.Steps)
		{
			if (S.empty()) Out.push_back(P + ": empty step");
		}
		if (C.Why.empty()) Out.push_back(P + ": needs a why");
	}
	if (Missions)
	{
		std::set<std::string> Used;
		for (const MissionDef& M : Missions->Missions)
		{
			for (const std::string& Id : M.ProcedureCards)
			{
				if (!Ids.count(Id)) Out.push_back(M.Id + ": unknown procedure card '" + Id + "'");
				Used.insert(Id);
			}
		}
		for (const ProcedureCard& C : Cards)
		{
			if (!Used.count(C.Id)) Out.push_back("FieldManual.json '" + C.Id + "': no mission teaches this card");
		}
	}
	return Out;
}

const ProcedureCard* FindCard(const std::vector<ProcedureCard>& Cards, const std::string& Id)
{
	for (const ProcedureCard& C : Cards)
	{
		if (C.Id == Id) return &C;
	}
	return nullptr;
}

bool HasCard(const GameState& State, const std::string& CardId)
{
	return std::find(State.FieldManual.begin(), State.FieldManual.end(), CardId) != State.FieldManual.end();
}

// ---------------------------------------------------------------- glossary

bool GlossaryFromJson(const std::string& Text, const std::string& SourceName, std::vector<GlossaryTerm>& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	std::vector<GlossaryTerm> Terms;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		for (const JsonValue& V : R.Items("terms", true))
		{
			GlossaryTerm T;
			Reader TR(V, SourceName + " term", OutErrors);
			TR.Str("id", T.Id, true);
			TR.Str("term", T.Term, true);
			TR.Str("category", T.Category, true);
			TR.Text("definition", T.Definition, true);
			TR.Text("example", T.Example, false);
			TR.Strings("missions", T.Missions);
			Terms.push_back(T);
		}
	}
	Out = std::move(Terms);
	return OutErrors.size() == Before;
}

std::vector<std::string> ValidateGlossary(const std::vector<GlossaryTerm>& Terms, const MissionCatalogue* Missions)
{
	std::vector<std::string> Out;
	std::set<std::string> Ids;
	for (const GlossaryTerm& T : Terms)
	{
		const std::string P = "Glossary.json '" + T.Id + "'";
		if (!json::IsLowerId(T.Id) || !Ids.insert(T.Id).second) Out.push_back(P + ": id missing, not lower-case or duplicated");
		if (T.Definition.size() < 20) Out.push_back(P + ": definition too short");
		for (const std::string& M : T.Missions)
		{
			if (Missions && !Missions->Find(M)) Out.push_back(P + ": unknown mission '" + M + "'");
		}
	}
	return Out;
}

std::vector<const GlossaryTerm*> TermsForMission(const std::vector<GlossaryTerm>& Terms, const std::string& MissionId)
{
	std::vector<const GlossaryTerm*> Out;
	for (const GlossaryTerm& T : Terms)
	{
		if (std::find(T.Missions.begin(), T.Missions.end(), MissionId) != T.Missions.end()) Out.push_back(&T);
	}
	return Out;
}

// ---------------------------------------------------------------- skills

const std::vector<SkillDef>& SkillDefs()
{
	static const std::vector<SkillDef> D = {
		{"interview_technique", "Interview technique", "Open questions come naturally: people tell you more, and relationship gains are a quarter larger.", 1},
		{"first_aid_trainer", "First aid trainer", "You have seen worse and trained others for it: stress gains are a quarter smaller.", 2},
		{"cctv_forensics", "CCTV forensics", "You find what the cameras saw before it is gone: weak evidence calls are less likely to come back as incidents.", 2},
		{"key_systems", "Key systems", "Locks, encoders and audit trails hold no secrets: weak key-control calls are less likely to turn into incidents.", 3},
		{"fire_systems", "Fire systems", "Panels, sprinklers and fire watch: opens replies that settle fire safety arguments with the facts.", 4},
		{"claims_negotiation", "Claims negotiation", "Opens replies that find the fair settlement with insurers and claimants.", 4},
		{"investigations_with_hr", "Investigations with HR", "Fair process wins trust even when the answer is hard: relationship losses are a quarter smaller.", 5},
		{"budget_and_staffing", "Budget and staffing", "Opens replies that win budget arguments with the numbers.", 5},
		{"de_escalation_trainer", "De-escalation trainer", "You switch off properly after a hard night: stress falls 2 more points a week.", 6},
		{"crisis_command", "Crisis command", "Incident command is second nature: crises wait two more weeks for you before they escalate.", 6},
	};
	return D;
}

const SkillDef* FindSkill(const std::string& Id)
{
	for (const SkillDef& S : SkillDefs())
	{
		if (Id == S.Id) return &S;
	}
	return nullptr;
}

int SkillPointsEarned(const GameState& State)
{
	// One from the background, one per promotion: ten by the top of the ladder.
	return 1 + std::max(0, State.Career.Level - 1);
}

int SkillPointsAvailable(const GameState& State)
{
	return std::max(0, SkillPointsEarned(State) - static_cast<int>(State.Career.Skills.size()));
}

const char* ToString(ESkillError E)
{
	switch (E)
	{
	case ESkillError::None: return "None";
	case ESkillError::Unknown: return "Unknown skill.";
	case ESkillError::AlreadyLearned: return "You already have this skill.";
	case ESkillError::NeedsRank: return "This skill opens at a higher rank.";
	case ESkillError::NoPoints: return "No skill points left: they come with promotions.";
	}
	return "?";
}

ESkillError LearnSkill(GameState& State, const std::string& Id)
{
	const SkillDef* S = FindSkill(Id);
	if (!S) return ESkillError::Unknown;
	if (HasSkill(State, Id)) return ESkillError::AlreadyLearned;
	if (State.Career.Level < S->MinLevel) return ESkillError::NeedsRank;
	if (SkillPointsAvailable(State) <= 0) return ESkillError::NoPoints;
	State.Career.Skills.push_back(Id);
	return ESkillError::None;
}

double StressGainFactor(const GameState& State) { return HasSkill(State, "first_aid_trainer") ? 0.75 : 1.0; }
int ExtraRecoveryPerWeek(const GameState& State) { return HasSkill(State, "de_escalation_trainer") ? 2 : 0; }
double RelationGainFactor(const GameState& State) { return HasSkill(State, "interview_technique") ? 1.25 : 1.0; }
double RelationLossFactor(const GameState& State) { return HasSkill(State, "investigations_with_hr") ? 0.75 : 1.0; }
int ExtraCrisisWeeks(const GameState& State) { return HasSkill(State, "crisis_command") ? 2 : 0; }

double FalloutFactor(const GameState& State)
{
	double F = 1.0;
	if (HasSkill(State, "cctv_forensics")) F *= 0.85;
	if (HasSkill(State, "key_systems")) F *= 0.85;
	return F;
}

// ---------------------------------------------------------------- endings

bool EndingsFromJson(const std::string& Text, const std::string& SourceName, std::vector<EndingDef>& Out, std::vector<std::string>& OutErrors)
{
	const size_t Before = OutErrors.size();
	JsonValue Doc;
	if (!json::ParseDoc(Text, SourceName, Doc, OutErrors)) return false;
	std::vector<EndingDef> Endings;
	{
		Reader R(Doc, SourceName, OutErrors);
		json::Schema(R);
		for (const JsonValue& V : R.Items("endings", true))
		{
			EndingDef E;
			Reader ER(V, SourceName + " ending", OutErrors);
			ER.Str("id", E.Id, true);
			ER.Str("title", E.Title, true);
			ER.Text("epilogue", E.Epilogue, true);
			if (const JsonValue* W = ER.Get("when", false)) json::ReadWorldCondition(*W, SourceName + " ending '" + E.Id + "' when", E.When, OutErrors);
			Endings.push_back(E);
		}
	}
	Out = std::move(Endings);
	return OutErrors.size() == Before;
}

static bool IsEmpty(const WorldCondition& C)
{
	return C.Flags.empty() && C.NotFlags.empty() && C.Passed.empty() && C.MinWeek == 0 && C.MinLevel == 0 && C.MaxLevel == 0 && C.MinClues == 0 &&
		C.MinStress == 0 && C.MinIncidents == 0 && C.Metrics.empty() && C.Relations.empty();
}

std::vector<std::string> ValidateEndings(const std::vector<EndingDef>& Endings)
{
	std::vector<std::string> Out;
	std::set<std::string> Ids;
	for (const EndingDef& E : Endings)
	{
		if (!json::IsLowerId(E.Id) || !Ids.insert(E.Id).second) Out.push_back("Endings.json '" + E.Id + "': id missing, not lower-case or duplicated");
		if (E.Title.empty() || E.Epilogue.empty()) Out.push_back("Endings.json '" + E.Id + "': needs a title and epilogue");
	}
	if (Endings.empty() || !IsEmpty(Endings.back().When)) Out.push_back("Endings.json: the last ending must have no condition (the default)");
	return Out;
}

const EndingDef* ChooseEnding(const GameState& State, const std::vector<EndingDef>& Endings)
{
	for (const EndingDef& E : Endings)
	{
		if (ConditionHolds(State, E.When)) return &E;
	}
	return nullptr;
}

// ---------------------------------------------------------------- review and certificate

PerformanceReview MakeReview(const GameState& State, const PeopleCatalogue& People)
{
	PerformanceReview R;
	R.Level = State.Career.Level;
	R.Title = LevelDef(State.Career.Level).Title;
	R.MissionsPassed = static_cast<int>(State.Career.PassedMissions.size());
	R.CardsEarned = static_cast<int>(State.FieldManual.size());
	int Sum = 0, N = 0;
	for (const MissionRecord& Rec : State.Campaign.Records)
	{
		if (Rec.bWhatIf) continue;
		Sum += Rec.Score;
		++N;
	}
	R.AverageScore = N ? Sum / N : 0;
	for (const ResolvedConsequence& H : State.Property.History)
	{
		R.IncidentsOccurred += H.bOccurred ? 1 : 0;
		R.IncidentsAvoided += H.bOccurred ? 0 : 1;
	}
	for (int i = 0; i < CompetencyCount; ++i)
	{
		const CompetencyRecord& C = State.Career.Competencies[static_cast<size_t>(i)];
		if (C.Assessments == 0) continue;
		if (C.Score >= 75) R.Strengths.push_back(ToString(static_cast<ECompetency>(i)));
		else if (C.Score < 60) R.Growth.push_back(ToString(static_cast<ECompetency>(i)));
	}
	int Best = RelationMin - 1, Worst = RelationMax + 1;
	for (const Relation& Rel : State.World.Relations)
	{
		const CharacterDef* P = People.Find(Rel.CharacterId);
		if (!P || !P->bStaff) continue;
		if (Rel.Value > Best)
		{
			Best = Rel.Value;
			R.Ally = P->Name;
		}
		if (Rel.Value < Worst)
		{
			Worst = Rel.Value;
			R.Strained = P->Name;
		}
	}
	if (Worst > RelationWary) R.Strained.clear();
	std::ostringstream S;
	S << "As " << R.Title << " you have closed " << R.MissionsPassed << " cases with an average score of " << R.AverageScore << ", and your Field Manual holds "
	  << R.CardsEarned << " procedure cards. ";
	if (!R.Strengths.empty()) S << "Your work is strongest in " << R.Strengths.front() << (R.Strengths.size() > 1 ? " and " + R.Strengths[1] : std::string()) << ". ";
	if (!R.Growth.empty()) S << "The next step is " << R.Growth.front() << ". ";
	S << R.IncidentsOccurred << " incidents happened on your watch and " << R.IncidentsAvoided << " were avoided. ";
	if (!R.Ally.empty()) S << R.Ally << " is your strongest supporter";
	if (!R.Strained.empty()) S << ", while things are strained with " << R.Strained;
	if (!R.Ally.empty()) S << ".";
	R.Summary = S.str();
	return R;
}

static const char* DifficultyName(EDifficulty D)
{
	return D == EDifficulty::Trainee ? "Trainee" : (D == EDifficulty::Expert ? "Expert" : "Professional");
}

std::string CertificateMarkdown(const GameState& State, const PerformanceReview& Review, const EndingDef* Ending, const std::string& Date)
{
	std::ostringstream S;
	S << "# Keycard career certificate\n\n";
	S << "**" << State.ProfileName << "**, " << Review.Title << " (level " << Review.Level << " of 10), Veldane Hotels & Resorts\n\n";
	if (Ending) S << "Ending: **" << Ending->Title << "**\n\n";
	S << "| | |\n|---|---|\n";
	S << "| Cases closed | " << Review.MissionsPassed << " |\n";
	S << "| Average score | " << Review.AverageScore << " |\n";
	S << "| Procedure cards | " << Review.CardsEarned << " |\n";
	S << "| Incidents (occurred / avoided) | " << Review.IncidentsOccurred << " / " << Review.IncidentsAvoided << " |\n";
	S << "| Weeks in LP | " << State.Property.Week << " |\n";
	S << "| Background | " << ToString(State.Career.Origin) << " |\n";
	S << "| Difficulty | " << DifficultyName(State.Difficulty) << " |\n\n";
	S << "## Competencies\n\n";
	for (int i = 0; i < CompetencyCount; ++i)
	{
		const CompetencyRecord& C = State.Career.Competencies[static_cast<size_t>(i)];
		if (C.Assessments > 0) S << "- " << ToString(static_cast<ECompetency>(i)) << ": " << C.Score << "\n";
	}
	S << "\n" << Review.Summary << "\n\n";
	S << "Issued " << Date << " by Keycard, a hotel Loss Prevention career simulation. Training-style fiction: a record of play, "
		 "not a professional qualification or legal advice. All names are fictional.\n";
	return S.str();
}

// ---------------------------------------------------------------- challenges

const char* ToString(EGameMode M)
{
	switch (M)
	{
	case EGameMode::Career: return "career";
	case EGameMode::Survival: return "survival";
	case EGameMode::Daily: return "daily";
	case EGameMode::Classroom: return "classroom";
	}
	return "?";
}

void StartChallenge(GameState& State, const MissionCatalogue& C, EGameMode Mode, const std::string& Date)
{
	ChallengeState& Ch = State.Challenge;
	Ch = ChallengeState();
	Ch.Mode = Mode;
	Ch.Date = Date;
	if (Mode == EGameMode::Career || Mode == EGameMode::Classroom) return;
	// Survival: a busy night shift of officer and supervisor incidents. Daily: any three cases.
	std::vector<std::string> Pool;
	for (const MissionDef& M : C.Missions)
	{
		if (M.Kind == EMissionKind::Capstone) continue;
		if (Mode == EGameMode::Survival && M.MinLevel > 3 && M.Kind != EMissionKind::Crisis) continue;
		Pool.push_back(M.Id);
	}
	std::sort(Pool.begin(), Pool.end());
	uint64_t Seed = State.Seed;
	if (Mode == EGameMode::Daily)
	{
		Seed = 1469598103934665603ULL;
		for (unsigned char Ch2 : Date)
		{
			Seed ^= Ch2;
			Seed *= 1099511628211ULL;
		}
	}
	Rng R(Seed);
	for (int i = static_cast<int>(Pool.size()) - 1; i > 0; --i) std::swap(Pool[static_cast<size_t>(i)], Pool[static_cast<size_t>(R.UniformInt(0, i))]);
	if (Mode == EGameMode::Daily && Pool.size() > 3) Pool.resize(3);
	Ch.Sequence = Pool;
	// Challenges are played with full authority: every tool open, every decision yours.
	State.Career.Level = TopLevel - 1;
}

void StartCustomChallenge(GameState& State, const std::vector<std::string>& Sequence)
{
	State.Challenge = ChallengeState();
	State.Challenge.Mode = EGameMode::Classroom;
	State.Challenge.Sequence = Sequence;
	State.Career.Level = TopLevel - 1;
}

std::string CurrentChallengeMission(const GameState& State)
{
	const ChallengeState& Ch = State.Challenge;
	if (Ch.Mode == EGameMode::Career || Ch.bOver || Ch.Index >= static_cast<int>(Ch.Sequence.size())) return std::string();
	return Ch.Sequence[static_cast<size_t>(Ch.Index)];
}

bool AdvanceChallenge(GameState& State, const MissionDebrief& D)
{
	ChallengeState& Ch = State.Challenge;
	if (Ch.Mode == EGameMode::Career || Ch.bOver || D.MissionId != CurrentChallengeMission(State)) return false;
	if (Ch.Mode == EGameMode::Survival)
	{
		if (D.bPassed) ++Ch.Points;
		else --Ch.Lives;
	}
	else
	{
		Ch.Points += D.Score;
	}
	++Ch.Index;
	Ch.bOver = Ch.Lives <= 0 || Ch.Index >= static_cast<int>(Ch.Sequence.size()) || State.World.Burnouts > 0;
	return Ch.bOver;
}

bool ChallengeUnlocks(const GameState& State, const std::string& MissionId)
{
	return !MissionId.empty() && MissionId == CurrentChallengeMission(State);
}

// ---------------------------------------------------------------- leaderboard

std::string SerializeLeaderboard(const std::vector<LeaderboardEntry>& Entries)
{
	auto Clean = [](std::string S) {
		std::replace(S.begin(), S.end(), '\t', ' ');
		std::replace(S.begin(), S.end(), '\n', ' ');
		std::replace(S.begin(), S.end(), '\r', ' ');
		return S;
	};
	std::ostringstream S;
	S << "KEYCARD-LEADERBOARD 1\n";
	for (const LeaderboardEntry& E : Entries)
	{
		S << Clean(E.Mode) << '\t' << Clean(E.Profile) << '\t' << Clean(E.Detail) << '\t' << E.Points << '\t' << E.Weeks << '\t' << Clean(E.When) << '\n';
	}
	return S.str();
}

std::vector<LeaderboardEntry> DeserializeLeaderboard(const std::string& Text)
{
	std::vector<LeaderboardEntry> Out;
	std::istringstream In(Text);
	std::string Line;
	if (!std::getline(In, Line) || Line.rfind("KEYCARD-LEADERBOARD", 0) != 0) return Out;
	while (std::getline(In, Line))
	{
		std::vector<std::string> F;
		std::string Cell;
		std::istringstream LS(Line);
		while (std::getline(LS, Cell, '\t')) F.push_back(Cell);
		if (F.size() != 6 || F[3].empty() || F[4].empty()) continue;
		LeaderboardEntry E;
		E.Mode = F[0];
		E.Profile = F[1];
		E.Detail = F[2];
		char* End = nullptr;
		E.Points = static_cast<int>(std::strtol(F[3].c_str(), &End, 10));
		if (*End != '\0') continue;
		E.Weeks = static_cast<int>(std::strtol(F[4].c_str(), &End, 10));
		if (*End != '\0') continue;
		E.When = F[5];
		Out.push_back(E);
	}
	return Out;
}

void AddToLeaderboard(std::vector<LeaderboardEntry>& Entries, const LeaderboardEntry& E)
{
	Entries.push_back(E);
	std::stable_sort(Entries.begin(), Entries.end(), [](const LeaderboardEntry& A, const LeaderboardEntry& B) {
		if (A.Mode != B.Mode) return A.Mode < B.Mode;
		return A.Points > B.Points;
	});
	std::vector<LeaderboardEntry> Kept;
	int Count = 0;
	std::string Mode;
	for (const LeaderboardEntry& X : Entries)
	{
		if (X.Mode != Mode)
		{
			Mode = X.Mode;
			Count = 0;
		}
		if (Count++ < 20) Kept.push_back(X);
	}
	Entries = std::move(Kept);
}
} // namespace kc
