// Every Loss Prevention formula in the game, in one place. Tools, scoring and UI call these and
// never re-implement them. Definitions, worked examples and limits: Docs/LpMethodology.md.
// Money is plain currency units. Ordinal ratings are never money. Invalid inputs return
// bValid = false (never NaN or a silent zero).
#pragma once

#include "KcApi.h"

#include <vector>

namespace kc
{
namespace lp
{
struct Result
{
	double Value = 0.0;
	bool bValid = false;
};

// Inventory shrink (minibar, F&B stores, linen, retail): (book - counted) / book x 100.
// Positive = loss, negative = overage. Book value must be > 0, counted >= 0.
KC_API Result ShrinkPercent(double BookValue, double CountedValue);

// Beverage or food cost variance in percentage points: actual cost % - ideal cost %,
// where cost % = cost / sales x 100. Positive = more product used than sales explain.
KC_API Result CostVariancePoints(double ActualCost, double IdealCost, double Sales);

// Incidents (or loss value) per 1,000 occupied room nights.
KC_API Result LossRatePer1000RoomNights(double IncidentsOrLossValue, double OccupiedRoomNights);

// Case reserve on an open claim: estimated ultimate (medical + indemnity + expense) - paid to date,
// never below zero. Incurred = paid + reserve.
struct ClaimEstimate
{
	double Medical = 0.0;
	double Indemnity = 0.0; // lost wages, damages, settlement
	double Expense = 0.0;   // adjuster, legal and other claim handling costs
	double PaidToDate = 0.0;
};
KC_API Result ClaimReserve(const ClaimEstimate& E);
KC_API Result ClaimIncurred(const ClaimEstimate& E);

// Cost of an incident to the hotel. Insured loss is paid by the insurer above the deductible,
// so the hotel keeps min(insured loss, deductible). All inputs >= 0.
struct IncidentCostInputs
{
	double UninsuredLoss = 0.0;  // stolen cash, items below deductible handled locally
	double ResponseHours = 0.0;  // staff hours spent on the response and paperwork
	double HourlyRate = 0.0;     // loaded labour rate
	double Repairs = 0.0;
	double GuestRecovery = 0.0;  // refunds, comps and goodwill
	double InsuredLoss = 0.0;    // gross loss covered by the policy
	double Deductible = 0.0;
};
KC_API Result IncidentCost(const IncidentCostInputs& In);
// What the insurer pays: max(0, insured loss - deductible).
KC_API Result InsurerRecovery(const IncidentCostInputs& In);

// Patrol tour compliance: checkpoints scanned inside their time window / checkpoints scheduled x 100.
KC_API Result PatrolCompliancePercent(int ScannedOnTime, int Scheduled);

// LP audit. Each item is scored 0..MaxPoints with a weight; not-applicable items are left out.
// Score = sum(weight x points) / sum(weight x max points) x 100. Any failed critical item
// fails the audit whatever the score (bPass false), as brand audits do.
struct AuditItem
{
	double Points = 0.0;
	double MaxPoints = 1.0;
	double Weight = 1.0;
	bool bCritical = false;
	bool bNotApplicable = false;
};
struct AuditResult
{
	double ScorePercent = 0.0;
	bool bPass = false;
	int CriticalFailures = 0;
	bool bValid = false;
};
KC_API AuditResult AuditScore(const std::vector<AuditItem>& Items, double PassMarkPercent);

// Staffing. Post hours per week for posts covered HoursPerDay on DaysPerWeek; FTEs needed for
// those hours given each officer's productive hours per year (after leave, training, sickness);
// coverage = scheduled hours / required hours x 100.
KC_API Result PostHoursPerWeek(double HoursPerDay, double DaysPerWeek, int Posts);
KC_API Result FtesRequired(double PostHoursWeekly, double ProductiveHoursPerFteYear);
KC_API Result StaffingCoveragePercent(double ScheduledHoursWeekly, double RequiredHoursWeekly);

// Re-key after a lost key: doors x (hardware or re-code cost + labour minutes / 60 x rate)
// + keys re-issued x cost per key.
struct RekeyInputs
{
	int Doors = 0;
	double CostPerDoor = 0.0;
	double LabourMinutesPerDoor = 0.0;
	double HourlyRate = 0.0;
	int KeysToReissue = 0;
	double CostPerKey = 0.0;
};
KC_API Result RekeyCost(const RekeyInputs& In);

// Annualised loss expectancy: events per year x average cost per event. An average, not a worst case.
KC_API Result AnnualisedLoss(double EventsPerYear, double AverageCostPerEvent);

// Typed figures in tools and missions are scored by closeness to the model answer (relative error).
constexpr int CloseScoreExact = 100; // within tolerance
constexpr int CloseScoreClose = 70;  // within 3 x tolerance
constexpr int CloseScoreNear = 35;   // within 10 x tolerance
KC_API int ScoreByCloseness(double Answer, double Truth, double RelativeTolerance);
} // namespace lp
} // namespace kc
