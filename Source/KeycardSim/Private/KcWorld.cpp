#include "KcWorld.h"

#include "KcGameState.h"
#include "KcLearning.h"

#include <algorithm>
#include <cmath>

namespace kc
{
const char* RelationLabel(int Value)
{
	if (Value >= RelationAlly) return "Ally";
	if (Value >= RelationFriendly) return "Friendly";
	if (Value > RelationWary) return "Neutral";
	if (Value > RelationHostile) return "Wary";
	return "Hostile";
}

const CharacterDef* PeopleCatalogue::Find(const std::string& Id) const
{
	for (const CharacterDef& C : People)
	{
		if (C.Id == Id) return &C;
	}
	return nullptr;
}

const ClueDef* StoryCatalogue::Find(const std::string& Id) const
{
	for (const ClueDef& C : Clues)
	{
		if (C.Id == Id) return &C;
	}
	return nullptr;
}

const EventDef* EventCatalogue::Find(const std::string& Id) const
{
	for (const EventDef& E : Events)
	{
		if (E.Id == Id) return &E;
	}
	return nullptr;
}

const FalloutTemplate* WorldContent::FalloutFor(const std::string& Topic) const
{
	const FalloutTemplate* Default = nullptr;
	for (const FalloutTemplate& T : Fallout)
	{
		if (T.Topic == Topic) return &T;
		if (T.Topic == "default") Default = &T;
	}
	return Default;
}

static const char* const CategoryNames[static_cast<int>(EMessageCategory::Count)] = {"story", "incident", "request", "personal", "politics", "team", "news"};

const char* ToString(EMessageCategory C)
{
	switch (C)
	{
	case EMessageCategory::Story: return "Story";
	case EMessageCategory::Incident: return "Incident";
	case EMessageCategory::Request: return "Request";
	case EMessageCategory::Personal: return "Personal";
	case EMessageCategory::Politics: return "Politics";
	case EMessageCategory::Team: return "Team";
	case EMessageCategory::News: return "News";
	case EMessageCategory::Count: break;
	}
	return "?";
}

bool MessageCategoryFromName(const std::string& Name, EMessageCategory& Out)
{
	for (int i = 0; i < static_cast<int>(EMessageCategory::Count); ++i)
	{
		if (Name == CategoryNames[i])
		{
			Out = static_cast<EMessageCategory>(i);
			return true;
		}
	}
	return false;
}

const char* ToString(EReplyError E)
{
	switch (E)
	{
	case EReplyError::None: return "None";
	case EReplyError::UnknownMessage: return "Unknown message.";
	case EReplyError::NoReplyNeeded: return "This message does not need a reply.";
	case EReplyError::AlreadyAnswered: return "You have already replied.";
	case EReplyError::UnknownChoice: return "Unknown reply.";
	case EReplyError::NeedsSkill: return "That reply needs a skill you do not have yet.";
	}
	return "?";
}

// ---------------------------------------------------------------- relationships, stress, clues

static Relation* FindRelation(WorldState& W, const std::string& Id)
{
	for (Relation& R : W.Relations)
	{
		if (R.CharacterId == Id) return &R;
	}
	return nullptr;
}

void InitWorld(GameState& State, const WorldContent& Content)
{
	for (const CharacterDef& C : Content.People.People)
	{
		if (!FindRelation(State.World, C.Id)) State.World.Relations.push_back({C.Id, std::max(RelationMin, std::min(RelationMax, C.StartRelation))});
	}
}

int GetRelation(const GameState& State, const std::string& CharacterId)
{
	for (const Relation& R : State.World.Relations)
	{
		if (R.CharacterId == CharacterId) return R.Value;
	}
	return 0;
}

void ChangeRelation(GameState& State, const std::string& CharacterId, int Delta)
{
	if (CharacterId.empty()) return;
	Relation* R = FindRelation(State.World, CharacterId);
	if (!R)
	{
		State.World.Relations.push_back({CharacterId, 0});
		R = &State.World.Relations.back();
	}
	const double Scaled = Delta * (Delta > 0 ? RelationGainFactor(State) : RelationLossFactor(State));
	R->Value = std::max(RelationMin, std::min(RelationMax, R->Value + static_cast<int>(std::lround(Scaled))));
}

void ChangeStress(GameState& State, int Delta)
{
	if (Delta > 0) Delta = static_cast<int>(std::lround(Delta * StressGainFactor(State)));
	State.World.Stress = std::max(0, std::min(StressMax, State.World.Stress + Delta));
}

bool HasClue(const GameState& State, const std::string& ClueId)
{
	return std::find(State.World.Clues.begin(), State.World.Clues.end(), ClueId) != State.World.Clues.end();
}

bool AddClue(GameState& State, const std::string& ClueId)
{
	if (ClueId.empty() || HasClue(State, ClueId)) return false;
	State.World.Clues.push_back(ClueId);
	return true;
}

bool HasSkill(const GameState& State, const std::string& SkillId)
{
	return std::find(State.Career.Skills.begin(), State.Career.Skills.end(), SkillId) != State.Career.Skills.end();
}

int IncidentsOccurred(const GameState& State)
{
	int N = 0;
	for (const ResolvedConsequence& R : State.Property.History) N += R.bOccurred ? 1 : 0;
	return N;
}

bool ConditionHolds(const GameState& State, const WorldCondition& C)
{
	for (const std::string& F : C.Flags)
	{
		if (!State.Property.HasFlag(F)) return false;
	}
	for (const std::string& F : C.NotFlags)
	{
		if (State.Property.HasFlag(F)) return false;
	}
	for (const std::string& P : C.Passed)
	{
		if (!State.Career.HasPassed(P)) return false;
	}
	if (State.Property.Week < C.MinWeek) return false;
	if (State.Career.Level < C.MinLevel) return false;
	if (C.MaxLevel > 0 && State.Career.Level > C.MaxLevel) return false;
	if (static_cast<int>(State.World.Clues.size()) < C.MinClues) return false;
	if (C.MinStress > 0 && State.World.Stress < C.MinStress) return false;
	if (C.MinIncidents > 0 && IncidentsOccurred(State) < C.MinIncidents) return false;
	for (const WorldCondition::MetricTest& T : C.Metrics)
	{
		const double V = State.Property.Get(T.Metric);
		if (T.bBelow ? !(V < T.Threshold) : !(V >= T.Threshold)) return false;
	}
	for (const WorldCondition::RelationTest& T : C.Relations)
	{
		const int V = GetRelation(State, T.CharacterId);
		if (T.bBelow ? !(V < T.Threshold) : !(V >= T.Threshold)) return false;
	}
	return true;
}

void ApplyEffects(GameState& State, const EffectsDef& E, const std::string& IdPrefix, const std::string& SourceId, const std::string& Cause)
{
	State.Property.Apply(E.Metrics);
	for (const RelationEffect& R : E.Relations) ChangeRelation(State, R.CharacterId, R.Delta);
	ChangeStress(State, E.Stress);
	for (const std::string& F : E.SetsFlags) State.Property.SetFlag(F);
	AddClue(State, E.Clue);
	for (const ConsequenceDef& Cq : E.Schedules)
	{
		PendingConsequence P;
		P.Id = IdPrefix + "-" + Cq.Id;
		P.SourceDecisionId = SourceId;
		P.Cause = Cause;
		P.Description = Cq.Description;
		P.TriggerWeek = State.Property.Week + std::max(1, Cq.DelayWeeks);
		P.Probability = std::min(1.0, Cq.Probability * IncidentProbabilityMultiplier(DifficultyIndex(State.Difficulty)) * FalloutFactor(State));
		P.EffectsIfOccurs = Cq.Effects;
		P.MitigatingFlag = Cq.MitigatingFlag;
		P.MitigatedProbabilityFactor = Cq.MitigatedProbabilityFactor;
		P.MitigatedSeverityFactor = Cq.MitigatedSeverityFactor;
		P.FlagIfOccurs = Cq.FlagIfOccurs;
		State.Property.Schedule(P);
	}
}

// ---------------------------------------------------------------- inbox and news

std::string PostMessage(GameState& State, InboxMessage M)
{
	M.Id = "MSG-" + std::to_string(State.World.NextMessage++);
	State.World.Inbox.push_back(M);
	return State.World.Inbox.back().Id;
}

void AddNews(GameState& State, const std::string& Text, int Tone)
{
	if (Text.empty()) return;
	State.World.News.push_back({State.Property.Week, Text, Tone < 0 ? -1 : (Tone > 0 ? 1 : 0)});
	if (State.World.News.size() > NewsCap) State.World.News.erase(State.World.News.begin());
}

int UnreadCount(const GameState& State)
{
	int N = 0;
	for (const InboxMessage& M : State.World.Inbox) N += M.bRead ? 0 : 1;
	return N;
}

int AwaitingReplyCount(const GameState& State)
{
	int N = 0;
	for (const InboxMessage& M : State.World.Inbox) N += M.NeedsReply() ? 1 : 0;
	return N;
}

const InboxMessage* FindMessage(const GameState& State, const std::string& Id)
{
	for (const InboxMessage& M : State.World.Inbox)
	{
		if (M.Id == Id) return &M;
	}
	return nullptr;
}

static InboxMessage* FindMessageMutable(GameState& State, const std::string& Id)
{
	for (InboxMessage& M : State.World.Inbox)
	{
		if (M.Id == Id) return &M;
	}
	return nullptr;
}

bool MarkMessageRead(GameState& State, const std::string& Id)
{
	InboxMessage* M = FindMessageMutable(State, Id);
	if (!M) return false;
	M->bRead = true;
	return true;
}

static void ApplyChoice(GameState& State, InboxMessage& Msg, const EventChoiceDef& C, bool bAuto)
{
	ApplyEffects(State, C.Effects, "EVT-" + Msg.Id, Msg.Id, "your reply to \"" + Msg.Subject + "\" (week " + std::to_string(Msg.Week) + "): " + C.Label);
	Msg.ChoiceId = C.Id;
	Msg.Outcome = bAuto ? "No reply in time. " + C.Outcome : C.Outcome;
	Msg.bAutoResolved = bAuto;
	if (!bAuto) Msg.bRead = true;
}

static const EventChoiceDef* FindChoice(const EventDef& E, const std::string& Id)
{
	for (const EventChoiceDef& C : E.Choices)
	{
		if (C.Id == Id) return &C;
	}
	return nullptr;
}

EReplyError ReplyToMessage(GameState& State, const WorldContent& Content, const std::string& MessageId, const std::string& ChoiceId)
{
	InboxMessage* M = FindMessageMutable(State, MessageId);
	if (!M) return EReplyError::UnknownMessage;
	if (M->DeadlineWeek < 0) return EReplyError::NoReplyNeeded;
	if (!M->ChoiceId.empty()) return EReplyError::AlreadyAnswered;
	const EventDef* E = Content.Events.Find(M->EventId);
	const EventChoiceDef* C = E ? FindChoice(*E, ChoiceId) : nullptr;
	if (!C) return EReplyError::UnknownChoice;
	if (!C->RequiresSkill.empty() && !HasSkill(State, C->RequiresSkill)) return EReplyError::NeedsSkill;
	ApplyChoice(State, *M, *C, false);
	return EReplyError::None;
}

// ---------------------------------------------------------------- world update

static void ReportResolved(GameState& State, const std::vector<ResolvedConsequence>& Resolved, WorldUpdate& U)
{
	for (const ResolvedConsequence& R : Resolved)
	{
		if (!R.bOccurred && !R.bMitigated) continue; // a quiet non-event: players never see luck they did not earn
		InboxMessage Msg;
		Msg.Week = R.Week;
		Msg.From = "lp_desk";
		if (R.bOccurred)
		{
			Msg.Category = EMessageCategory::Incident;
			Msg.Subject = "Incident report";
			Msg.Body = R.Description;
			if (R.bMitigated) Msg.Body += "\n\nA control you put in place reduced the damage.";
			if (!R.Cause.empty()) Msg.Body += "\n\nThe incident report traces this to " + R.Cause + ".";
			AddNews(State, "Veldane: " + R.Description, -1);
		}
		else
		{
			Msg.Category = EMessageCategory::Team;
			Msg.Subject = "Near miss: your control held";
			Msg.Body = "It nearly happened: " + R.Description + "\n\nThe control you put in place stopped it.";
			if (!R.Cause.empty()) Msg.Body += "\n\nThat goes back to " + R.Cause + ".";
		}
		U.NewMessages.push_back(PostMessage(State, Msg));
	}
}

static void ResolveOverdue(GameState& State, const WorldContent& Content, WorldUpdate& U)
{
	for (InboxMessage& M : State.World.Inbox)
	{
		if (!M.NeedsReply() || State.Property.Week <= M.DeadlineWeek) continue;
		const EventDef* E = Content.Events.Find(M.EventId);
		const EventChoiceDef* C = E ? FindChoice(*E, E->DefaultChoice) : nullptr;
		if (!C)
		{
			M.ChoiceId = "-";
			M.Outcome = "No reply in time.";
			M.bAutoResolved = true;
		}
		else
		{
			ApplyChoice(State, M, *C, true);
		}
		U.AutoResolved.push_back(M.Id);
	}
}

static void CheckCrisisDeadlines(GameState& State, const MissionCatalogue& Missions, WorldUpdate& U)
{
	for (const MissionDef& M : Missions.Missions)
	{
		if (M.DeadlineWeeks <= 0 || State.Property.HasFlag(M.Id + "_escalated") || State.Campaign.ActiveMissionId == M.Id) continue;
		const bool bAttempted = std::any_of(State.Campaign.Records.begin(), State.Campaign.Records.end(),
			[&](const MissionRecord& R) { return R.MissionId == M.Id && !R.bWhatIf; });
		if (bAttempted || !CheckAvailability(State, M).bAvailable) continue;
		auto It = std::find_if(State.World.CrisisOpened.begin(), State.World.CrisisOpened.end(),
			[&](const std::pair<std::string, int>& O) { return O.first == M.Id; });
		if (It == State.World.CrisisOpened.end())
		{
			State.World.CrisisOpened.push_back({M.Id, State.Property.Week});
			continue;
		}
		if (State.Property.Week <= It->second + M.DeadlineWeeks + ExtraCrisisWeeks(State)) continue;
		State.Property.Apply(M.EscalationEffects);
		State.Property.SetFlag(M.Id + "_escalated");
		InboxMessage Msg;
		Msg.Category = EMessageCategory::Incident;
		Msg.From = "lp_desk";
		Msg.Week = State.Property.Week;
		Msg.Subject = "Escalated without you: " + M.Title;
		Msg.Body = M.EscalationText.empty() ? "The crisis was left too long and the executive committee stepped in without LP." : M.EscalationText;
		U.NewMessages.push_back(PostMessage(State, Msg));
		AddNews(State, "Veldane criticised over handling of \"" + M.Title + "\"", -1);
		U.Escalated.push_back(M.Id);
	}
}

static void DeliverEvents(GameState& State, const WorldContent& Content, int WeeksElapsed, WorldUpdate& U)
{
	constexpr int MaxPerUpdate = 3; // an inbox that floods is noise, not pressure
	int Delivered = 0;
	for (const EventDef& E : Content.Events.Events)
	{
		if (Delivered >= MaxPerUpdate) break;
		if (std::find(State.World.FiredEvents.begin(), State.World.FiredEvents.end(), E.Id) != State.World.FiredEvents.end()) continue;
		if (!ConditionHolds(State, E.When)) continue;
		if (E.Probability < 1.0)
		{
			const double P = 1.0 - std::pow(1.0 - std::max(0.0, E.Probability), std::max(1, WeeksElapsed));
			if (State.Property.DrawUniform() >= P) continue;
		}
		State.World.FiredEvents.push_back(E.Id);
		InboxMessage Msg;
		Msg.EventId = E.Id;
		Msg.Category = E.Category;
		Msg.From = E.From;
		Msg.Subject = E.Subject;
		Msg.Body = E.Body;
		Msg.Week = State.Property.Week;
		Msg.DeadlineWeek = E.DeadlineWeeks > 0 ? State.Property.Week + E.DeadlineWeeks : -1;
		AddClue(State, E.Clue);
		AddNews(State, E.Headline, E.HeadlineTone);
		U.NewMessages.push_back(PostMessage(State, Msg));
		++Delivered;
	}
}

WorldUpdate UpdateWorld(GameState& State, const MissionCatalogue* Missions, const WorldContent& Content,
	const std::vector<ResolvedConsequence>& Resolved, int WeeksElapsed)
{
	WorldUpdate U;
	InitWorld(State, Content);
	ReportResolved(State, Resolved, U);
	ResolveOverdue(State, Content, U);
	if (Missions) CheckCrisisDeadlines(State, *Missions, U);
	ChangeStress(State, -(StressRecoveryPerWeek + ExtraRecoveryPerWeek(State)) * std::max(0, WeeksElapsed));
	if (State.World.Stress >= StressBurnout)
	{
		U.bBurnout = true;
		++State.World.Burnouts;
		State.Property.SetFlag("burnout");
		InboxMessage Msg;
		Msg.Category = EMessageCategory::Personal;
		Msg.From = "mentor";
		Msg.Week = State.Property.Week;
		Msg.Subject = "Two weeks off. Not a request.";
		Msg.Body = "Night after night, crisis after crisis: you have been running on empty and it shows on the floor. "
			"Take two weeks. The hotel will still be here, and so will the team.\n\nWhile you were away, the hotel carried on without you.";
		U.NewMessages.push_back(PostMessage(State, Msg));
		const std::vector<ResolvedConsequence> During = AdvanceGameWeeks(State, BurnoutLeaveWeeks);
		State.World.Stress = StressAfterLeave;
		ReportResolved(State, During, U);
		ResolveOverdue(State, Content, U);
		if (Missions) CheckCrisisDeadlines(State, *Missions, U);
	}
	DeliverEvents(State, Content, WeeksElapsed, U);
	return U;
}
} // namespace kc
