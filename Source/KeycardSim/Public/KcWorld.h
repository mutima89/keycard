// The living world around the career: relationships, the player's stress from night shifts and
// crises, the story investigation (clues), the inbox (requests, favours and reply deadlines),
// fallout from weak decisions, crisis escalation and news.
// Content is data: Data/People.json, Data/Story.json, Data/Events.json (Docs/WorldData.md).
// Deterministic: every draw uses the property's seeded event stream.
#pragma once

#include "KcApi.h"

#include "KcMission.h"
#include "KcPeople.h"
#include "KcProperty.h"

#include <string>
#include <utility>
#include <vector>

namespace kc
{
struct GameState;

// ---------------------------------------------------------------- content

struct ClueDef
{
	std::string Id;
	std::string Title;
	std::string Summary; // shown on the investigation case board
	int Chapter = 1;     // story chapter (1..4)
};

struct StoryCatalogue
{
	std::string Title;
	std::vector<ClueDef> Clues;
	KC_API const ClueDef* Find(const std::string& Id) const;
};

// Every listed part must hold.
struct WorldCondition
{
	struct MetricTest
	{
		EPropertyMetric Metric = EPropertyMetric::Reputation;
		bool bBelow = true;
		double Threshold = 0.0;
	};
	struct RelationTest
	{
		std::string CharacterId;
		bool bBelow = true;
		int Threshold = 0;
	};
	std::vector<std::string> Flags;
	std::vector<std::string> NotFlags;
	std::vector<std::string> Passed;
	int MinWeek = 0;
	int MinLevel = 0;
	int MaxLevel = 0; // 0: no upper limit
	int MinClues = 0;
	int MinStress = 0;
	int MinIncidents = 0;
	std::vector<MetricTest> Metrics;
	std::vector<RelationTest> Relations;
};

enum class EMessageCategory
{
	Story,
	Incident,
	Request,   // a colleague needs something (favours)
	Personal,
	Politics,  // revenue and guest satisfaction vs safety and cost
	Team,
	News,
	Count
};

KC_API const char* ToString(EMessageCategory C);
KC_API bool MessageCategoryFromName(const std::string& Name, EMessageCategory& Out);

struct EventChoiceDef
{
	std::string Id;
	std::string Label;
	std::string Outcome;
	EffectsDef Effects;
	std::string RequiresSkill;
};

struct EventDef
{
	std::string Id;
	EMessageCategory Category = EMessageCategory::Team;
	std::string From;
	std::string Subject;
	std::string Body;
	WorldCondition When;
	double Probability = 1.0;  // chance per elapsed week once the condition holds
	int DeadlineWeeks = 0;     // 0: no reply needed
	std::string DefaultChoice; // applied when the deadline passes
	std::vector<EventChoiceDef> Choices;
	std::string Clue;
	std::string Headline;
	int HeadlineTone = 0;
};

struct EventCatalogue
{
	std::vector<EventDef> Events;
	KC_API const EventDef* Find(const std::string& Id) const;
};

// Incident text for latent risk left by weak answers, by mission topic.
struct FalloutTemplate
{
	std::string Topic;       // matches MissionDef::Topic; "default" is the fallback
	std::string Description; // "{mission}" and "{choice}" are replaced
	std::vector<MetricEffect> Effects;
};

struct WorldContent
{
	PeopleCatalogue People;
	StoryCatalogue Story;
	EventCatalogue Events;
	std::vector<FalloutTemplate> Fallout;
	KC_API const FalloutTemplate* FalloutFor(const std::string& Topic) const;
};

// ---------------------------------------------------------------- player state

struct InboxMessage
{
	std::string Id; // "MSG-<n>"
	std::string EventId;
	EMessageCategory Category = EMessageCategory::Team;
	std::string From;
	std::string Subject;
	std::string Body;
	int Week = 0;
	int DeadlineWeek = -1;
	bool bRead = false;
	std::string ChoiceId;
	std::string Outcome;
	bool bAutoResolved = false;
	bool NeedsReply() const { return DeadlineWeek >= 0 && ChoiceId.empty(); }
};

struct NewsItem
{
	int Week = 0;
	std::string Text;
	int Tone = 0;
};

struct WorldState
{
	std::vector<Relation> Relations;
	int Stress = 20;
	int Burnouts = 0;
	std::vector<std::string> Clues;
	std::vector<InboxMessage> Inbox;
	std::vector<std::string> FiredEvents;
	std::vector<NewsItem> News;
	std::vector<std::pair<std::string, int>> CrisisOpened; // crisis id -> week it opened
	int NextMessage = 1;
	std::vector<std::string> ConversationsHad;
};

constexpr int StressMax = 100;
constexpr int StressStart = 20;
constexpr int StressPerMission = 4;
constexpr int StressPerNightShift = 2;   // levels 1-3 work nights
constexpr int StressPerCrisis = 12;
constexpr int StressPerCapstone = 8;
constexpr int StressRecoveryPerWeek = 3;
constexpr int StressBurnout = 90;
constexpr int BurnoutLeaveWeeks = 2;
constexpr int StressAfterLeave = 40;
constexpr size_t NewsCap = 40;

// ---------------------------------------------------------------- rules

KC_API void InitWorld(GameState& State, const WorldContent& Content);
KC_API int GetRelation(const GameState& State, const std::string& CharacterId);
KC_API void ChangeRelation(GameState& State, const std::string& CharacterId, int Delta);
KC_API void ChangeStress(GameState& State, int Delta);
KC_API bool HasClue(const GameState& State, const std::string& ClueId);
KC_API bool AddClue(GameState& State, const std::string& ClueId);
KC_API int IncidentsOccurred(const GameState& State);
KC_API bool ConditionHolds(const GameState& State, const WorldCondition& C);
KC_API bool HasSkill(const GameState& State, const std::string& SkillId);

// Applies effects of a choice (mission action or option, or inbox reply) now. Scheduled
// consequences carry Cause (the decision in words) so a later incident can name it.
KC_API void ApplyEffects(GameState& State, const EffectsDef& E, const std::string& IdPrefix, const std::string& SourceId, const std::string& Cause);

KC_API std::string PostMessage(GameState& State, InboxMessage M);
KC_API void AddNews(GameState& State, const std::string& Text, int Tone);
KC_API int UnreadCount(const GameState& State);
KC_API int AwaitingReplyCount(const GameState& State);
KC_API const InboxMessage* FindMessage(const GameState& State, const std::string& Id);
KC_API bool MarkMessageRead(GameState& State, const std::string& Id);

enum class EReplyError
{
	None,
	UnknownMessage,
	NoReplyNeeded,
	AlreadyAnswered,
	UnknownChoice,
	NeedsSkill
};

KC_API const char* ToString(EReplyError E);
KC_API EReplyError ReplyToMessage(GameState& State, const WorldContent& Content, const std::string& MessageId, const std::string& ChoiceId);

struct WorldUpdate
{
	std::vector<std::string> NewMessages;
	std::vector<std::string> AutoResolved;
	std::vector<std::string> Escalated;
	bool bBurnout = false;
};

KC_API WorldUpdate UpdateWorld(GameState& State, const MissionCatalogue* Missions, const WorldContent& Content,
	const std::vector<ResolvedConsequence>& Resolved, int WeeksElapsed);

// Content validation: every problem as "<file>: <message>". Empty means valid.
KC_API std::vector<std::string> ValidateWorld(const WorldContent& Content, const MissionCatalogue* Missions);

KC_API bool PeopleFromJson(const std::string& Text, const std::string& SourceName, PeopleCatalogue& Out, std::vector<std::string>& OutErrors);
KC_API bool StoryFromJson(const std::string& Text, const std::string& SourceName, StoryCatalogue& Out, std::vector<std::string>& OutErrors);
KC_API bool EventsFromJson(const std::string& Text, const std::string& SourceName, EventCatalogue& OutEvents,
	std::vector<FalloutTemplate>& OutFallout, std::vector<std::string>& OutErrors);
} // namespace kc
