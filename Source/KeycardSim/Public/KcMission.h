// Missions: LP casework worked on foot, scored against a hidden rubric, ending in a debrief that
// is a mini lesson. Mission content is data (Data/Missions/*.json, format: Docs/MissionData.md);
// the rules live here. Rubric scores stay hidden until SubmitMission().
//
// A mission has three parts:
//  - Fieldwork: actions done in places (walk to the scene, talk to people, interrogate a lock,
//    pull CCTV, bag evidence). Each action takes clock minutes plus travel; its order and timing
//    are scored by checks ("preserve the footage before it overwrites").
//  - In-person questions: choices made face to face during the fieldwork (what you ask, what you say).
//  - The report and decisions: the incident report writer and the calls made at the end.
#pragma once

#include "KcApi.h"

#include "KcCareer.h"
#include "KcPeople.h"
#include "KcProperty.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kc
{
struct GameState;
struct WorldContent;

enum class EMissionKind
{
	Mission,
	Crisis,
	Capstone
};

KC_API const char* ToString(EMissionKind K);

// The seven rubric dimensions (Docs/GameDesign.md "Rubric dimensions").
enum class ERubric
{
	Procedure,     // steps done, in the right order
	Evidence,      // quality, completeness, chain of custody
	Compliance,    // policy and local law
	Care,          // guest and staff welfare, dignity, de-escalation
	Report,        // who, what, when, where, how; facts not opinions
	Communication, // the right people, at the right time, with the right information
	Cost,          // proportionate spend within authority
	Count
};

constexpr int RubricCount = static_cast<int>(ERubric::Count);
KC_API const char* ToString(ERubric R);           // display name
KC_API bool RubricFromName(const std::string& Name, ERubric& Out); // data name ("Procedure")
KC_API const char* RubricName(ERubric R);

// How the case reaches the player, in the world.
enum class EChannel
{
	Radio,
	FrontDesk,
	Phone,
	FirePanel,
	Email,
	Letter,
	InPerson,
	Count
};

KC_API const char* ToString(EChannel C);
KC_API bool ChannelFromName(const std::string& Name, EChannel& Out);

// What an action is, which decides the tool the game opens for it.
enum class EActionKind
{
	Look,          // examine the scene
	Interview,     // talk to a person face to face
	LockAudit,     // interrogate a lock: audit trail table
	CctvReview,    // CCTV review console: camera clips table
	ExportFootage, // clip export with a chain of custody entry
	Photograph,
	BagEvidence,   // seal and label an item: chain of custody entry
	LogEvidence,   // evidence locker or log book entry
	Check,         // a log, a record, a system (lost property, key log, fire panel)
	Call,          // phone or radio someone
	Secure,        // secure a scene, a door, an area
	Search,        // search a person or their belongings (policy and local law decide)
	Detain,        // hold a person (policy and local law decide)
	Enter,         // enter a room
	Other,
	Count
};

KC_API const char* ToString(EActionKind K);
KC_API bool ActionKindFromName(const std::string& Name, EActionKind& Out);

// ---------------------------------------------------------------- places

struct LocationDef
{
	std::string Id;
	std::string Name;
	int Floor = 0;
};

// Places in a Veldane hotel where casework happens in person. Sister properties reuse the kit
// with the same ids. Phase 1 builds lobby, front desk, LP office, CCTV room and floor 14.
KC_API const std::vector<LocationDef>& KnownLocations();
KC_API const LocationDef* FindLocation(const std::string& Id);
KC_API std::string LocationName(const std::string& Id); // "" if unknown
// Named places inside a location where a piece of work is done (the CCTV console, the lock on 1412).
// The hotel builds a prop for each; an action names one with "spot" (Docs/MissionData.md).
struct SpotDef
{
	std::string Id;
	std::string Location;
	std::string Name;
};

KC_API const std::vector<SpotDef>& KnownSpots();
KC_API const SpotDef* FindSpot(const std::string& Id);
// Minutes to walk between two places: 0 in place, 2 on the same floor, 5 by lift or stairs.
KC_API int TravelMinutes(const std::string& From, const std::string& To);
// "02:41" for a clock minute (minutes after midnight, wraps past 24:00).
KC_API std::string ClockText(int Minute);
// "02:41" -> 161. False on anything else.
KC_API bool ParseClock(const std::string& Text, int& OutMinute);

// ---------------------------------------------------------------- content

struct DialogueLine
{
	std::string Speaker; // character id (Data/People.json); "player" for the player's own words
	std::string Text;
};

// A consequence a choice schedules for later (resolved by PropertyState::AdvanceWeeks).
struct ConsequenceDef
{
	std::string Id;
	std::string Description;
	int DelayWeeks = 4;
	double Probability = 0.5;           // before the difficulty multiplier
	std::vector<MetricEffect> Effects;  // if it occurs
	std::string FlagIfOccurs;           // opens follow-up missions and crises
	std::string MitigatingFlag;
	double MitigatedProbabilityFactor = 1.0;
	double MitigatedSeverityFactor = 1.0;
};

// Things every choice (action, option, inbox reply) can do to the world.
struct EffectsDef
{
	std::vector<MetricEffect> Metrics;
	std::vector<RelationEffect> Relations;
	int Stress = 0;
	std::vector<std::string> SetsFlags;
	std::vector<ConsequenceDef> Schedules;
	std::string Clue;
};

struct ActionDef
{
	std::string Id;
	std::string Label;                 // "Interview the guest"
	EActionKind Kind = EActionKind::Look;
	std::string At;                    // location id where it is done
	std::string Person;                // character met (interviews, calls), may be empty
	std::string Spot;                  // where in the location it is done (KnownSpots), may be empty
	int Minutes = 2;                   // clock time it takes
	std::vector<std::string> Requires; // actions that must be done first
	bool bRequired = false;            // must be done before the report can be filed
	// What the player learns (added to the notebook).
	std::string FindingTitle;
	std::string Finding;
	std::vector<DialogueLine> Lines;   // spoken during the action (voiced)
	// Tool view (lock audit trail, CCTV clips, logs): a table.
	std::vector<std::string> Columns;
	std::vector<std::vector<std::string>> Rows;
	// Timing: past this clock minute the action still runs but finds ExpiredFinding instead
	// (footage overwritten, witness gone). 0: never expires.
	int ExpiresAt = 0;
	std::string ExpiredFinding;
	// Chain of custody: the evidence item this action seals or logs, and the log line.
	std::string Evidence;
	std::string Custody;
	double Cost = 0.0;                 // spend the action commits (a locksmith, a callout)
	EffectsDef Effects;
};

enum class EQuestionKind
{
	Single,  // pick one option; the option's Score is the result
	Multi,   // pick all that apply; scored against the options marked bCorrect
	Numeric, // type a figure (worked out with an LP tool); scored by closeness (KcLpMath)
	Order    // put every option in order; scored by how many pairs are in the right order
};

KC_API const char* ToString(EQuestionKind K); // "single", "multi", "numeric", "order"

struct NumericMistake
{
	double Value = 0.0;
	std::string Feedback;
};

struct OptionDef
{
	std::string Id;
	std::string Label;
	std::string Feedback;        // shown in the debrief only
	int Score = 0;               // Single: quality 0..100
	bool bCorrect = false;       // Multi: belongs to the right set
	bool bCritical = false;      // a serious policy or legal breach: the mission cannot pass
	double Cost = 0.0;           // above the player's authority it must be escalated
	bool bEscalates = false;     // routes the decision to someone with the authority
	DialogueLine Reply;          // in-person questions: what the other person says back (voiced)
	EffectsDef Effects;
};

struct QuestionDef
{
	std::string Id;
	std::string Prompt;
	EQuestionKind Kind = EQuestionKind::Single;
	ERubric Dimension = ERubric::Procedure;
	ECompetency Competency = ECompetency::IncidentResponse;
	int Weight = 0;              // percent of the mission (questions and checks sum to 100)
	std::string Section;         // incident report section ("who", "what", "when", "where", "how", "actions", "facts"), or ""
	std::string At;              // asked in person at this location during fieldwork ("" = at submission)
	std::string Person;          // the character it is put to (in-person questions)
	std::vector<std::string> Requires; // actions done before it can be asked in person
	std::vector<OptionDef> Options;    // not Numeric; Order lists them in the right order
	// Numeric
	double Answer = 0.0;               // set from Formula when one is given
	double Tolerance = 0.02;           // relative
	std::string Unit;
	std::vector<NumericMistake> Mistakes;
	std::string Formula;               // KcLpMath formula name (Docs/MissionData.md), "" = Answer as written
	std::vector<std::pair<std::string, double>> FormulaInputs;
	std::string Explanation;           // the worked answer (Numeric and Order), debrief only
};

enum class ECheckKind
{
	Done,     // Action was done
	NotDone,  // Action was not done (a policy breach, a shortcut)
	Before,   // Action was done, and before Other (or Other was never done)
	ByMinute  // Action was done by clock minute Minute
};

KC_API const char* ToString(ECheckKind K);

// A fieldwork rubric line: hidden until submission.
struct CheckDef
{
	std::string Id;
	std::string Label;              // the standard, shown in the debrief ("Preserve footage before it overwrites")
	ECheckKind Kind = ECheckKind::Done;
	std::string Action;
	std::string Other;
	int Minute = 0;
	ERubric Dimension = ERubric::Procedure;
	ECompetency Competency = ECompetency::IncidentResponse;
	int Weight = 0;
	bool bCritical = false;         // failing it means the mission cannot pass
	std::string Pass;               // debrief feedback when met
	std::string Fail;               // debrief feedback when missed
};

struct ContextNoteDef
{
	std::string Flag;
	bool bWhenAbsent = false;
	bool bUseMetric = false;
	EPropertyMetric Metric = EPropertyMetric::GuestSatisfaction;
	bool bBelow = true;
	double Threshold = 0.0;
	std::string RelationId;
	bool bRelationBelow = false;
	int RelationThreshold = 0;
	int MinClues = 0;
	std::string Text;
};

struct OpeningDef
{
	EChannel Channel = EChannel::Radio;
	std::string From;  // character id
	std::string Text;  // the call, the email, the letter (voiced when spoken)
	std::string At;    // where the player is when the case reaches them
};

struct DebriefDef
{
	std::string WhatHappened;               // the truth of the case
	std::vector<std::string> BestPractice;  // what a strong LP professional does, in order
	std::string Why;                        // the risk, the liability, the person
	std::vector<std::string> CommonMistakes;
};

struct MissionDef
{
	std::string Id;
	std::string Title;
	EMissionKind Kind = EMissionKind::Mission;
	std::string Topic;                 // "theft", "fire", "keys", "cctv", "claims", "investigation", "audit", "crisis", ...
	std::string Property = "harbourfront";
	int MinLevel = 1;
	bool bOptional = false;            // never required for promotion
	bool bInspiredByRealEvents = false;
	std::vector<std::string> RequiresPassed;
	std::vector<std::string> RequiresFlags;
	std::vector<std::string> ExcludedByFlags;
	int RequiresClues = 0;
	OpeningDef Opening;
	std::string Briefing;
	std::vector<ContextNoteDef> Context;
	std::vector<std::string> LearningObjectives;
	int StartMinute = 0;               // clock when the case starts (minutes after midnight)
	std::vector<ActionDef> Actions;
	std::vector<QuestionDef> Questions;
	std::vector<CheckDef> Checks;
	std::vector<std::string> BestOrder; // the expert's fieldwork, in order (tests, self-test, walkthrough)
	DebriefDef Debrief;
	std::vector<std::string> ProcedureCards; // Field Manual card ids earned (Data/FieldManual.json)
	std::string ReportAt = "lp_office"; // where the report is written and filed
	int PassMark = 60;
	int Xp = 100;                      // feedback only, never a promotion input
	int Weeks = 1;                     // hotel time that passes when the case closes
	std::string Lesson;                // one line for the learning history
	// Incident-triggered crises: weeks the case can wait once it opens before it escalates without you.
	int DeadlineWeeks = 0;
	std::vector<MetricEffect> EscalationEffects;
	std::string EscalationText;

	KC_API const ActionDef* FindAction(const std::string& ActionId) const;
	KC_API const QuestionDef* FindQuestion(const std::string& QuestionId) const;
};

struct MissionCatalogue
{
	std::vector<MissionDef> Missions;
	KC_API const MissionDef* Find(const std::string& Id) const;
};

// Content validation: every problem as "<mission id>: <message>". Empty means valid. Includes the
// on-foot rule (two or more places, a face-to-face conversation, every action placed) and that
// BestOrder can be walked and meets every check.
KC_API std::vector<std::string> ValidateMission(const MissionDef& M);
KC_API std::vector<std::string> ValidateCatalogue(const MissionCatalogue& C);

// ---------------------------------------------------------------- player state

struct MissionAnswer
{
	std::string QuestionId;
	std::vector<std::string> OptionIds; // Numeric: one string ("1830"); Order: every option id in order
};

struct FieldStep
{
	std::string ActionId;
	int Minute = 0;        // clock when it finished
	bool bExpired = false; // done after ExpiresAt
};

struct CustodyEntry
{
	std::string EvidenceId;
	int Minute = 0;
	std::string Text;
};

struct MissionRecord
{
	std::string MissionId;
	int Week = 0;
	int Level = 0;
	int Score = 0;
	bool bPassed = false;
	bool bWhatIf = false;                 // a replay from the debrief: no career or hotel effects
	std::vector<int> DimensionScores;     // RubricCount entries, -1 where not assessed
	std::vector<std::string> Steps;       // action ids in the order done
	std::vector<MissionAnswer> Answers;   // in-person and report answers
};

struct CampaignState
{
	std::string ActiveMissionId;
	bool bWhatIf = false;
	int Clock = 0;                         // minutes after midnight
	std::string Location;                  // where the player last acted
	std::vector<FieldStep> Steps;
	std::vector<MissionAnswer> FieldAnswers;
	std::vector<CustodyEntry> Custody;
	std::vector<std::string> SeenActions;  // findings opened in the notebook (UI)
	std::vector<MissionRecord> Records;    // every submitted attempt, oldest first
};

struct MissionAvailability
{
	bool bAvailable = false;
	bool bReview = false;
	std::vector<std::string> Reasons;
};

KC_API MissionAvailability CheckAvailability(const GameState& State, const MissionDef& M, const MissionCatalogue* Catalogue = nullptr);
KC_API bool IsCampaignStuck(const GameState& State, const MissionCatalogue& C);
KC_API std::vector<const MissionDef*> AvailableMissions(const GameState& State, const MissionCatalogue& C);
// Seeded order in which a question's options are shown (never by position in the file).
KC_API std::vector<int> OptionDisplayOrder(uint64_t GameSeed, const std::string& MissionId, const std::string& QuestionId, int OptionCount);
KC_API std::vector<std::string> BriefingFor(const GameState& State, const MissionDef& M);
// For a crisis opened by an incident: which earlier decision led here ("" if none).
KC_API std::string CrisisCause(const GameState& State, const MissionDef& M);
KC_API int WeeksUntilEscalation(const GameState& State, const MissionDef& M);

enum class EMissionError
{
	None,
	Unavailable,
	AnotherMissionActive,
	NotActive,
	UnknownAction,
	AlreadyDone,
	WrongPlace,          // go to where it is
	NeedsEarlierStep,    // Requires not met
	RequiredStepMissing, // a required action is not done yet
	UnansweredQuestion,
	AnsweredInPerson,    // this question is answered face to face, not on the form
	UnknownOption,
	SingleNeedsOneOption,
	OrderNeedsEveryOption,
	FileWhereReportsAre, // the report is filed at ReportAt
	NotSubmitted         // what-if needs an earlier attempt
};

KC_API const char* ToString(EMissionError E);

KC_API EMissionError StartMission(GameState& State, const MissionDef& M, const MissionCatalogue* Catalogue = nullptr);
// Replay a submitted mission from its debrief to try another approach. Scored and debriefed, but
// changes nothing in the career, the hotel or the world.
KC_API EMissionError StartWhatIf(GameState& State, const MissionDef& M);
KC_API EMissionError AbandonMission(GameState& State);

// Actions the player can do now at Where (not done, earlier steps met).
KC_API std::vector<const ActionDef*> ActionsAt(const GameState& State, const MissionDef& M, const std::string& Where);
KC_API bool IsActionDone(const GameState& State, const std::string& ActionId);
// Does it: walks from the last place (travel minutes), spends the action's minutes, records the
// step, custody and effects-free notes. Effects apply at submission (never in a what-if).
KC_API EMissionError DoAction(GameState& State, const MissionDef& M, const std::string& ActionId, const std::string& Where);
// Mission text names case times with tokens, filled from the case clock: "{time}" is when this
// action finished, "{time:action_id}" when that action finished ("--:--" while it is not done).
KC_API std::string ExpandTimes(const GameState& State, const std::string& Text, const std::string& SelfActionId = std::string());
// The finding text the player got (ExpiredFinding if it was done too late), times filled in.
KC_API std::string FindingFor(const GameState& State, const ActionDef& A);
KC_API std::string FindingTitleFor(const GameState& State, const ActionDef& A);
KC_API std::vector<std::vector<std::string>> RowsFor(const GameState& State, const ActionDef& A);
// In-person questions open at Where now (Requires met, not yet answered).
KC_API std::vector<const QuestionDef*> QuestionsAt(const GameState& State, const MissionDef& M, const std::string& Where);
KC_API EMissionError AnswerInPerson(GameState& State, const MissionDef& M, const MissionAnswer& Answer, const std::string& Where);
// The questions answered on the report form at submission (not in person).
KC_API std::vector<const QuestionDef*> ReportQuestions(const MissionDef& M);
KC_API EMissionError ValidateSubmission(const GameState& State, const MissionDef& M, const std::vector<MissionAnswer>& ReportAnswers);

struct MissionCriterion
{
	std::string Id;                 // question or check id
	std::string Label;
	bool bCheck = false;
	ERubric Dimension = ERubric::Procedure;
	ECompetency Competency = ECompetency::IncidentResponse;
	int Weight = 0;
	int Score = 0;
	bool bAuthorityExceeded = false;
	bool bCriticalFailed = false;
	std::vector<std::string> Feedback;
};

struct MissionDebrief
{
	std::string MissionId;
	std::string Title;
	int Score = 0;
	bool bPassed = false;
	bool bWhatIf = false;
	bool bReview = false;
	int PreviousBest = -1;                  // best earlier score (what-if comparisons)
	bool bCriticalBreach = false;
	int DimensionScores[RubricCount] = {-1, -1, -1, -1, -1, -1, -1};
	std::vector<MissionCriterion> Criteria;
	std::vector<AssessmentResult> CompetencyEvidence;
	std::vector<std::string> WellDone;      // standards met (criteria at 80 or more)
	std::vector<std::string> Timeline;      // "02:14 Interview the guest"
	int XpAwarded = 0;
	// The lesson (from the mission data).
	std::string WhatHappened;
	std::vector<std::string> BestPractice;
	std::string Why;
	std::vector<std::string> CommonMistakes;
	std::vector<std::string> CardsEarned;   // new Field Manual cards
	// Filled by SubmitMission:
	std::vector<ResolvedConsequence> Resolved;
	bool bPromoted = false;
	int Level = 0;
	std::vector<std::string> PromotionGaps;
	std::vector<std::string> Fallout;       // latent risk the weak choices left behind
	std::vector<std::string> NewMessages;
	std::vector<std::string> Escalated;
	bool bBurnout = false;
};

// Weak choices leave latent risk: a Single answer below this (with no consequence of its own), a
// wrong Multi pick, or a failed check may cause a later incident that names the decision.
constexpr int FalloutScoreThreshold = 50;
constexpr double FalloutMaxProbability = 0.6;
constexpr double FalloutMultiProbability = 0.2;
constexpr int AuthorityBreachScoreCap = 40;
constexpr int MultiFalsePickPenalty = 25;

// The answer a perfect player gives.
KC_API MissionAnswer BestAnswer(const QuestionDef& Q);
KC_API bool ParseNumericAnswer(const std::string& Text, double& Out);
// Pure scoring of a finished case (fieldwork steps and every answer). Level sets the authority.
KC_API MissionDebrief ScoreMission(const MissionDef& M, const std::vector<FieldStep>& Steps, const std::vector<MissionAnswer>& Answers, int Level);
KC_API double IncidentProbabilityMultiplier(int Difficulty);

// Scores; then (not in a what-if) applies competence evidence, the hotel decision and its effects,
// relations, stress, flags, consequences and fallout, advances hotel time, runs the world update
// and the promotion review. Earns the mission's Field Manual cards (what-ifs too: learning counts).
KC_API EMissionError SubmitMission(GameState& State, const MissionDef& M, const std::vector<MissionAnswer>& ReportAnswers,
	MissionDebrief* OutDebrief = nullptr, const MissionCatalogue* Catalogue = nullptr, const WorldContent* World = nullptr,
	const std::string& Where = std::string());

// Plays the expert's fieldwork (BestOrder) and in-person answers, and returns the best report
// answers to submit. For tests, the self-test and the walkthrough. False if it could not.
KC_API bool PlayBestFieldwork(GameState& State, const MissionDef& M, std::vector<MissionAnswer>& OutReportAnswers);
} // namespace kc
