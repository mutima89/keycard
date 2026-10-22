#include "KcProperty.h"

#include "KcRng.h"

#include <algorithm>

namespace kc
{
static const char* const MetricNames[PropertyMetricCount] = {"GuestSatisfaction", "Reputation", "StaffMorale", "SafetyCompliance",
	"SecurityPosture", "LossExposure", "InsurerConfidence", "OwnerTrust", "IncidentCosts", "LpBudget"};

const char* ToString(EPropertyMetric M)
{
	switch (M)
	{
	case EPropertyMetric::GuestSatisfaction: return "Guest satisfaction";
	case EPropertyMetric::Reputation: return "Reputation";
	case EPropertyMetric::StaffMorale: return "Staff morale";
	case EPropertyMetric::SafetyCompliance: return "Safety compliance";
	case EPropertyMetric::SecurityPosture: return "Security posture";
	case EPropertyMetric::LossExposure: return "Loss exposure";
	case EPropertyMetric::InsurerConfidence: return "Insurer confidence";
	case EPropertyMetric::OwnerTrust: return "Owner trust";
	case EPropertyMetric::IncidentCosts: return "Incident costs";
	case EPropertyMetric::LpBudget: return "LP budget left";
	case EPropertyMetric::Count: break;
	}
	return "?";
}

const char* MetricName(EPropertyMetric M)
{
	const int I = static_cast<int>(M);
	return I >= 0 && I < PropertyMetricCount ? MetricNames[I] : "?";
}

bool MetricFromName(const std::string& Name, EPropertyMetric& Out)
{
	for (int i = 0; i < PropertyMetricCount; ++i)
	{
		if (Name == MetricNames[i])
		{
			Out = static_cast<EPropertyMetric>(i);
			return true;
		}
	}
	return false;
}

bool IsIndexMetric(EPropertyMetric M)
{
	return M != EPropertyMetric::IncidentCosts && M != EPropertyMetric::LpBudget;
}

const std::vector<PropertyProfile>& PropertyProfiles()
{
	// Order of metrics follows EPropertyMetric.
	static const std::vector<PropertyProfile> P = {
		{"harbourfront", "Veldane Grand Harbourfront", "Full-service flagship, Port Avener", 600, {72, 70, 62, 64, 55, 45, 60, 60, 0, 420000}},
		{"airport", "Veldane Airport Port Avener", "Select service", 240, {68, 64, 58, 60, 50, 50, 58, 60, 0, 140000}},
		{"cliffside", "Veldane Cliffside Resort", "Resort and spa", 320, {78, 74, 60, 58, 50, 50, 56, 62, 0, 260000}},
		{"convention", "Veldane Convention Centre Hotel", "Convention hotel", 900, {66, 66, 56, 62, 52, 52, 58, 58, 0, 610000}},
	};
	return P;
}

const PropertyProfile* FindProperty(const std::string& Id)
{
	for (const PropertyProfile& P : PropertyProfiles())
	{
		if (P.Id == Id) return &P;
	}
	return nullptr;
}

PropertyState::PropertyState()
	: PropertyState(PropertyProfiles().front())
{
}

PropertyState::PropertyState(const PropertyProfile& Profile)
	: ProfileId(Profile.Id)
	, Name(Profile.Name)
{
	std::copy(Profile.Metrics, Profile.Metrics + PropertyMetricCount, Metrics);
}

void PropertyState::Apply(const MetricEffect& E, double Scale)
{
	const int I = static_cast<int>(E.Metric);
	if (I < 0 || I >= PropertyMetricCount) return;
	double& V = Metrics[I];
	V += E.Delta * Scale;
	if (IsIndexMetric(E.Metric)) V = std::max(0.0, std::min(100.0, V));
}

void PropertyState::Apply(const std::vector<MetricEffect>& Effects, double Scale)
{
	for (const MetricEffect& E : Effects) Apply(E, Scale);
}

void PropertyState::SetFlag(const std::string& Flag)
{
	if (!Flag.empty() && !HasFlag(Flag)) Flags.push_back(Flag);
}

bool PropertyState::HasFlag(const std::string& Flag) const
{
	return std::find(Flags.begin(), Flags.end(), Flag) != Flags.end();
}

void PropertyState::RecordDecision(DecisionRecord D)
{
	D.Week = Week;
	Apply(D.Effects);
	Decisions.push_back(std::move(D));
}

void PropertyState::Schedule(PendingConsequence C)
{
	Pending.push_back(std::move(C));
}

double PropertyState::NextUniform()
{
	// Re-derive the stream from seed + draw count so the sequence survives save and load
	// without serialising generator internals.
	Rng R(EventSeed ^ (0x9E3779B97F4A7C15ULL * (EventDraws + 1)));
	++EventDraws;
	return R.Uniform();
}

std::vector<ResolvedConsequence> PropertyState::AdvanceWeeks(int Weeks)
{
	std::vector<ResolvedConsequence> Resolved;
	Week += std::max(0, Weeks);
	std::stable_sort(Pending.begin(), Pending.end(),
		[](const PendingConsequence& A, const PendingConsequence& B) { return A.TriggerWeek < B.TriggerWeek; });
	std::vector<PendingConsequence> Remaining;
	for (PendingConsequence& C : Pending)
	{
		if (C.TriggerWeek > Week)
		{
			Remaining.push_back(std::move(C));
			continue;
		}
		ResolvedConsequence R;
		R.Id = C.Id;
		R.SourceDecisionId = C.SourceDecisionId;
		R.Cause = C.Cause;
		R.Description = C.Description;
		R.Week = C.TriggerWeek;
		R.FlagIfOccurs = C.FlagIfOccurs;
		R.bMitigated = !C.MitigatingFlag.empty() && HasFlag(C.MitigatingFlag);
		R.EffectiveProbability = std::min(1.0, std::max(0.0, C.Probability * (R.bMitigated ? C.MitigatedProbabilityFactor : 1.0)));
		R.bOccurred = NextUniform() < R.EffectiveProbability;
		if (R.bOccurred)
		{
			Apply(C.EffectsIfOccurs, R.bMitigated ? C.MitigatedSeverityFactor : 1.0);
			SetFlag(C.FlagIfOccurs);
		}
		for (DecisionRecord& D : Decisions)
		{
			if (D.Id == C.SourceDecisionId)
			{
				if (!D.Outcome.empty()) D.Outcome += " ";
				D.Outcome += (R.bOccurred ? "Incident occurred: " : "Incident avoided: ") + C.Description;
			}
		}
		History.push_back(R);
		Resolved.push_back(R);
	}
	Pending = std::move(Remaining);
	return Resolved;
}
} // namespace kc
