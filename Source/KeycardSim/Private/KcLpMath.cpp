#include "KcLpMath.h"

#include <algorithm>
#include <cmath>

namespace kc
{
namespace lp
{
static bool Finite(double V) { return std::isfinite(V); }
static bool NonNeg(double V) { return Finite(V) && V >= 0.0; }
static Result Ok(double V) { return {V, Finite(V)}; }
static const Result Invalid{};

Result ShrinkPercent(double BookValue, double CountedValue)
{
	if (!Finite(BookValue) || BookValue <= 0.0 || !NonNeg(CountedValue)) return Invalid;
	return Ok((BookValue - CountedValue) / BookValue * 100.0);
}

Result CostVariancePoints(double ActualCost, double IdealCost, double Sales)
{
	if (!NonNeg(ActualCost) || !NonNeg(IdealCost) || !Finite(Sales) || Sales <= 0.0) return Invalid;
	return Ok((ActualCost - IdealCost) / Sales * 100.0);
}

Result LossRatePer1000RoomNights(double IncidentsOrLossValue, double OccupiedRoomNights)
{
	if (!NonNeg(IncidentsOrLossValue) || !Finite(OccupiedRoomNights) || OccupiedRoomNights <= 0.0) return Invalid;
	return Ok(IncidentsOrLossValue / OccupiedRoomNights * 1000.0);
}

static bool ValidClaim(const ClaimEstimate& E)
{
	return NonNeg(E.Medical) && NonNeg(E.Indemnity) && NonNeg(E.Expense) && NonNeg(E.PaidToDate);
}

Result ClaimReserve(const ClaimEstimate& E)
{
	if (!ValidClaim(E)) return Invalid;
	return Ok(std::max(0.0, E.Medical + E.Indemnity + E.Expense - E.PaidToDate));
}

Result ClaimIncurred(const ClaimEstimate& E)
{
	const Result R = ClaimReserve(E);
	if (!R.bValid) return Invalid;
	return Ok(E.PaidToDate + R.Value);
}

static bool ValidIncident(const IncidentCostInputs& In)
{
	return NonNeg(In.UninsuredLoss) && NonNeg(In.ResponseHours) && NonNeg(In.HourlyRate) && NonNeg(In.Repairs) &&
		NonNeg(In.GuestRecovery) && NonNeg(In.InsuredLoss) && NonNeg(In.Deductible);
}

Result IncidentCost(const IncidentCostInputs& In)
{
	if (!ValidIncident(In)) return Invalid;
	return Ok(In.UninsuredLoss + In.ResponseHours * In.HourlyRate + In.Repairs + In.GuestRecovery +
		std::min(In.InsuredLoss, In.Deductible));
}

Result InsurerRecovery(const IncidentCostInputs& In)
{
	if (!ValidIncident(In)) return Invalid;
	return Ok(std::max(0.0, In.InsuredLoss - In.Deductible));
}

Result PatrolCompliancePercent(int ScannedOnTime, int Scheduled)
{
	if (Scheduled <= 0 || ScannedOnTime < 0 || ScannedOnTime > Scheduled) return Invalid;
	return Ok(100.0 * ScannedOnTime / Scheduled);
}

AuditResult AuditScore(const std::vector<AuditItem>& Items, double PassMarkPercent)
{
	AuditResult Out;
	if (!Finite(PassMarkPercent) || PassMarkPercent < 0.0 || PassMarkPercent > 100.0) return Out;
	double Earned = 0.0;
	double Possible = 0.0;
	for (const AuditItem& I : Items)
	{
		if (I.bNotApplicable) continue;
		if (!Finite(I.MaxPoints) || I.MaxPoints <= 0.0 || !NonNeg(I.Points) || I.Points > I.MaxPoints || !Finite(I.Weight) || I.Weight <= 0.0)
		{
			return AuditResult();
		}
		Earned += I.Weight * I.Points;
		Possible += I.Weight * I.MaxPoints;
		if (I.bCritical && I.Points < I.MaxPoints) ++Out.CriticalFailures;
	}
	if (Possible <= 0.0) return AuditResult();
	Out.ScorePercent = Earned / Possible * 100.0;
	Out.bPass = Out.CriticalFailures == 0 && Out.ScorePercent >= PassMarkPercent;
	Out.bValid = true;
	return Out;
}

Result PostHoursPerWeek(double HoursPerDay, double DaysPerWeek, int Posts)
{
	if (!NonNeg(HoursPerDay) || HoursPerDay > 24.0 || !NonNeg(DaysPerWeek) || DaysPerWeek > 7.0 || Posts < 0) return Invalid;
	return Ok(HoursPerDay * DaysPerWeek * Posts);
}

Result FtesRequired(double PostHoursWeekly, double ProductiveHoursPerFteYear)
{
	if (!NonNeg(PostHoursWeekly) || !Finite(ProductiveHoursPerFteYear) || ProductiveHoursPerFteYear <= 0.0) return Invalid;
	return Ok(PostHoursWeekly * 52.0 / ProductiveHoursPerFteYear);
}

Result StaffingCoveragePercent(double ScheduledHoursWeekly, double RequiredHoursWeekly)
{
	if (!NonNeg(ScheduledHoursWeekly) || !Finite(RequiredHoursWeekly) || RequiredHoursWeekly <= 0.0) return Invalid;
	return Ok(ScheduledHoursWeekly / RequiredHoursWeekly * 100.0);
}

Result RekeyCost(const RekeyInputs& In)
{
	if (In.Doors < 0 || In.KeysToReissue < 0 || !NonNeg(In.CostPerDoor) || !NonNeg(In.LabourMinutesPerDoor) ||
		!NonNeg(In.HourlyRate) || !NonNeg(In.CostPerKey))
	{
		return Invalid;
	}
	return Ok(In.Doors * (In.CostPerDoor + In.LabourMinutesPerDoor / 60.0 * In.HourlyRate) + In.KeysToReissue * In.CostPerKey);
}

Result AnnualisedLoss(double EventsPerYear, double AverageCostPerEvent)
{
	if (!NonNeg(EventsPerYear) || !NonNeg(AverageCostPerEvent)) return Invalid;
	return Ok(EventsPerYear * AverageCostPerEvent);
}

int ScoreByCloseness(double Answer, double Truth, double RelativeTolerance)
{
	if (!Finite(Answer) || !Finite(Truth) || !Finite(RelativeTolerance) || RelativeTolerance <= 0.0) return 0;
	// Relative to the truth; a zero truth is compared in absolute terms against the tolerance.
	const double Scale = std::fabs(Truth) > 0.0 ? std::fabs(Truth) : 1.0;
	const double Err = std::fabs(Answer - Truth) / Scale;
	if (Err <= RelativeTolerance) return CloseScoreExact;
	if (Err <= 3.0 * RelativeTolerance) return CloseScoreClose;
	if (Err <= 10.0 * RelativeTolerance) return CloseScoreNear;
	return 0;
}
} // namespace lp
} // namespace kc
