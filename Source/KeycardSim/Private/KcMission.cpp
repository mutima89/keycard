#include "KcMission.h"

#include "KcGameState.h"
#include "KcLearning.h"
#include "KcLpMath.h"
#include "KcRng.h"
#include "KcWorld.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <set>

namespace kc
{
// ---------------------------------------------------------------- names

const char* ToString(EMissionKind K)
{
	switch (K)
	{
	case EMissionKind::Mission: return "Mission";
	case EMissionKind::Crisis: return "Crisis";
	case EMissionKind::Capstone: return "Capstone";
	}
	return "?";
}

static const char* const RubricNames[RubricCount] = {"Procedure", "Evidence", "Compliance", "Care", "Report", "Communication", "Cost"};

const char* ToString(ERubric R)
{
	switch (R)
	{
	case ERubric::Procedure: return "Procedure and order";
	case ERubric::Evidence: return "Evidence and chain of custody";
	case ERubric::Compliance: return "Policy and local law";
	case ERubric::Care: return "Guest and staff care";
	case ERubric::Report: return "The written report";
	case ERubric::Communication: return "Communication";
	case ERubric::Cost: return "Cost";
	case ERubric::Count: break;
	}
	return "?";
}

const char* RubricName(ERubric R)
{
	const int I = static_cast<int>(R);
	return I >= 0 && I < RubricCount ? RubricNames[I] : "?";
}

bool RubricFromName(const std::string& Name, ERubric& Out)
{
	for (int i = 0; i < RubricCount; ++i)
	{
		if (Name == RubricNames[i])
		{
			Out = static_cast<ERubric>(i);
			return true;
		}
	}
	return false;
}

static const char* const ChannelNames[static_cast<int>(EChannel::Count)] = {"radio", "frontDesk", "phone", "firePanel", "email", "letter", "inPerson"};

const char* ToString(EChannel C)
{
	switch (C)
	{
	case EChannel::Radio: return "Radio";
	case EChannel::FrontDesk: return "Front desk";
	case EChannel::Phone: return "Phone";
	case EChannel::FirePanel: return "Fire panel";
	case EChannel::Email: return "Email";
	case EChannel::Letter: return "Letter";
	case EChannel::InPerson: return "In person";
	case EChannel::Count: break;
	}
	return "?";
}

bool ChannelFromName(const std::string& Name, EChannel& Out)
{
	for (int i = 0; i < static_cast<int>(EChannel::Count); ++i)
	{
		if (Name == ChannelNames[i])
		{
			Out = static_cast<EChannel>(i);
			return true;
		}
	}
	return false;
}

static const char* const ActionKindNames[static_cast<int>(EActionKind::Count)] = {"look", "interview", "lockAudit", "cctvReview", "exportFootage",
	"photograph", "bagEvidence", "logEvidence", "check", "call", "secure", "search", "detain", "enter", "other"};

const char* ToString(EActionKind K)
{
	const int I = static_cast<int>(K);
	return I >= 0 && I < static_cast<int>(EActionKind::Count) ? ActionKindNames[I] : "?";
}

bool ActionKindFromName(const std::string& Name, EActionKind& Out)
{
	for (int i = 0; i < static_cast<int>(EActionKind::Count); ++i)
	{
		if (Name == ActionKindNames[i])
		{
			Out = static_cast<EActionKind>(i);
			return true;
		}
	}
	return false;
}

const char* ToString(EQuestionKind K)
{
	switch (K)
	{
	case EQuestionKind::Single: return "single";
	case EQuestionKind::Multi: return "multi";
	case EQuestionKind::Numeric: return "numeric";
	case EQuestionKind::Order: return "order";
	}
	return "?";
}

const char* ToString(ECheckKind K)
{
	switch (K)
	{
	case ECheckKind::Done: return "done";
	case ECheckKind::NotDone: return "notDone";
	case ECheckKind::Before: return "before";
	case ECheckKind::ByMinute: return "byMinute";
	}
	return "?";
}

const char* ToString(EMissionError E)
{
	switch (E)
	{
	case EMissionError::None: return "None";
	case EMissionError::Unavailable: return "This case is not open to you yet.";
	case EMissionError::AnotherMissionActive: return "Close or set aside the case in progress first.";
	case EMissionError::NotActive: return "Open the case first.";
	case EMissionError::UnknownAction: return "Unknown action.";
	case EMissionError::AlreadyDone: return "You have already done that.";
	case EMissionError::WrongPlace: return "That has to be done in person: go to where it is.";
	case EMissionError::NeedsEarlierStep: return "Something has to happen before that.";
	case EMissionError::RequiredStepMissing: return "The case is not worked yet: there is still something you must do in person.";
	case EMissionError::UnansweredQuestion: return "Answer every part of the report.";
	case EMissionError::AnsweredInPerson: return "That was a face-to-face choice; it cannot be changed on the form.";
	case EMissionError::UnknownOption: return "Unknown answer.";
	case EMissionError::SingleNeedsOneOption: return "Choose exactly one option where asked.";
	case EMissionError::OrderNeedsEveryOption: return "Put every step in order.";
	case EMissionError::FileWhereReportsAre: return "Reports are written and filed at the LP office.";
	case EMissionError::NotSubmitted: return "Close the case once before replaying it.";
	}
	return "?";
}

// ---------------------------------------------------------------- places

const std::vector<LocationDef>& KnownLocations()
{
	// One building: B2 garage and plant, B1 back of house, ground floor public areas, mezzanine
	// ballroom, 3 pool and spa, 4 executive offices, 14 the guest floor of the slice.
	static const std::vector<LocationDef> L = {
		{"garage", "Parking garage", -2},
		{"engineering", "Engineering and plant room", -2},
		{"lp_office", "LP office", -1},
		{"cctv_room", "CCTV control room", -1},
		{"back_of_house", "Back-of-house corridor", -1},
		{"loading_dock", "Loading dock and receiving", -1},
		{"housekeeping", "Housekeeping and laundry", -1},
		{"lobby", "Lobby", 0},
		{"front_desk", "Front desk", 0},
		{"lobby_bar", "Lobby bar", 0},
		{"bell_stand", "Bell stand and luggage room", 0},
		{"fire_command", "Fire command centre", 0},
		{"kitchen", "Main kitchen", 0},
		{"ballroom", "Harbour Ballroom", 1},
		{"pool_spa", "Pool and spa", 3},
		{"exec_offices", "Executive offices", 4},
		{"gm_office", "General Manager's office", 4},
		{"boardroom", "Boardroom", 4},
		{"f14_corridor", "Floor 14 corridor", 14},
		{"room_1412", "Room 1412", 14},
		{"f14_service", "Floor 14 service closet", 14},
		{"guest_room", "Guest room", 14},
	};
	return L;
}

const LocationDef* FindLocation(const std::string& Id)
{
	for (const LocationDef& L : KnownLocations())
	{
		if (L.Id == Id) return &L;
	}
	return nullptr;
}

const std::vector<SpotDef>& KnownSpots()
{
	static const std::vector<SpotDef> S = {
		{"lp_desk", "lp_office", "Your desk"},
		{"lp_radio", "lp_office", "LP radio base"},
		{"evidence_locker", "lp_office", "Evidence locker"},
		{"report_terminal", "lp_office", "Incident report terminal"},
		{"cctv_console", "cctv_room", "CCTV console"},
		{"key_encoder", "front_desk", "Keycard encoder"},
		{"desk_phone", "front_desk", "Front desk phone"},
		{"lost_property", "front_desk", "Lost property safe"},
		{"door_1406", "f14_corridor", "Door of 1406"},
		{"door_1412", "f14_corridor", "Lock on 1412"},
		{"door_1418", "f14_corridor", "Door of 1418"},
		{"door_1409", "f14_corridor", "Door of 1409"},
		{"aed_cabinet", "lobby", "AED cabinet"},
		{"unattended_bag", "lobby", "Unattended bag"},
		{"luggage_room", "bell_stand", "Luggage room"},
		{"luggage_log", "bell_stand", "Luggage tag book"},
		{"key_cabinet", "housekeeping", "Master key cabinet"},
		{"dock_door", "loading_dock", "Dock door"},
		{"front_office_pc", "front_desk", "Front office terminal"},
		{"fire_panel", "fire_command", "Fire alarm control panel"},
		{"door_1416", "f14_corridor", "Door of 1416"},
		{"valve_14", "f14_corridor", "Sprinkler control valve, floor 14"},
		{"wet_floor", "lobby", "Street doors floor"},
		{"kitchen_line", "kitchen", "Cook line and extract hood"},
		{"permit_board", "kitchen", "Permit board"},
		{"hr_office", "exec_offices", "HR interview room"},
		{"finance_office", "exec_offices", "Finance office"},
		{"banquet_office", "ballroom", "Banquet office"},
		{"banquet_bar", "ballroom", "Banquet bar store"},
		{"corridor_14", "f14_corridor", "Floor 14 corridor"},
		{"room_1412_desk", "room_1412", "Desk in 1412"},
		{"room_1412_safe", "room_1412", "Safe in 1412"},
		{"room_1406_threshold", "guest_room", "Doorway of 1406"},
		{"service_cart", "f14_service", "Housekeeping trolley"},
	};
	return S;
}

const SpotDef* FindSpot(const std::string& Id)
{
	for (const SpotDef& S : KnownSpots())
	{
		if (S.Id == Id) return &S;
	}
	return nullptr;
}

std::string LocationName(const std::string& Id)
{
	const LocationDef* L = FindLocation(Id);
	return L ? L->Name : std::string();
}

int TravelMinutes(const std::string& From, const std::string& To)
{
	if (From.empty() || From == To) return 0;
	const LocationDef* A = FindLocation(From);
	const LocationDef* B = FindLocation(To);
	if (!A || !B) return 0;
	return A->Floor == B->Floor ? 2 : 5;
}

std::string ClockText(int Minute)
{
	const int M = ((Minute % 1440) + 1440) % 1440;
	char Buf[8];
	std::snprintf(Buf, sizeof(Buf), "%02d:%02d", M / 60, M % 60);
	return Buf;
}

bool ParseClock(const std::string& Text, int& OutMinute)
{
	if (Text.size() != 5 || Text[2] != ':') return false;
	for (size_t i : {0u, 1u, 3u, 4u})
	{
		if (!std::isdigit(static_cast<unsigned char>(Text[i]))) return false;
	}
	const int H = (Text[0] - '0') * 10 + (Text[1] - '0');
	const int M = (Text[3] - '0') * 10 + (Text[4] - '0');
	if (H > 23 || M > 59) return false;
	OutMinute = H * 60 + M;
	return true;
}

// ---------------------------------------------------------------- lookups

const ActionDef* MissionDef::FindAction(const std::string& ActionId) const
{
	for (const ActionDef& A : Actions)
	{
		if (A.Id == ActionId) return &A;
	}
	return nullptr;
}

const QuestionDef* MissionDef::FindQuestion(const std::string& QuestionId) const
{
	for (const QuestionDef& Q : Questions)
	{
		if (Q.Id == QuestionId) return &Q;
	}
	return nullptr;
}

const MissionDef* MissionCatalogue::Find(const std::string& Id) const
{
	for (const MissionDef& M : Missions)
	{
		if (M.Id == Id) return &M;
	}
	return nullptr;
}

static bool Contains(const std::vector<std::string>& V, const std::string& S)
{
	return std::find(V.begin(), V.end(), S) != V.end();
}

static const MissionAnswer* FindAnswer(const std::vector<MissionAnswer>& Answers, const std::string& QuestionId)
{
	for (const MissionAnswer& A : Answers)
	{
		if (A.QuestionId == QuestionId) return &A;
	}
	return nullptr;
}

static const OptionDef* FindOption(const QuestionDef& Q, const std::string& OptionId)
{
	for (const OptionDef& O : Q.Options)
	{
		if (O.Id == OptionId) return &O;
	}
	return nullptr;
}

static int StepIndex(const std::vector<FieldStep>& Steps, const std::string& ActionId)
{
	for (size_t i = 0; i < Steps.size(); ++i)
	{
		if (Steps[i].ActionId == ActionId) return static_cast<int>(i);
	}
	return -1;
}

// ---------------------------------------------------------------- answers

bool ParseNumericAnswer(const std::string& Text, double& Out)
{
	std::string Clean;
	for (char Ch : Text)
	{
		if (Ch == ',' || Ch == ' ') continue; // thousands separators as players type them
		Clean += Ch;
	}
	if (Clean.empty() || Clean.size() > 32) return false;
	char* End = nullptr;
	const double V = std::strtod(Clean.c_str(), &End);
	if (End == Clean.c_str() || *End != '\0' || !std::isfinite(V)) return false;
	Out = V;
	return true;
}

static std::string FormatFigure(double V)
{
	if (std::fabs(V - std::round(V)) < 1e-9) return FormatMoney(V);
	char Buf[64];
	std::snprintf(Buf, sizeof(Buf), "%.2f", V);
	return Buf;
}

MissionAnswer BestAnswer(const QuestionDef& Q)
{
	MissionAnswer A{Q.Id, {}};
	switch (Q.Kind)
	{
	case EQuestionKind::Multi:
		for (const OptionDef& O : Q.Options)
		{
			if (O.bCorrect) A.OptionIds.push_back(O.Id);
		}
		break;
	case EQuestionKind::Numeric:
	{
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.10g", Q.Answer);
		A.OptionIds.push_back(Buf);
		break;
	}
	case EQuestionKind::Order:
		for (const OptionDef& O : Q.Options) A.OptionIds.push_back(O.Id);
		break;
	case EQuestionKind::Single:
		if (!Q.Options.empty())
		{
			A.OptionIds.push_back(std::max_element(Q.Options.begin(), Q.Options.end(),
				[](const OptionDef& L, const OptionDef& R) { return L.Score < R.Score; })->Id);
		}
		break;
	}
	return A;
}

double IncidentProbabilityMultiplier(int Difficulty)
{
	return Difficulty <= 0 ? 0.6 : (Difficulty >= 2 ? 1.4 : 1.0);
}

// ---------------------------------------------------------------- checks

// 100 met, 40 done but late or out of order, 0 missed.
static int EvaluateCheck(const CheckDef& C, const std::vector<FieldStep>& Steps)
{
	const int A = StepIndex(Steps, C.Action);
	switch (C.Kind)
	{
	case ECheckKind::Done: return A >= 0 ? 100 : 0;
	case ECheckKind::NotDone: return A < 0 ? 100 : 0;
	case ECheckKind::Before:
	{
		if (A < 0) return 0;
		const int B = StepIndex(Steps, C.Other);
		return (B < 0 || A < B) ? 100 : 40;
	}
	case ECheckKind::ByMinute:
		if (A < 0) return 0;
		return Steps[static_cast<size_t>(A)].Minute <= C.Minute ? 100 : 40;
	}
	return 0;
}

// Walks an action sequence from the mission start without a GameState (validation and tests).
static bool SimulateSteps(const MissionDef& M, const std::vector<std::string>& Order, std::vector<FieldStep>& Out, std::string& Error)
{
	Out.clear();
	int Clock = M.StartMinute;
	std::string Where = M.Opening.At;
	for (const std::string& Id : Order)
	{
		const ActionDef* A = M.FindAction(Id);
		if (!A)
		{
			Error = "bestOrder names unknown action '" + Id + "'";
			return false;
		}
		if (StepIndex(Out, Id) >= 0)
		{
			Error = "bestOrder repeats '" + Id + "'";
			return false;
		}
		for (const std::string& R : A->Requires)
		{
			if (StepIndex(Out, R) < 0)
			{
				Error = "bestOrder does '" + Id + "' before its required step '" + R + "'";
				return false;
			}
		}
		Clock += TravelMinutes(Where, A->At) + std::max(0, A->Minutes);
		Where = A->At;
		Out.push_back({Id, Clock, A->ExpiresAt > 0 && Clock > A->ExpiresAt});
	}
	return true;
}

// ---------------------------------------------------------------- validation

static void ValidateEffectsDef(const EffectsDef& E, const std::string& Where, std::set<std::string>& ConsequenceIds, std::vector<std::string>& Out)
{
	for (const MetricEffect& Me : E.Metrics)
	{
		const int I = static_cast<int>(Me.Metric);
		if (I < 0 || I >= PropertyMetricCount) Out.push_back(Where + ": unknown metric");
		else if (!std::isfinite(Me.Delta)) Out.push_back(Where + ": effect is not a number");
		else if (IsIndexMetric(Me.Metric) && std::fabs(Me.Delta) > 50.0) Out.push_back(Where + ": index effect above 50 points");
	}
	if (E.Stress < -30 || E.Stress > 30) Out.push_back(Where + ": stress change outside -30..30");
	for (const RelationEffect& R : E.Relations)
	{
		if (R.CharacterId.empty() || R.Delta < -40 || R.Delta > 40) Out.push_back(Where + ": relation effect needs a character and a change within 40");
	}
	for (const std::string& F : E.SetsFlags)
	{
		if (F.empty()) Out.push_back(Where + ": empty flag");
	}
	for (const ConsequenceDef& Cq : E.Schedules)
	{
		const std::string CP = Where + " consequence '" + Cq.Id + "'";
		if (Cq.Id.empty() || !ConsequenceIds.insert(Cq.Id).second) Out.push_back(CP + ": id missing or duplicated");
		if (Cq.Description.empty()) Out.push_back(CP + ": missing description");
		if (Cq.DelayWeeks < 1) Out.push_back(CP + ": delay must be at least one week");
		if (!(Cq.Probability >= 0.0 && Cq.Probability <= 1.0)) Out.push_back(CP + ": probability outside 0..1");
		if (Cq.MitigatedProbabilityFactor < 0.0 || Cq.MitigatedSeverityFactor < 0.0) Out.push_back(CP + ": negative mitigation factor");
		for (const MetricEffect& Me : Cq.Effects)
		{
			if (IsIndexMetric(Me.Metric) && std::fabs(Me.Delta) > 50.0) Out.push_back(CP + ": index effect above 50 points");
		}
	}
}

static bool ValidId(const std::string& Id)
{
	if (Id.empty()) return false;
	for (char Ch : Id)
	{
		if (!(std::isalnum(static_cast<unsigned char>(Ch)) || Ch == '_' || Ch == '-')) return false;
	}
	return true;
}

std::vector<std::string> ValidateMission(const MissionDef& M)
{
	std::vector<std::string> Out;
	const std::string P = (M.Id.empty() ? std::string("(no id)") : M.Id) + ": ";
	auto Err = [&](const std::string& Msg) { Out.push_back(P + Msg); };
	if (!ValidId(M.Id)) Err("missing id or invalid characters");
	if (M.Title.empty()) Err("missing title");
	if (M.Topic.empty()) Err("missing topic");
	if (!FindProperty(M.Property)) Err("unknown property '" + M.Property + "'");
	if (M.Briefing.empty()) Err("missing briefing");
	if (M.MinLevel < 1 || M.MinLevel > TopLevel) Err("minLevel outside 1..10");
	if (M.PassMark < 1 || M.PassMark > 100) Err("passMark outside 1..100");
	if (M.Weeks < 0 || M.Weeks > 52) Err("weeks outside 0..52");
	if (M.Xp < 0) Err("negative xp");
	if (Contains(M.RequiresPassed, M.Id)) Err("requires itself");
	if (M.RequiresClues < 0) Err("negative requiresClues");
	if (M.StartMinute < 0 || M.StartMinute >= 1440) Err("start time outside the day");
	if (M.Opening.Text.empty() || M.Opening.From.empty()) Err("the opening needs a sender and words");
	if (!FindLocation(M.Opening.At)) Err("the opening is at an unknown location '" + M.Opening.At + "'");
	if (!M.ReportAt.empty() && !FindLocation(M.ReportAt)) Err("reportAt is an unknown location '" + M.ReportAt + "'");
	if (M.LearningObjectives.empty() || M.LearningObjectives.size() > 3) Err("needs one to three learning objectives");
	for (const std::string& L : M.LearningObjectives)
	{
		if (L.empty()) Err("empty learning objective");
	}
	for (const ContextNoteDef& C : M.Context)
	{
		if (C.Text.empty()) Err("context note without text");
		if (C.Flag.empty() && !C.bUseMetric && C.RelationId.empty() && C.MinClues <= 0) Err("context note without a condition");
	}
	if (M.DeadlineWeeks < 0 || M.DeadlineWeeks > 26) Err("deadlineWeeks outside 0..26");
	if (M.DeadlineWeeks > 0 && (M.Kind != EMissionKind::Crisis || M.RequiresFlags.empty()))
	{
		Err("only incident-triggered crises (with requiresFlags) can have a deadline");
	}
	if (M.DeadlineWeeks == 0 && (!M.EscalationEffects.empty() || !M.EscalationText.empty())) Err("escalation needs deadlineWeeks");
	if (M.Debrief.WhatHappened.empty() || M.Debrief.Why.empty()) Err("the debrief needs what happened and why");
	if (M.Debrief.BestPractice.size() < 3) Err("the debrief needs at least three best-practice steps");
	if (M.Debrief.CommonMistakes.empty()) Err("the debrief needs common mistakes");
	if (M.ProcedureCards.empty()) Err("no procedure card for the Field Manual");

	std::set<std::string> ConsequenceIds;
	// Actions: worked on foot.
	std::set<std::string> ActionIds;
	std::set<std::string> Places;
	bool bConversation = false;
	if (M.Actions.size() < 3) Err("needs at least three fieldwork actions");
	for (const ActionDef& A : M.Actions)
	{
		const std::string AP = "action '" + A.Id + "'";
		if (!ValidId(A.Id) || !ActionIds.insert(A.Id).second) Err(AP + ": id missing or duplicated");
		if (A.Label.empty()) Err(AP + ": missing label");
		if (!FindLocation(A.At)) Err(AP + ": unknown location '" + A.At + "'");
		if (!A.Spot.empty())
		{
			const SpotDef* Spot = FindSpot(A.Spot);
			if (!Spot) Err(AP + ": unknown spot '" + A.Spot + "'");
			else if (Spot->Location != A.At) Err(AP + ": spot '" + A.Spot + "' is not in " + A.At);
		}
		Places.insert(A.At);
		if (A.Minutes < 0 || A.Minutes > 240) Err(AP + ": minutes outside 0..240");
		if (A.Finding.empty()) Err(AP + ": needs a finding");
		if ((A.Kind == EActionKind::Interview || A.Kind == EActionKind::Call) && A.Person.empty()) Err(AP + ": an interview or call needs a person");
		if (A.Kind == EActionKind::Interview && !A.Person.empty()) bConversation = true;
		if (!A.Rows.empty() && A.Columns.empty()) Err(AP + ": table rows need columns");
		for (const auto& Row : A.Rows)
		{
			if (Row.size() != A.Columns.size()) Err(AP + ": a table row does not match the columns");
		}
		if ((A.ExpiresAt > 0) != !A.ExpiredFinding.empty()) Err(AP + ": expiresAt and expiredFinding go together");
		if (!A.Custody.empty() && A.Evidence.empty()) Err(AP + ": a custody entry needs an evidence id");
		if (A.Cost < 0.0 || !std::isfinite(A.Cost)) Err(AP + ": invalid cost");
		for (const DialogueLine& L : A.Lines)
		{
			if (L.Speaker.empty() || L.Text.empty()) Err(AP + ": a line needs a speaker and words");
		}
		for (const std::string& R : A.Requires)
		{
			if (R == A.Id || !M.FindAction(R)) Err(AP + ": requires unknown action '" + R + "'");
		}
		ValidateEffectsDef(A.Effects, P + AP, ConsequenceIds, Out);
	}
	if (Places.size() < 2) Err("the case must be worked in at least two places");
	if (!bConversation) Err("the case needs at least one face-to-face conversation");

	// Questions and checks share the 100 weight points.
	int WeightSum = 0;
	std::set<int> Dimensions;
	std::set<std::string> QuestionIds;
	for (const QuestionDef& Q : M.Questions)
	{
		const std::string QP = "question '" + Q.Id + "'";
		if (!ValidId(Q.Id) || !QuestionIds.insert(Q.Id).second) Err(QP + ": id missing or duplicated");
		if (Q.Prompt.empty()) Err(QP + ": missing prompt");
		if (Q.Weight <= 0) Err(QP + ": weight must be positive");
		WeightSum += Q.Weight;
		Dimensions.insert(static_cast<int>(Q.Dimension));
		static const std::vector<std::string> Sections = {"", "who", "what", "when", "where", "how", "actions", "facts"};
		if (!Contains(Sections, Q.Section)) Err(QP + ": unknown report section '" + Q.Section + "'");
		if (!Q.At.empty() && !FindLocation(Q.At)) Err(QP + ": unknown location '" + Q.At + "'");
		if (!Q.Person.empty() && Q.At.empty()) Err(QP + ": a person needs a location");
		if (!Q.At.empty() && !Q.Section.empty()) Err(QP + ": report questions are answered on the form, not in person");
		for (const std::string& R : Q.Requires)
		{
			if (!M.FindAction(R)) Err(QP + ": requires unknown action '" + R + "'");
		}
		if (Q.Kind == EQuestionKind::Numeric)
		{
			if (!Q.Options.empty()) Err(QP + ": numeric questions have no options");
			if (Q.Explanation.empty()) Err(QP + ": needs an explanation for the debrief");
			if (!std::isfinite(Q.Answer)) Err(QP + ": answer is not a number");
			if (!(Q.Tolerance > 0.0 && Q.Tolerance <= 0.5)) Err(QP + ": tolerance outside (0, 0.5]");
			for (const NumericMistake& Mk : Q.Mistakes)
			{
				if (!std::isfinite(Mk.Value) || Mk.Feedback.empty()) Err(QP + ": a mistake needs a value and feedback");
				else if (std::fabs(Mk.Value - Q.Answer) <= Q.Tolerance * std::max(std::fabs(Q.Answer), 1.0)) Err(QP + ": a mistake is within tolerance of the answer");
			}
			if (!Q.Formula.empty())
			{
				double V = 0.0;
				std::string E;
				if (!lp::EvaluateFormula(Q.Formula, Q.FormulaInputs, V, E)) Err(QP + ": " + E);
				else if (std::fabs(V - Q.Answer) > 1e-6 * std::max(1.0, std::fabs(V))) Err(QP + ": answer does not match its formula");
			}
			continue;
		}
		if (!Q.Formula.empty()) Err(QP + ": only numeric questions have a formula");
		if (Q.Kind == EQuestionKind::Order)
		{
			if (Q.Options.size() < 3) Err(QP + ": an order question needs at least three steps");
			if (Q.Explanation.empty()) Err(QP + ": needs an explanation for the debrief");
		}
		else if (Q.Options.size() < 2)
		{
			Err(QP + ": needs at least two options");
		}
		std::set<std::string> OptionIds;
		int Best = -1;
		int CorrectCount = 0;
		for (const OptionDef& O : Q.Options)
		{
			const std::string OP = QP + " option '" + O.Id + "'";
			if (!ValidId(O.Id) || !OptionIds.insert(O.Id).second) Err(OP + ": id missing or duplicated");
			if (O.Label.empty()) Err(OP + ": missing label");
			if (O.Score < 0 || O.Score > 100) Err(OP + ": score outside 0..100");
			if (O.Cost < 0.0 || !std::isfinite(O.Cost)) Err(OP + ": invalid cost");
			if (Q.Kind != EQuestionKind::Single && O.Cost > 0.0) Err(OP + ": costs belong on single-choice questions");
			if (O.bCritical && O.Score >= FalloutScoreThreshold) Err(OP + ": a critical breach cannot score 50 or more");
			if (Q.Kind == EQuestionKind::Order && (O.Score != 0 || O.bCorrect || O.bCritical)) Err(OP + ": order steps carry no score");
			if (!O.Reply.Text.empty() && Q.At.empty()) Err(OP + ": a spoken reply needs an in-person question");
			if (!O.Reply.Text.empty() && O.Reply.Speaker.empty()) Err(OP + ": a reply needs a speaker");
			Best = std::max(Best, O.Score);
			CorrectCount += O.bCorrect ? 1 : 0;
			ValidateEffectsDef(O.Effects, P + OP, ConsequenceIds, Out);
		}
		if (Q.Kind == EQuestionKind::Single && Best < 80) Err(QP + ": no option scores 80 or more");
		if (Q.Kind == EQuestionKind::Multi && (CorrectCount == 0 || CorrectCount == static_cast<int>(Q.Options.size())))
		{
			Err(QP + ": multi-select needs at least one correct and one incorrect option");
		}
	}

	std::set<std::string> CheckIds;
	for (const CheckDef& C : M.Checks)
	{
		const std::string CP = "check '" + C.Id + "'";
		if (!ValidId(C.Id) || !CheckIds.insert(C.Id).second || QuestionIds.count(C.Id)) Err(CP + ": id missing or duplicated");
		if (C.Label.empty() || C.Pass.empty() || C.Fail.empty()) Err(CP + ": needs a label, pass and fail feedback");
		if (!M.FindAction(C.Action)) Err(CP + ": unknown action '" + C.Action + "'");
		if (C.Kind == ECheckKind::Before && !M.FindAction(C.Other)) Err(CP + ": unknown other action '" + C.Other + "'");
		if (C.Kind == ECheckKind::ByMinute && (C.Minute <= M.StartMinute && C.Minute < 1440)) Err(CP + ": byMinute must be after the start");
		if (C.Weight <= 0) Err(CP + ": weight must be positive");
		WeightSum += C.Weight;
		Dimensions.insert(static_cast<int>(C.Dimension));
	}
	if (M.Checks.empty()) Err("no fieldwork checks: the rubric must look at what was done on foot");
	if (WeightSum != 100) Err("question and check weights sum to " + std::to_string(WeightSum) + ", not 100");
	if (Dimensions.size() < 4) Err("the rubric must assess at least four dimensions");

	// The expert's walk: possible, complete and perfect.
	std::vector<FieldStep> Steps;
	std::string SimError;
	if (M.BestOrder.empty()) Err("missing bestOrder");
	else if (!SimulateSteps(M, M.BestOrder, Steps, SimError)) Err(SimError);
	else
	{
		for (const ActionDef& A : M.Actions)
		{
			if (A.bRequired && StepIndex(Steps, A.Id) < 0) Err("bestOrder skips required action '" + A.Id + "'");
		}
		for (const FieldStep& S : Steps)
		{
			if (S.bExpired) Err("bestOrder does '" + S.ActionId + "' after it expires");
		}
		for (const CheckDef& C : M.Checks)
		{
			if (EvaluateCheck(C, Steps) != 100) Err("bestOrder fails check '" + C.Id + "'");
		}
		for (const QuestionDef& Q : M.Questions)
		{
			if (Q.At.empty()) continue;
			bool bReachable = false;
			for (size_t i = 0; i < Steps.size() && !bReachable; ++i)
			{
				const ActionDef* A = M.FindAction(Steps[i].ActionId);
				if (!A || A->At != Q.At) continue;
				bool bReq = true;
				for (const std::string& R : Q.Requires) bReq = bReq && StepIndex(std::vector<FieldStep>(Steps.begin(), Steps.begin() + static_cast<long>(i) + 1), R) >= 0;
				bReachable = bReq;
			}
			if (!bReachable) Err("question '" + Q.Id + "' cannot be asked during bestOrder");
		}
	}
	return Out;
}

std::vector<std::string> ValidateCatalogue(const MissionCatalogue& C)
{
	std::vector<std::string> Out;
	std::set<std::string> Ids;
	for (const MissionDef& M : C.Missions)
	{
		if (!Ids.insert(M.Id).second) Out.push_back(M.Id + ": duplicate mission id");
	}
	for (const MissionDef& M : C.Missions)
	{
		for (const std::string& E : ValidateMission(M)) Out.push_back(E);
		for (const std::string& R : M.RequiresPassed)
		{
			const MissionDef* Req = C.Find(R);
			if (!Req) Out.push_back(M.Id + ": requires unknown mission '" + R + "'");
			else if (Req->DeadlineWeeks > 0) Out.push_back(M.Id + ": requires " + R + ", which can escalate and close");
		}
	}
	// Every mission the ladder needs exists and is not optional.
	for (const CareerLevelDef& L : CareerLadder())
	{
		for (const std::string& R : L.RequiredMissions)
		{
			const MissionDef* M = C.Find(R);
			if (M && M->bOptional) Out.push_back(R + ": required for promotion, so it cannot be optional");
			if (M && M->MinLevel > L.Level) Out.push_back(R + ": required at level " + std::to_string(L.Level) + " but opens at " + std::to_string(M->MinLevel));
		}
	}
	return Out;
}

// ---------------------------------------------------------------- availability

static MissionAvailability CheckNew(const GameState& State, const MissionDef& M)
{
	MissionAvailability A;
	if (State.Challenge.Mode != EGameMode::Career)
	{
		A.bAvailable = ChallengeUnlocks(State, M.Id);
		if (!A.bAvailable) A.Reasons.push_back(State.Challenge.bOver ? "The challenge is over" : "Not the current case in this challenge");
		return A;
	}
	if (State.Career.HasPassed(M.Id)) A.Reasons.push_back("Already closed");
	if (State.Career.Level < M.MinLevel) A.Reasons.push_back("Requires " + LevelDef(M.MinLevel).Title + " (level " + std::to_string(M.MinLevel) + ")");
	for (const std::string& R : M.RequiresPassed)
	{
		if (!State.Career.HasPassed(R)) A.Reasons.push_back("Close " + R + " first");
	}
	for (const std::string& F : M.RequiresFlags)
	{
		if (!State.Property.HasFlag(F))
		{
			A.Reasons.push_back("Not triggered yet");
			break;
		}
	}
	for (const std::string& F : M.ExcludedByFlags)
	{
		if (State.Property.HasFlag(F))
		{
			A.Reasons.push_back("No longer relevant");
			break;
		}
	}
	if (M.DeadlineWeeks > 0 && State.Property.HasFlag(M.Id + "_escalated")) A.Reasons.push_back("Escalated without you");
	if (static_cast<int>(State.World.Clues.size()) < M.RequiresClues)
	{
		A.Reasons.push_back("Needs more evidence (" + std::to_string(State.World.Clues.size()) + " of " + std::to_string(M.RequiresClues) + " clues)");
	}
	A.bAvailable = A.Reasons.empty();
	return A;
}

bool IsCampaignStuck(const GameState& State, const MissionCatalogue& C)
{
	if (State.Career.Level >= TopLevel) return false;
	if (State.Career.CheckPromotion().bEligible) return false;
	for (const MissionDef& M : C.Missions)
	{
		if (CheckNew(State, M).bAvailable) return false;
	}
	return true;
}

MissionAvailability CheckAvailability(const GameState& State, const MissionDef& M, const MissionCatalogue* Catalogue)
{
	MissionAvailability A = CheckNew(State, M);
	// A passed case at the player's level reopens as practice when nothing else can move them on.
	if (!A.bAvailable && Catalogue && State.Career.HasPassed(M.Id) && M.Kind == EMissionKind::Mission && M.MinLevel == State.Career.Level &&
		IsCampaignStuck(State, *Catalogue))
	{
		A.bAvailable = true;
		A.bReview = true;
		A.Reasons.clear();
	}
	return A;
}

std::vector<const MissionDef*> AvailableMissions(const GameState& State, const MissionCatalogue& C)
{
	std::vector<const MissionDef*> Out;
	for (const MissionDef& M : C.Missions)
	{
		if (CheckAvailability(State, M, &C).bAvailable) Out.push_back(&M);
	}
	return Out;
}

std::vector<int> OptionDisplayOrder(uint64_t GameSeed, const std::string& MissionId, const std::string& QuestionId, int OptionCount)
{
	uint64_t H = 1469598103934665603ULL;
	for (const std::string* S : {&MissionId, &QuestionId})
	{
		for (unsigned char C : *S)
		{
			H ^= C;
			H *= 1099511628211ULL;
		}
		H ^= 0xFF;
		H *= 1099511628211ULL;
	}
	Rng R(GameSeed ^ H);
	std::vector<int> Order;
	for (int i = 0; i < OptionCount; ++i) Order.push_back(i);
	for (int i = OptionCount - 1; i > 0; --i)
	{
		const int j = R.UniformInt(0, i);
		std::swap(Order[static_cast<size_t>(i)], Order[static_cast<size_t>(j)]);
	}
	return Order;
}

std::vector<std::string> BriefingFor(const GameState& State, const MissionDef& M)
{
	std::vector<std::string> Out = {M.Briefing};
	for (const ContextNoteDef& C : M.Context)
	{
		bool bApplies = true;
		if (!C.Flag.empty()) bApplies = State.Property.HasFlag(C.Flag) != C.bWhenAbsent;
		if (bApplies && C.bUseMetric)
		{
			const double V = State.Property.Get(C.Metric);
			bApplies = C.bBelow ? V < C.Threshold : V >= C.Threshold;
		}
		if (bApplies && !C.RelationId.empty())
		{
			const int V = GetRelation(State, C.RelationId);
			bApplies = C.bRelationBelow ? V < C.RelationThreshold : V >= C.RelationThreshold;
		}
		if (bApplies && C.MinClues > 0) bApplies = static_cast<int>(State.World.Clues.size()) >= C.MinClues;
		if (bApplies) Out.push_back(C.Text);
	}
	const std::string Cause = CrisisCause(State, M);
	if (!Cause.empty()) Out.push_back(Cause);
	return Out;
}

std::string CrisisCause(const GameState& State, const MissionDef& M)
{
	for (const std::string& F : M.RequiresFlags)
	{
		for (auto It = State.Property.History.rbegin(); It != State.Property.History.rend(); ++It)
		{
			if (!It->bOccurred || It->FlagIfOccurs != F || It->Cause.empty()) continue;
			return "This traces back to " + It->Cause + ". In week " + std::to_string(It->Week) + ": " + It->Description;
		}
	}
	return std::string();
}

int WeeksUntilEscalation(const GameState& State, const MissionDef& M)
{
	if (M.DeadlineWeeks <= 0) return -1;
	for (const auto& O : State.World.CrisisOpened)
	{
		if (O.first == M.Id) return std::max(0, O.second + M.DeadlineWeeks + ExtraCrisisWeeks(State) - State.Property.Week);
	}
	return -1;
}

// ---------------------------------------------------------------- play

static void ResetField(CampaignState& C)
{
	C.Steps.clear();
	C.FieldAnswers.clear();
	C.Custody.clear();
	C.SeenActions.clear();
	C.Location.clear();
	C.Clock = 0;
}

static void BeginField(GameState& State, const MissionDef& M, bool bWhatIf)
{
	CampaignState& C = State.Campaign;
	ResetField(C);
	C.ActiveMissionId = M.Id;
	C.bWhatIf = bWhatIf;
	C.Clock = M.StartMinute;
	C.Location = M.Opening.At;
	State.Checkpoint = M.Id + (bWhatIf ? ":whatif" : ":start");
}

EMissionError StartMission(GameState& State, const MissionDef& M, const MissionCatalogue* Catalogue)
{
	CampaignState& C = State.Campaign;
	if (C.ActiveMissionId == M.Id && !C.bWhatIf) return EMissionError::None; // resume
	if (!C.ActiveMissionId.empty()) return EMissionError::AnotherMissionActive;
	if (!CheckAvailability(State, M, Catalogue).bAvailable) return EMissionError::Unavailable;
	BeginField(State, M, false);
	return EMissionError::None;
}

EMissionError StartWhatIf(GameState& State, const MissionDef& M)
{
	CampaignState& C = State.Campaign;
	if (C.ActiveMissionId == M.Id && C.bWhatIf) return EMissionError::None;
	if (!C.ActiveMissionId.empty()) return EMissionError::AnotherMissionActive;
	const bool bSubmitted = std::any_of(C.Records.begin(), C.Records.end(), [&](const MissionRecord& R) { return R.MissionId == M.Id && !R.bWhatIf; });
	if (!bSubmitted) return EMissionError::NotSubmitted;
	BeginField(State, M, true);
	return EMissionError::None;
}

EMissionError AbandonMission(GameState& State)
{
	if (State.Campaign.ActiveMissionId.empty()) return EMissionError::NotActive;
	State.Campaign.ActiveMissionId.clear();
	State.Campaign.bWhatIf = false;
	ResetField(State.Campaign);
	return EMissionError::None;
}

bool IsActionDone(const GameState& State, const std::string& ActionId)
{
	return StepIndex(State.Campaign.Steps, ActionId) >= 0;
}

static bool RequiresMet(const GameState& State, const std::vector<std::string>& Requires)
{
	for (const std::string& R : Requires)
	{
		if (!IsActionDone(State, R)) return false;
	}
	return true;
}

// A conversation that depends on a step done too late (the witness had gone) cannot happen.
static bool RequiresExpired(const GameState& State, const std::vector<std::string>& Requires)
{
	for (const FieldStep& S : State.Campaign.Steps)
	{
		if (S.bExpired && Contains(Requires, S.ActionId)) return true;
	}
	return false;
}

std::vector<const ActionDef*> ActionsAt(const GameState& State, const MissionDef& M, const std::string& Where)
{
	std::vector<const ActionDef*> Out;
	if (State.Campaign.ActiveMissionId != M.Id) return Out;
	for (const ActionDef& A : M.Actions)
	{
		if (A.At == Where && !IsActionDone(State, A.Id) && RequiresMet(State, A.Requires)) Out.push_back(&A);
	}
	return Out;
}

EMissionError DoAction(GameState& State, const MissionDef& M, const std::string& ActionId, const std::string& Where)
{
	CampaignState& C = State.Campaign;
	if (C.ActiveMissionId != M.Id) return EMissionError::NotActive;
	const ActionDef* A = M.FindAction(ActionId);
	if (!A) return EMissionError::UnknownAction;
	if (IsActionDone(State, ActionId)) return EMissionError::AlreadyDone;
	if (A->At != Where) return EMissionError::WrongPlace;
	if (!RequiresMet(State, A->Requires)) return EMissionError::NeedsEarlierStep;
	C.Clock += TravelMinutes(C.Location, A->At) + std::max(0, A->Minutes);
	C.Location = A->At;
	FieldStep S{A->Id, C.Clock, A->ExpiresAt > 0 && C.Clock > A->ExpiresAt};
	C.Steps.push_back(S);
	if (!A->Evidence.empty() && !A->Custody.empty()) C.Custody.push_back({A->Evidence, C.Clock, A->Custody});
	State.Checkpoint = M.Id + ":" + A->Id;
	return EMissionError::None;
}

std::string FindingFor(const GameState& State, const ActionDef& A)
{
	for (const FieldStep& S : State.Campaign.Steps)
	{
		if (S.ActionId == A.Id) return S.bExpired ? A.ExpiredFinding : A.Finding;
	}
	return std::string();
}

std::vector<const QuestionDef*> QuestionsAt(const GameState& State, const MissionDef& M, const std::string& Where)
{
	std::vector<const QuestionDef*> Out;
	if (State.Campaign.ActiveMissionId != M.Id) return Out;
	for (const QuestionDef& Q : M.Questions)
	{
		if (Q.At.empty() || Q.At != Where || FindAnswer(State.Campaign.FieldAnswers, Q.Id)) continue;
		if (RequiresMet(State, Q.Requires) && !RequiresExpired(State, Q.Requires)) Out.push_back(&Q);
	}
	return Out;
}

static EMissionError CheckAnswerShape(const QuestionDef& Q, const MissionAnswer& A)
{
	if (A.OptionIds.empty()) return EMissionError::UnansweredQuestion;
	switch (Q.Kind)
	{
	case EQuestionKind::Numeric:
	{
		double V = 0.0;
		if (A.OptionIds.size() != 1 || !ParseNumericAnswer(A.OptionIds[0], V)) return EMissionError::UnknownOption;
		return EMissionError::None;
	}
	case EQuestionKind::Single:
	case EQuestionKind::Multi:
	case EQuestionKind::Order:
	{
		std::set<std::string> Unique;
		for (const std::string& O : A.OptionIds)
		{
			if (!FindOption(Q, O) || !Unique.insert(O).second) return EMissionError::UnknownOption;
		}
		if (Q.Kind == EQuestionKind::Single && A.OptionIds.size() != 1) return EMissionError::SingleNeedsOneOption;
		if (Q.Kind == EQuestionKind::Order && A.OptionIds.size() != Q.Options.size()) return EMissionError::OrderNeedsEveryOption;
		return EMissionError::None;
	}
	}
	return EMissionError::UnknownOption;
}

EMissionError AnswerInPerson(GameState& State, const MissionDef& M, const MissionAnswer& Answer, const std::string& Where)
{
	CampaignState& C = State.Campaign;
	if (C.ActiveMissionId != M.Id) return EMissionError::NotActive;
	const QuestionDef* Q = M.FindQuestion(Answer.QuestionId);
	if (!Q || Q->At.empty()) return EMissionError::UnknownOption;
	if (FindAnswer(C.FieldAnswers, Q->Id)) return EMissionError::AlreadyDone;
	if (Q->At != Where) return EMissionError::WrongPlace;
	if (!RequiresMet(State, Q->Requires) || RequiresExpired(State, Q->Requires)) return EMissionError::NeedsEarlierStep;
	const EMissionError E = CheckAnswerShape(*Q, Answer);
	if (E != EMissionError::None) return E;
	C.FieldAnswers.push_back(Answer);
	return EMissionError::None;
}

std::vector<const QuestionDef*> ReportQuestions(const MissionDef& M)
{
	std::vector<const QuestionDef*> Out;
	for (const QuestionDef& Q : M.Questions)
	{
		if (Q.At.empty()) Out.push_back(&Q);
	}
	return Out;
}

EMissionError ValidateSubmission(const GameState& State, const MissionDef& M, const std::vector<MissionAnswer>& ReportAnswers)
{
	if (State.Campaign.ActiveMissionId != M.Id) return EMissionError::NotActive;
	for (const ActionDef& A : M.Actions)
	{
		if (A.bRequired && !IsActionDone(State, A.Id)) return EMissionError::RequiredStepMissing;
	}
	for (const MissionAnswer& A : ReportAnswers)
	{
		const QuestionDef* Q = M.FindQuestion(A.QuestionId);
		if (!Q) return EMissionError::UnknownOption;
		if (!Q->At.empty()) return EMissionError::AnsweredInPerson;
	}
	for (const QuestionDef& Q : M.Questions)
	{
		// An in-person question never asked counts as a missed chance, not a blocked report.
		if (!Q.At.empty()) continue;
		const MissionAnswer* A = FindAnswer(ReportAnswers, Q.Id);
		if (!A) return EMissionError::UnansweredQuestion;
		const EMissionError E = CheckAnswerShape(Q, *A);
		if (E != EMissionError::None) return E;
	}
	return EMissionError::None;
}

// ---------------------------------------------------------------- scoring

static int ScoreQuestion(const QuestionDef& Q, const MissionAnswer* A, double Authority, MissionCriterion& C)
{
	const std::vector<std::string> Picked = A ? A->OptionIds : std::vector<std::string>();
	if (Picked.empty())
	{
		C.Feedback.push_back(Q.At.empty() ? "Not answered." : "You never had this conversation: the chance was missed.");
		return 0;
	}
	switch (Q.Kind)
	{
	case EQuestionKind::Single:
	{
		const OptionDef* O = FindOption(Q, Picked.front());
		if (!O) return 0;
		int Score = O->Score;
		if (!O->Feedback.empty()) C.Feedback.push_back(O->Feedback);
		if (O->bCritical) C.bCriticalFailed = true;
		if (O->Cost > Authority && !O->bEscalates)
		{
			C.bAuthorityExceeded = true;
			Score = std::min(Score, AuthorityBreachScoreCap);
			C.Feedback.push_back("This commits " + FormatMoney(O->Cost) + ", above your approval authority of " + FormatMoney(Authority) +
				". It needed the approval of someone who holds that authority.");
		}
		return Score;
	}
	case EQuestionKind::Multi:
	{
		int Correct = 0, Hits = 0, False = 0;
		for (const OptionDef& O : Q.Options)
		{
			const bool bPicked = Contains(Picked, O.Id);
			Correct += O.bCorrect ? 1 : 0;
			if (bPicked && O.bCorrect) ++Hits;
			if (bPicked && !O.bCorrect) ++False;
			if (bPicked && O.bCritical) C.bCriticalFailed = true;
			if (bPicked && !O.Feedback.empty()) C.Feedback.push_back(O.Feedback);
			if (!bPicked && O.bCorrect) C.Feedback.push_back("Missed: " + O.Label);
		}
		const int Raw = Correct ? static_cast<int>(std::lround(100.0 * Hits / Correct)) : 0;
		return std::max(0, std::min(100, Raw - MultiFalsePickPenalty * False));
	}
	case EQuestionKind::Numeric:
	{
		double V = 0.0;
		ParseNumericAnswer(Picked.front(), V);
		int Score = lp::ScoreByCloseness(V, Q.Answer, Q.Tolerance);
		C.Feedback.push_back("You entered " + FormatFigure(V) + (Q.Unit.empty() ? std::string() : " " + Q.Unit) + ".");
		for (const NumericMistake& Mk : Q.Mistakes)
		{
			if (Score < lp::CloseScoreExact && std::fabs(V - Mk.Value) <= Q.Tolerance * std::max(std::fabs(Mk.Value), 1.0))
			{
				Score = std::min(Score, 10);
				C.Feedback.push_back(Mk.Feedback);
			}
		}
		C.Feedback.push_back("Answer: " + FormatFigure(Q.Answer) + (Q.Unit.empty() ? std::string() : " " + Q.Unit) + ". " + Q.Explanation);
		return Score;
	}
	case EQuestionKind::Order:
	{
		// Share of pairs in the right relative order.
		const size_t N = Q.Options.size();
		std::vector<size_t> Pos(N, 0);
		for (size_t i = 0; i < Picked.size() && i < N; ++i)
		{
			for (size_t k = 0; k < N; ++k)
			{
				if (Q.Options[k].Id == Picked[i]) Pos[k] = i;
			}
		}
		int Good = 0, Pairs = 0;
		for (size_t a = 0; a < N; ++a)
		{
			for (size_t b = a + 1; b < N; ++b)
			{
				++Pairs;
				Good += Pos[a] < Pos[b] ? 1 : 0;
			}
		}
		std::string Right;
		for (size_t k = 0; k < N; ++k) Right += (k ? " > " : "") + Q.Options[k].Label;
		C.Feedback.push_back("The right order: " + Right + ". " + Q.Explanation);
		return Pairs ? static_cast<int>(std::lround(100.0 * Good / Pairs)) : 0;
	}
	}
	return 0;
}

MissionDebrief ScoreMission(const MissionDef& M, const std::vector<FieldStep>& Steps, const std::vector<MissionAnswer>& Answers, int Level)
{
	MissionDebrief D;
	D.MissionId = M.Id;
	D.Title = M.Title;
	D.WhatHappened = M.Debrief.WhatHappened;
	D.BestPractice = M.Debrief.BestPractice;
	D.Why = M.Debrief.Why;
	D.CommonMistakes = M.Debrief.CommonMistakes;
	const double Authority = LevelDef(Level).DecisionAuthority;
	double Weighted = 0.0;
	double CompSum[CompetencyCount] = {};
	int CompWeight[CompetencyCount] = {};
	double DimSum[RubricCount] = {};
	int DimWeight[RubricCount] = {};
	auto Add = [&](MissionCriterion& C) {
		Weighted += static_cast<double>(C.Score) * C.Weight;
		CompSum[static_cast<int>(C.Competency)] += static_cast<double>(C.Score) * C.Weight;
		CompWeight[static_cast<int>(C.Competency)] += C.Weight;
		DimSum[static_cast<int>(C.Dimension)] += static_cast<double>(C.Score) * C.Weight;
		DimWeight[static_cast<int>(C.Dimension)] += C.Weight;
		if (C.bCriticalFailed) D.bCriticalBreach = true;
		if (C.Score >= 80) D.WellDone.push_back(C.Label);
		D.Criteria.push_back(C);
	};
	for (const CheckDef& Ck : M.Checks)
	{
		MissionCriterion C;
		C.Id = Ck.Id;
		C.Label = Ck.Label;
		C.bCheck = true;
		C.Dimension = Ck.Dimension;
		C.Competency = Ck.Competency;
		C.Weight = Ck.Weight;
		C.Score = EvaluateCheck(Ck, Steps);
		C.Feedback.push_back(C.Score == 100 ? Ck.Pass : Ck.Fail);
		if (C.Score == 40) C.Feedback.push_back(Ck.Kind == ECheckKind::ByMinute ? "You did it, but too late." : "You did it, but in the wrong order.");
		C.bCriticalFailed = Ck.bCritical && C.Score < 100;
		Add(C);
	}
	for (const QuestionDef& Q : M.Questions)
	{
		MissionCriterion C;
		C.Id = Q.Id;
		C.Label = Q.Prompt;
		C.Dimension = Q.Dimension;
		C.Competency = Q.Competency;
		C.Weight = Q.Weight;
		C.Score = ScoreQuestion(Q, FindAnswer(Answers, Q.Id), Authority, C);
		Add(C);
	}
	D.Score = static_cast<int>(std::lround(Weighted / 100.0));
	if (D.bCriticalBreach) D.Score = std::min(D.Score, M.PassMark - 1);
	D.bPassed = D.Score >= M.PassMark;
	for (int i = 0; i < RubricCount; ++i)
	{
		D.DimensionScores[i] = DimWeight[i] > 0 ? static_cast<int>(std::lround(DimSum[i] / DimWeight[i])) : -1;
	}
	for (int i = 0; i < CompetencyCount; ++i)
	{
		if (CompWeight[i] > 0) D.CompetencyEvidence.push_back({static_cast<ECompetency>(i), static_cast<int>(std::lround(CompSum[i] / CompWeight[i]))});
	}
	for (const FieldStep& S : Steps)
	{
		const ActionDef* A = M.FindAction(S.ActionId);
		if (A) D.Timeline.push_back(ClockText(S.Minute) + "  " + A->Label + " (" + LocationName(A->At) + ")" + (S.bExpired ? " - too late" : ""));
	}
	D.XpAwarded = M.Xp * D.Score / 100;
	return D;
}

// ---------------------------------------------------------------- submission

static std::string DecisionCause(const MissionDef& M, int Week, const std::string& Choice)
{
	return "your call in \"" + M.Title + "\" (week " + std::to_string(Week) + "): " + Choice;
}

static void ScheduleFallout(GameState& State, const MissionDef& M, const MissionDebrief& D, const std::vector<MissionAnswer>& Answers,
	const std::string& DecisionId, const WorldContent& World, MissionDebrief& Out)
{
	const FalloutTemplate* T = World.FalloutFor(M.Topic);
	if (!T) return;
	for (const MissionCriterion& Cr : D.Criteria)
	{
		if (Cr.Score >= FalloutScoreThreshold) continue;
		std::string Choice;
		double P = FalloutMaxProbability * (FalloutScoreThreshold - Cr.Score) / FalloutScoreThreshold;
		if (Cr.bCheck)
		{
			Choice = "skipped: " + Cr.Label;
		}
		else
		{
			const QuestionDef* Q = M.FindQuestion(Cr.Id);
			const MissionAnswer* A = Q ? FindAnswer(Answers, Q->Id) : nullptr;
			if (!Q || !A || A->OptionIds.empty()) continue;
			if (Q->Kind == EQuestionKind::Single)
			{
				const OptionDef* O = FindOption(*Q, A->OptionIds.front());
				if (!O || !O->Effects.Schedules.empty()) continue; // it has its own consequence
				Choice = O->Label;
			}
			else if (Q->Kind == EQuestionKind::Multi)
			{
				for (const std::string& Id : A->OptionIds)
				{
					const OptionDef* O = FindOption(*Q, Id);
					if (O && !O->bCorrect && Choice.empty()) Choice = O->Label;
				}
				if (Choice.empty()) continue;
				P = FalloutMultiProbability;
			}
			else
			{
				Choice = "your answer to \"" + Q->Prompt + "\"";
			}
		}
		std::string Text = T->Description;
		const std::pair<std::string, std::string> Subs[] = {{"{mission}", M.Title}, {"{choice}", Choice}};
		for (const auto& Sub : Subs)
		{
			for (size_t i = Text.find(Sub.first); i != std::string::npos; i = Text.find(Sub.first, i + Sub.second.size())) Text.replace(i, Sub.first.size(), Sub.second);
		}
		uint64_t H = 1469598103934665603ULL;
		for (unsigned char Ch : M.Id + "/" + Cr.Id)
		{
			H ^= Ch;
			H *= 1099511628211ULL;
		}
		PendingConsequence C;
		C.Id = "FALLOUT-" + DecisionId + "-" + Cr.Id;
		C.SourceDecisionId = DecisionId;
		C.Cause = DecisionCause(M, State.Property.Week, Choice);
		C.Description = Text;
		C.TriggerWeek = State.Property.Week + 2 + static_cast<int>(H % 5);
		C.Probability = std::min(1.0, P * IncidentProbabilityMultiplier(DifficultyIndex(State.Difficulty)) * FalloutFactor(State));
		C.FlagIfOccurs = "fallout_" + M.Topic;
		C.EffectsIfOccurs = T->Effects;
		State.Property.Schedule(C);
		Out.Fallout.push_back(Text);
	}
}

static void RecordAttempt(GameState& State, const MissionDef& M, const MissionDebrief& D, const std::vector<MissionAnswer>& All, bool bWhatIf)
{
	MissionRecord R;
	R.MissionId = M.Id;
	R.Week = State.Property.Week;
	R.Level = State.Career.Level;
	R.Score = D.Score;
	R.bPassed = D.bPassed;
	R.bWhatIf = bWhatIf;
	for (int i = 0; i < RubricCount; ++i) R.DimensionScores.push_back(D.DimensionScores[i]);
	for (const FieldStep& S : State.Campaign.Steps) R.Steps.push_back(S.ActionId);
	R.Answers = All;
	State.Campaign.Records.push_back(R);
	State.Campaign.ActiveMissionId.clear();
	State.Campaign.bWhatIf = false;
	ResetField(State.Campaign);
}

static void EarnCards(GameState& State, const MissionDef& M, MissionDebrief& D)
{
	for (const std::string& Card : M.ProcedureCards)
	{
		if (!Contains(State.FieldManual, Card))
		{
			State.FieldManual.push_back(Card);
			D.CardsEarned.push_back(Card);
		}
	}
}

EMissionError SubmitMission(GameState& State, const MissionDef& M, const std::vector<MissionAnswer>& ReportAnswers, MissionDebrief* OutDebrief,
	const MissionCatalogue* Catalogue, const WorldContent* World, const std::string& Where)
{
	const EMissionError Err = ValidateSubmission(State, M, ReportAnswers);
	if (Err != EMissionError::None) return Err;
	if (!M.ReportAt.empty() && Where != M.ReportAt) return EMissionError::FileWhereReportsAre;

	std::vector<MissionAnswer> All = State.Campaign.FieldAnswers;
	All.insert(All.end(), ReportAnswers.begin(), ReportAnswers.end());
	const std::vector<FieldStep> Steps = State.Campaign.Steps;
	MissionDebrief D = ScoreMission(M, Steps, All, State.Career.Level);
	for (const MissionRecord& R : State.Campaign.Records)
	{
		if (R.MissionId == M.Id) D.PreviousBest = std::max(D.PreviousBest, R.Score);
	}

	if (State.Campaign.bWhatIf)
	{
		// A replay from the debrief: see the other outcome; the career and the hotel stay as they were.
		D.bWhatIf = true;
		D.XpAwarded = 0;
		EarnCards(State, M, D);
		RecordAttempt(State, M, D, All, true);
		State.Checkpoint = M.Id + ":whatif_debriefed";
		D.Level = State.Career.Level;
		D.PromotionGaps = State.Career.CheckPromotion().UnmetGates;
		if (OutDebrief) *OutDebrief = D;
		return EMissionError::None;
	}

	D.bReview = State.Career.HasPassed(M.Id);
	for (const AssessmentResult& A : D.CompetencyEvidence) State.Career.ApplyAssessment(A);
	State.Career.RecordMission(M.Id, D.bPassed, D.bReview ? 0 : D.XpAwarded);
	EarnCards(State, M, D);
	if (D.bReview)
	{
		D.XpAwarded = 0;
		RecordAttempt(State, M, D, All, false);
		State.Checkpoint = M.Id + ":reviewed";
		D.bPromoted = State.Career.TryPromote();
		if (D.bPromoted)
		{
			State.bCeremonyPending = true;
			State.CeremonyLevel = State.Career.Level;
		}
		D.Level = State.Career.Level;
		D.PromotionGaps = State.Career.CheckPromotion().UnmetGates;
		if (OutDebrief) *OutDebrief = D;
		return EMissionError::None;
	}

	int Attempt = 1;
	for (const MissionRecord& R : State.Campaign.Records) Attempt += (R.MissionId == M.Id && !R.bWhatIf) ? 1 : 0;

	// The hotel's decision: what was done on foot and what was chosen, with their effects.
	DecisionRecord Rec;
	Rec.Id = M.Id + "-" + std::to_string(Attempt);
	Rec.MissionId = M.Id;
	Rec.Summary = M.Title;
	Rec.QualityScore = D.Score;
	const double Authority = LevelDef(State.Career.Level).DecisionAuthority;
	struct Chosen
	{
		const EffectsDef* Effects;
		std::string Label;
	};
	std::vector<Chosen> Picks;
	double Spend = 0.0;
	for (const FieldStep& S : Steps)
	{
		const ActionDef* A = M.FindAction(S.ActionId);
		if (!A) continue;
		Picks.push_back({&A->Effects, A->Label});
		Spend += A->Cost;
	}
	for (const QuestionDef& Q : M.Questions)
	{
		if (Q.Kind != EQuestionKind::Single && Q.Kind != EQuestionKind::Multi) continue;
		const MissionAnswer* A = FindAnswer(All, Q.Id);
		if (!A) continue;
		for (const std::string& Id : A->OptionIds)
		{
			const OptionDef* O = FindOption(Q, Id);
			if (!O) continue;
			Picks.push_back({&O->Effects, O->Label});
			if (Q.Kind == EQuestionKind::Single) Rec.Summary += "; " + O->Label;
			// Escalated spend is approved and paid above the LP budget line; the rest is LP's.
			if (O->Cost > 0.0 && (!O->bEscalates || O->Cost <= Authority)) Spend += O->Cost;
		}
	}
	for (const Chosen& C : Picks)
	{
		Rec.Effects.insert(Rec.Effects.end(), C.Effects->Metrics.begin(), C.Effects->Metrics.end());
	}
	if (Spend > 0.0) Rec.Effects.push_back({EPropertyMetric::LpBudget, -Spend});
	State.Property.RecordDecision(Rec);
	for (const Chosen& C : Picks)
	{
		EffectsDef NoMetrics = *C.Effects;
		NoMetrics.Metrics.clear(); // already applied through the decision record
		ApplyEffects(State, NoMetrics, M.Id, Rec.Id, DecisionCause(M, State.Property.Week, C.Label));
	}
	State.Property.SetFlag(M.Id + (D.bPassed ? "_passed" : "_failed"));
	ChangeStress(State, M.Kind == EMissionKind::Crisis ? StressPerCrisis : (M.Kind == EMissionKind::Capstone ? StressPerCapstone : StressPerMission));
	if (State.Career.Level <= 3) ChangeStress(State, StressPerNightShift);
	if (World) ScheduleFallout(State, M, D, All, Rec.Id, *World, D);

	RecordAttempt(State, M, D, All, false);
	if (!M.Lesson.empty()) State.LearningHistory.push_back(M.Id + ": " + M.Lesson);
	State.Checkpoint = M.Id + ":debriefed";

	D.Resolved = AdvanceGameWeeks(State, M.Weeks);
	if (World)
	{
		const WorldUpdate U = UpdateWorld(State, Catalogue, *World, D.Resolved, M.Weeks);
		D.NewMessages = U.NewMessages;
		D.Escalated = U.Escalated;
		D.bBurnout = U.bBurnout;
	}

	if (State.Challenge.Mode != EGameMode::Career)
	{
		AdvanceChallenge(State, D);
		D.Level = State.Career.Level;
		if (OutDebrief) *OutDebrief = D;
		return EMissionError::None;
	}
	// Promotion review: competence evidence and passed missions only (Docs/CareerModel.md).
	D.bPromoted = State.Career.TryPromote();
	if (D.bPromoted)
	{
		State.bCeremonyPending = true;
		State.CeremonyLevel = State.Career.Level;
	}
	D.Level = State.Career.Level;
	D.PromotionGaps = State.Career.CheckPromotion().UnmetGates;
	if (OutDebrief) *OutDebrief = D;
	return EMissionError::None;
}

bool PlayBestFieldwork(GameState& State, const MissionDef& M, std::vector<MissionAnswer>& OutReportAnswers)
{
	if (State.Campaign.ActiveMissionId != M.Id) return false;
	auto AskHere = [&](const std::string& Where) {
		for (const QuestionDef* Q : QuestionsAt(State, M, Where))
		{
			if (AnswerInPerson(State, M, BestAnswer(*Q), Where) != EMissionError::None) return false;
		}
		return true;
	};
	if (!AskHere(State.Campaign.Location)) return false;
	for (const std::string& Id : M.BestOrder)
	{
		const ActionDef* A = M.FindAction(Id);
		if (!A || DoAction(State, M, Id, A->At) != EMissionError::None) return false;
		if (!AskHere(A->At)) return false;
	}
	OutReportAnswers.clear();
	for (const QuestionDef* Q : ReportQuestions(M)) OutReportAnswers.push_back(BestAnswer(*Q));
	return true;
}
} // namespace kc
