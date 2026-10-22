// The hotel (or group of hotels) the player answers for: metrics, flags, decision records and
// consequences scheduled by earlier choices. Decision QUALITY (the rubric score) is recorded
// separately from the OUTCOME (what chance produced). Rules: Docs/GameDesign.md "Consequences".
#pragma once

#include "KcApi.h"

#include <cstdint>
#include <string>
#include <vector>

namespace kc
{
enum class EPropertyMetric
{
	GuestSatisfaction, // index 0..100
	Reputation,        // index 0..100 (press, reviews, brand standing)
	StaffMorale,       // index 0..100
	SafetyCompliance,  // index 0..100 (fire and life safety, audit readiness)
	SecurityPosture,   // index 0..100 (how well controls actually work)
	LossExposure,      // index 0..100 (higher = worse: latent risk the hotel carries)
	InsurerConfidence, // index 0..100
	OwnerTrust,        // index 0..100 (GM, owners and, later, the board)
	IncidentCosts,     // currency, cumulative cost of incidents (KcLpMath IncidentCost)
	LpBudget,          // currency, what is left of the LP budget this year
	Count
};

constexpr int PropertyMetricCount = static_cast<int>(EPropertyMetric::Count);
KC_API const char* ToString(EPropertyMetric M);      // display name
KC_API const char* MetricName(EPropertyMetric M);    // data file name ("GuestSatisfaction")
KC_API bool MetricFromName(const std::string& Name, EPropertyMetric& Out);
KC_API bool IsIndexMetric(EPropertyMetric M);        // clamped to 0..100

struct MetricEffect
{
	EPropertyMetric Metric = EPropertyMetric::GuestSatisfaction;
	double Delta = 0.0;
};

struct DecisionRecord
{
	std::string Id;        // "<mission>-<attempt>"
	std::string MissionId;
	std::string Summary;   // mission title and the choices made, quoted by later incidents
	int Week = 0;
	int QualityScore = 0;  // 0..100 from the rubric, independent of the outcome
	std::string Outcome;   // filled when its consequences resolve
	std::vector<MetricEffect> Effects;
};

// A consequence scheduled for later. When its week comes a seeded draw decides whether it happens.
// A mitigating flag (a control the player put in place later) scales probability and severity.
struct PendingConsequence
{
	std::string Id;
	std::string SourceDecisionId;
	std::string Cause;        // the earlier decision in words ("M001 Night Shift: Room 1412: you ...")
	std::string Description;  // what happens if it occurs
	int TriggerWeek = 0;
	double Probability = 1.0;
	std::vector<MetricEffect> EffectsIfOccurs;
	std::string MitigatingFlag;
	double MitigatedProbabilityFactor = 1.0;
	double MitigatedSeverityFactor = 1.0;
	std::string FlagIfOccurs; // set when it happens (opens follow-up missions and crises)
};

struct ResolvedConsequence
{
	std::string Id;
	std::string SourceDecisionId;
	std::string Cause;
	std::string Description;
	int Week = 0;
	bool bOccurred = false;
	bool bMitigated = false;
	double EffectiveProbability = 0.0;
	std::string FlagIfOccurs;
};

struct PropertyProfile
{
	std::string Id;
	std::string Name;
	std::string Segment;
	int Rooms = 0;
	double Metrics[PropertyMetricCount];
};

// Veldane properties: "harbourfront" (flagship, the career start), "airport", "cliffside", "convention".
KC_API const std::vector<PropertyProfile>& PropertyProfiles();
KC_API const PropertyProfile* FindProperty(const std::string& Id);

class KC_API PropertyState
{
public:
	PropertyState();
	explicit PropertyState(const PropertyProfile& Profile);

	std::string ProfileId;
	std::string Name;
	int Week = 0;
	uint64_t EventSeed = 1;  // reproducible random events
	uint64_t EventDraws = 0; // draws consumed, so a reload continues the same sequence
	double Metrics[PropertyMetricCount] = {};
	std::vector<std::string> Flags;
	std::vector<DecisionRecord> Decisions;
	std::vector<PendingConsequence> Pending;
	std::vector<ResolvedConsequence> History;

	double Get(EPropertyMetric M) const { return Metrics[static_cast<int>(M)]; }
	void Apply(const MetricEffect& E, double Scale = 1.0);
	void Apply(const std::vector<MetricEffect>& Effects, double Scale = 1.0);

	void SetFlag(const std::string& Flag);
	bool HasFlag(const std::string& Flag) const;

	void RecordDecision(DecisionRecord D);
	void Schedule(PendingConsequence C);

	// Advances time and resolves every consequence whose week has come, in trigger order.
	std::vector<ResolvedConsequence> AdvanceWeeks(int Weeks);

	// Next draw from the seeded event stream (shared with consequences; survives save and load).
	double DrawUniform() { return NextUniform(); }

private:
	double NextUniform();
};
} // namespace kc
