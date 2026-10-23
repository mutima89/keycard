#include "KcLpMath.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

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
namespace
{
struct FormulaSpec
{
	const char* Name;
	std::vector<std::string> Inputs;
};

const std::vector<FormulaSpec>& Specs()
{
	static const std::vector<FormulaSpec> S = {
		{"ShrinkPercent", {"book", "counted"}},
		{"CostVariancePoints", {"actual", "ideal", "sales"}},
		{"LossRatePer1000RoomNights", {"value", "roomNights"}},
		{"ClaimReserve", {"medical", "indemnity", "expense", "paid"}},
		{"ClaimIncurred", {"medical", "indemnity", "expense", "paid"}},
		{"IncidentCost", {"uninsured", "hours", "rate", "repairs", "recovery", "insured", "deductible"}},
		{"InsurerRecovery", {"uninsured", "hours", "rate", "repairs", "recovery", "insured", "deductible"}},
		{"PatrolCompliancePercent", {"onTime", "scheduled"}},
		{"PostHoursPerWeek", {"hoursPerDay", "daysPerWeek", "posts"}},
		{"FtesRequired", {"postHours", "productiveHours"}},
		{"StaffingCoveragePercent", {"scheduled", "required"}},
		{"RekeyCost", {"doors", "costPerDoor", "labourMinutes", "rate", "keys", "costPerKey"}},
		{"AnnualisedLoss", {"eventsPerYear", "averageCost"}},
	};
	return S;
}
} // namespace

const std::vector<std::string>& FormulaNames()
{
	static const std::vector<std::string> N = [] {
		std::vector<std::string> Out;
		for (const FormulaSpec& F : Specs()) Out.push_back(F.Name);
		return Out;
	}();
	return N;
}

bool EvaluateFormula(const std::string& Name, const std::vector<std::pair<std::string, double>>& Inputs, double& OutValue, std::string& OutError)
{
	const FormulaSpec* Spec = nullptr;
	for (const FormulaSpec& F : Specs())
	{
		if (Name == F.Name) Spec = &F;
	}
	if (!Spec)
	{
		OutError = "unknown formula \"" + Name + "\"";
		return false;
	}
	std::vector<double> V(Spec->Inputs.size(), 0.0);
	std::vector<bool> Given(Spec->Inputs.size(), false);
	for (const auto& In : Inputs)
	{
		size_t i = 0;
		while (i < Spec->Inputs.size() && Spec->Inputs[i] != In.first) ++i;
		if (i == Spec->Inputs.size())
		{
			OutError = Name + " has no input \"" + In.first + "\"";
			return false;
		}
		V[i] = In.second;
		Given[i] = true;
	}
	// Every input is named: a forgotten one must not silently count as zero.
	for (size_t i = 0; i < Given.size(); ++i)
	{
		if (!Given[i])
		{
			OutError = Name + " needs input \"" + Spec->Inputs[i] + "\"";
			return false;
		}
	}
	auto I = [&](size_t i) { return static_cast<int>(std::lround(V[i])); };
	Result R;
	const std::string& N = Name;
	if (N == "ShrinkPercent") R = ShrinkPercent(V[0], V[1]);
	else if (N == "CostVariancePoints") R = CostVariancePoints(V[0], V[1], V[2]);
	else if (N == "LossRatePer1000RoomNights") R = LossRatePer1000RoomNights(V[0], V[1]);
	else if (N == "ClaimReserve" || N == "ClaimIncurred")
	{
		const ClaimEstimate E{V[0], V[1], V[2], V[3]};
		R = N == "ClaimReserve" ? ClaimReserve(E) : ClaimIncurred(E);
	}
	else if (N == "IncidentCost" || N == "InsurerRecovery")
	{
		const IncidentCostInputs E{V[0], V[1], V[2], V[3], V[4], V[5], V[6]};
		R = N == "IncidentCost" ? IncidentCost(E) : InsurerRecovery(E);
	}
	else if (N == "PatrolCompliancePercent") R = PatrolCompliancePercent(I(0), I(1));
	else if (N == "PostHoursPerWeek") R = PostHoursPerWeek(V[0], V[1], I(2));
	else if (N == "FtesRequired") R = FtesRequired(V[0], V[1]);
	else if (N == "StaffingCoveragePercent") R = StaffingCoveragePercent(V[0], V[1]);
	else if (N == "RekeyCost") R = RekeyCost({I(0), V[1], V[2], V[3], I(4), V[5]});
	else if (N == "AnnualisedLoss") R = AnnualisedLoss(V[0], V[1]);
	if (!R.bValid)
	{
		OutError = Name + ": invalid inputs";
		return false;
	}
	OutValue = R.Value;
	return true;
}
} // namespace lp

std::string FormatMoney(double Value)
{
	if (!std::isfinite(Value)) return "?";
	const bool bNeg = Value < 0.0;
	const double Abs = std::fabs(Value);
	const long long Whole = static_cast<long long>(std::floor(Abs + 1e-9));
	const int Cents = static_cast<int>(std::lround((Abs - static_cast<double>(Whole)) * 100.0));
	std::string Digits = std::to_string(Cents == 100 ? Whole + 1 : Whole);
	std::string Out;
	for (size_t i = 0; i < Digits.size(); ++i)
	{
		if (i > 0 && (Digits.size() - i) % 3 == 0) Out += ',';
		Out += Digits[i];
	}
	if (Cents > 0 && Cents < 100)
	{
		char Buf[8];
		std::snprintf(Buf, sizeof(Buf), ".%02d", Cents);
		Out += Buf;
	}
	return (bNeg ? "-" : "") + Out;
}
} // namespace kc
