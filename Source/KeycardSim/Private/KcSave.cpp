#include "KcSave.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace fs = std::filesystem;

namespace kc
{
// Paths are UTF-8 throughout the simulation layer.
static fs::path ToPath(const std::string& Utf8)
{
#if defined(__cpp_char8_t)
	return fs::path(std::u8string(Utf8.begin(), Utf8.end()));
#else
	return fs::u8path(Utf8);
#endif
}

const char* ToString(ELoadStatus S)
{
	switch (S)
	{
	case ELoadStatus::Ok: return "Ok";
	case ELoadStatus::RecoveredFromBackup: return "Recovered from backup";
	case ELoadStatus::NotFound: return "Not found";
	case ELoadStatus::Corrupt: return "Corrupt";
	case ELoadStatus::UnsupportedVersion: return "Unsupported version";
	}
	return "?";
}

static const char* const Magic = "KEYCARD-SAVE";

static uint64_t Fnv1a64(const std::string& S)
{
	uint64_t H = 0xCBF29CE484222325ULL;
	for (unsigned char C : S)
	{
		H ^= C;
		H *= 0x100000001B3ULL;
	}
	return H;
}

static std::string Escape(const std::string& V)
{
	std::string O;
	O.reserve(V.size());
	for (char C : V)
	{
		switch (C)
		{
		case '\\': O += "\\\\"; break;
		case '\t': O += "\\t"; break;
		case '\n': O += "\\n"; break;
		case '\r': O += "\\r"; break;
		default: O += C;
		}
	}
	return O;
}

static std::string Unescape(const std::string& V)
{
	std::string O;
	O.reserve(V.size());
	for (size_t i = 0; i < V.size(); ++i)
	{
		if (V[i] == '\\' && i + 1 < V.size())
		{
			const char N = V[++i];
			O += N == 't' ? '\t' : N == 'n' ? '\n' : N == 'r' ? '\r' : N;
		}
		else
		{
			O += V[i];
		}
	}
	return O;
}

// ---------------- writer ----------------
class KvWriter
{
public:
	std::string Body;

	void Put(const std::string& K, const std::string& V) { Body += K + "\t" + Escape(V) + "\n"; }
	void Int(const std::string& K, long long V) { Put(K, std::to_string(V)); }
	void U64(const std::string& K, uint64_t V)
	{
		char Buf[32];
		std::snprintf(Buf, sizeof(Buf), "%" PRIu64, V);
		Put(K, Buf);
	}
	void Dbl(const std::string& K, double V)
	{
		char Buf[64];
		std::snprintf(Buf, sizeof(Buf), "%.17g", V);
		Put(K, Buf);
	}
	void Bool(const std::string& K, bool V) { Put(K, V ? "1" : "0"); }
	void List(const std::string& K, const std::vector<std::string>& V)
	{
		Int(K + ".n", static_cast<long long>(V.size()));
		for (size_t i = 0; i < V.size(); ++i) Put(K + "." + std::to_string(i), V[i]);
	}
};

// ---------------- reader ----------------
class KvReader
{
public:
	std::map<std::string, std::string> Map;
	bool bOk = true;

	bool Has(const std::string& K) const { return Map.count(K) != 0; }
	std::string Str(const std::string& K, const std::string& Def = std::string()) const
	{
		const auto It = Map.find(K);
		return It == Map.end() ? Def : It->second;
	}
	long long Int(const std::string& K, long long Def = 0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const long long V = std::strtoll(It->second.c_str(), &End, 10);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return V;
	}
	uint64_t U64(const std::string& K, uint64_t Def = 0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const unsigned long long V = std::strtoull(It->second.c_str(), &End, 10);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return static_cast<uint64_t>(V);
	}
	double Dbl(const std::string& K, double Def = 0.0)
	{
		const auto It = Map.find(K);
		if (It == Map.end()) return Def;
		char* End = nullptr;
		const double V = std::strtod(It->second.c_str(), &End);
		if (End == It->second.c_str() || *End != '\0') bOk = false;
		return V;
	}
	bool Bool(const std::string& K, bool Def = false) { return Int(K, Def ? 1 : 0) != 0; }
	template <typename E>
	E Enum(const std::string& K, E Def, int Max)
	{
		const long long V = Int(K, static_cast<long long>(Def));
		if (V < 0 || V > Max)
		{
			bOk = false;
			return Def;
		}
		return static_cast<E>(V);
	}
	// Caps element counts so a hostile/corrupt file cannot request huge allocations.
	int Count(const std::string& K)
	{
		const long long N = Int(K + ".n", 0);
		if (N < 0 || N > 100000)
		{
			bOk = false;
			return 0;
		}
		return static_cast<int>(N);
	}
	std::vector<std::string> List(const std::string& K)
	{
		std::vector<std::string> V;
		const int N = Count(K);
		for (int i = 0; i < N; ++i) V.push_back(Str(K + "." + std::to_string(i)));
		return V;
	}
};


// ---------------- sections ----------------
static void WriteCareer(KvWriter& W, const CareerState& C)
{
	W.Int("career.level", C.Level);
	W.Int("career.xp", C.Xp);
	W.Int("career.origin", static_cast<int>(C.Origin));
	for (int i = 0; i < CompetencyCount; ++i)
	{
		W.Int("career.comp." + std::to_string(i) + ".s", C.Competencies[static_cast<size_t>(i)].Score);
		W.Int("career.comp." + std::to_string(i) + ".a", C.Competencies[static_cast<size_t>(i)].Assessments);
	}
	W.List("career.passed", C.PassedMissions);
	W.List("career.attempted", C.AttemptedMissions);
	W.List("career.skills", C.Skills);
}

static void ReadCareer(KvReader& R, CareerState& C)
{
	C.Level = static_cast<int>(R.Int("career.level", 1));
	if (C.Level < 1 || C.Level > TopLevel) R.bOk = false;
	C.Xp = static_cast<int>(R.Int("career.xp", 0));
	if (C.Xp < 0) R.bOk = false;
	C.Origin = R.Enum<EBackground>("career.origin", EBackground::FrontOffice, BackgroundCount - 1);
	for (int i = 0; i < CompetencyCount; ++i)
	{
		CompetencyRecord& Rec = C.Competencies[static_cast<size_t>(i)];
		Rec.Score = static_cast<int>(R.Int("career.comp." + std::to_string(i) + ".s", 0));
		Rec.Assessments = static_cast<int>(R.Int("career.comp." + std::to_string(i) + ".a", 0));
		if (Rec.Score < 0 || Rec.Score > 100 || Rec.Assessments < 0) R.bOk = false;
	}
	C.PassedMissions = R.List("career.passed");
	C.AttemptedMissions = R.List("career.attempted");
	C.Skills = R.List("career.skills");
}

static void WriteEffects(KvWriter& W, const std::string& K, const std::vector<MetricEffect>& E)
{
	W.Int(K + ".n", static_cast<long long>(E.size()));
	for (size_t i = 0; i < E.size(); ++i)
	{
		W.Int(K + "." + std::to_string(i) + ".m", static_cast<int>(E[i].Metric));
		W.Dbl(K + "." + std::to_string(i) + ".d", E[i].Delta);
	}
}

static std::vector<MetricEffect> ReadEffects(KvReader& R, const std::string& K)
{
	std::vector<MetricEffect> Out;
	const int N = R.Count(K);
	for (int i = 0; i < N; ++i)
	{
		MetricEffect E;
		E.Metric = R.Enum<EPropertyMetric>(K + "." + std::to_string(i) + ".m", EPropertyMetric::GuestSatisfaction, PropertyMetricCount - 1);
		E.Delta = R.Dbl(K + "." + std::to_string(i) + ".d", 0.0);
		if (!std::isfinite(E.Delta)) R.bOk = false;
		Out.push_back(E);
	}
	return Out;
}

static void WriteProperty(KvWriter& W, const PropertyState& P)
{
	W.Put("property.id", P.ProfileId);
	W.Put("property.name", P.Name);
	W.Int("property.week", P.Week);
	W.U64("property.seed", P.EventSeed);
	W.U64("property.draws", P.EventDraws);
	for (int i = 0; i < PropertyMetricCount; ++i) W.Dbl("property.metric." + std::to_string(i), P.Metrics[i]);
	W.List("property.flags", P.Flags);
	W.Int("property.decisions.n", static_cast<long long>(P.Decisions.size()));
	for (size_t i = 0; i < P.Decisions.size(); ++i)
	{
		const DecisionRecord& D = P.Decisions[i];
		const std::string K = "property.decisions." + std::to_string(i);
		W.Put(K + ".id", D.Id);
		W.Put(K + ".mission", D.MissionId);
		W.Put(K + ".summary", D.Summary);
		W.Int(K + ".week", D.Week);
		W.Int(K + ".quality", D.QualityScore);
		W.Put(K + ".outcome", D.Outcome);
		WriteEffects(W, K + ".effects", D.Effects);
	}
	W.Int("property.pending.n", static_cast<long long>(P.Pending.size()));
	for (size_t i = 0; i < P.Pending.size(); ++i)
	{
		const PendingConsequence& C = P.Pending[i];
		const std::string K = "property.pending." + std::to_string(i);
		W.Put(K + ".id", C.Id);
		W.Put(K + ".source", C.SourceDecisionId);
		W.Put(K + ".cause", C.Cause);
		W.Put(K + ".text", C.Description);
		W.Int(K + ".week", C.TriggerWeek);
		W.Dbl(K + ".p", C.Probability);
		WriteEffects(W, K + ".effects", C.EffectsIfOccurs);
		W.Put(K + ".mitigatedBy", C.MitigatingFlag);
		W.Dbl(K + ".mp", C.MitigatedProbabilityFactor);
		W.Dbl(K + ".ms", C.MitigatedSeverityFactor);
		W.Put(K + ".flag", C.FlagIfOccurs);
	}
	W.Int("property.history.n", static_cast<long long>(P.History.size()));
	for (size_t i = 0; i < P.History.size(); ++i)
	{
		const ResolvedConsequence& C = P.History[i];
		const std::string K = "property.history." + std::to_string(i);
		W.Put(K + ".id", C.Id);
		W.Put(K + ".source", C.SourceDecisionId);
		W.Put(K + ".cause", C.Cause);
		W.Put(K + ".text", C.Description);
		W.Int(K + ".week", C.Week);
		W.Bool(K + ".occurred", C.bOccurred);
		W.Bool(K + ".mitigated", C.bMitigated);
		W.Dbl(K + ".p", C.EffectiveProbability);
		W.Put(K + ".flag", C.FlagIfOccurs);
	}
}

static void ReadProperty(KvReader& R, PropertyState& P)
{
	const PropertyProfile* Profile = FindProperty(R.Str("property.id", "harbourfront"));
	if (!Profile)
	{
		R.bOk = false;
		return;
	}
	P = PropertyState(*Profile);
	P.Name = R.Str("property.name", Profile->Name);
	P.Week = static_cast<int>(R.Int("property.week", 0));
	if (P.Week < 0) R.bOk = false;
	P.EventSeed = R.U64("property.seed", 1);
	P.EventDraws = R.U64("property.draws", 0);
	for (int i = 0; i < PropertyMetricCount; ++i)
	{
		P.Metrics[i] = R.Dbl("property.metric." + std::to_string(i), Profile->Metrics[i]);
		if (!std::isfinite(P.Metrics[i])) R.bOk = false;
	}
	P.Flags = R.List("property.flags");
	const int ND = R.Count("property.decisions");
	for (int i = 0; i < ND; ++i)
	{
		const std::string K = "property.decisions." + std::to_string(i);
		DecisionRecord D;
		D.Id = R.Str(K + ".id");
		D.MissionId = R.Str(K + ".mission");
		D.Summary = R.Str(K + ".summary");
		D.Week = static_cast<int>(R.Int(K + ".week", 0));
		D.QualityScore = static_cast<int>(R.Int(K + ".quality", 0));
		D.Outcome = R.Str(K + ".outcome");
		D.Effects = ReadEffects(R, K + ".effects");
		P.Decisions.push_back(D);
	}
	const int NP = R.Count("property.pending");
	for (int i = 0; i < NP; ++i)
	{
		const std::string K = "property.pending." + std::to_string(i);
		PendingConsequence C;
		C.Id = R.Str(K + ".id");
		C.SourceDecisionId = R.Str(K + ".source");
		C.Cause = R.Str(K + ".cause");
		C.Description = R.Str(K + ".text");
		C.TriggerWeek = static_cast<int>(R.Int(K + ".week", 0));
		C.Probability = R.Dbl(K + ".p", 0.0);
		if (!(C.Probability >= 0.0 && C.Probability <= 1.0)) R.bOk = false;
		C.EffectsIfOccurs = ReadEffects(R, K + ".effects");
		C.MitigatingFlag = R.Str(K + ".mitigatedBy");
		C.MitigatedProbabilityFactor = R.Dbl(K + ".mp", 1.0);
		C.MitigatedSeverityFactor = R.Dbl(K + ".ms", 1.0);
		C.FlagIfOccurs = R.Str(K + ".flag");
		P.Pending.push_back(C);
	}
	const int NH = R.Count("property.history");
	for (int i = 0; i < NH; ++i)
	{
		const std::string K = "property.history." + std::to_string(i);
		ResolvedConsequence C;
		C.Id = R.Str(K + ".id");
		C.SourceDecisionId = R.Str(K + ".source");
		C.Cause = R.Str(K + ".cause");
		C.Description = R.Str(K + ".text");
		C.Week = static_cast<int>(R.Int(K + ".week", 0));
		C.bOccurred = R.Bool(K + ".occurred");
		C.bMitigated = R.Bool(K + ".mitigated");
		C.EffectiveProbability = R.Dbl(K + ".p", 0.0);
		C.FlagIfOccurs = R.Str(K + ".flag");
		P.History.push_back(C);
	}
}

static void WriteAnswers(KvWriter& W, const std::string& K, const std::vector<MissionAnswer>& A)
{
	W.Int(K + ".n", static_cast<long long>(A.size()));
	for (size_t i = 0; i < A.size(); ++i)
	{
		W.Put(K + "." + std::to_string(i) + ".q", A[i].QuestionId);
		W.List(K + "." + std::to_string(i) + ".o", A[i].OptionIds);
	}
}

static std::vector<MissionAnswer> ReadAnswers(KvReader& R, const std::string& K)
{
	std::vector<MissionAnswer> Out;
	const int N = R.Count(K);
	for (int i = 0; i < N; ++i)
	{
		MissionAnswer A;
		A.QuestionId = R.Str(K + "." + std::to_string(i) + ".q");
		A.OptionIds = R.List(K + "." + std::to_string(i) + ".o");
		Out.push_back(A);
	}
	return Out;
}

static void WriteCampaign(KvWriter& W, const CampaignState& C)
{
	W.Put("campaign.active", C.ActiveMissionId);
	W.Bool("campaign.whatIf", C.bWhatIf);
	W.Int("campaign.clock", C.Clock);
	W.Put("campaign.location", C.Location);
	W.Int("campaign.steps.n", static_cast<long long>(C.Steps.size()));
	for (size_t i = 0; i < C.Steps.size(); ++i)
	{
		const std::string K = "campaign.steps." + std::to_string(i);
		W.Put(K + ".a", C.Steps[i].ActionId);
		W.Int(K + ".m", C.Steps[i].Minute);
		W.Bool(K + ".x", C.Steps[i].bExpired);
	}
	WriteAnswers(W, "campaign.field", C.FieldAnswers);
	W.Int("campaign.custody.n", static_cast<long long>(C.Custody.size()));
	for (size_t i = 0; i < C.Custody.size(); ++i)
	{
		const std::string K = "campaign.custody." + std::to_string(i);
		W.Put(K + ".e", C.Custody[i].EvidenceId);
		W.Int(K + ".m", C.Custody[i].Minute);
		W.Put(K + ".t", C.Custody[i].Text);
	}
	W.List("campaign.seen", C.SeenActions);
	W.Int("campaign.records.n", static_cast<long long>(C.Records.size()));
	for (size_t i = 0; i < C.Records.size(); ++i)
	{
		const MissionRecord& R = C.Records[i];
		const std::string K = "campaign.records." + std::to_string(i);
		W.Put(K + ".mission", R.MissionId);
		W.Int(K + ".week", R.Week);
		W.Int(K + ".level", R.Level);
		W.Int(K + ".score", R.Score);
		W.Bool(K + ".passed", R.bPassed);
		W.Bool(K + ".whatIf", R.bWhatIf);
		std::vector<std::string> Dims;
		for (int D : R.DimensionScores) Dims.push_back(std::to_string(D));
		W.List(K + ".dims", Dims);
		W.List(K + ".steps", R.Steps);
		WriteAnswers(W, K + ".answers", R.Answers);
	}
}

static void ReadCampaign(KvReader& R, CampaignState& C)
{
	C.ActiveMissionId = R.Str("campaign.active");
	C.bWhatIf = R.Bool("campaign.whatIf");
	C.Clock = static_cast<int>(R.Int("campaign.clock", 0));
	if (C.Clock < 0 || C.Clock > 100000) R.bOk = false;
	C.Location = R.Str("campaign.location");
	const int NS = R.Count("campaign.steps");
	for (int i = 0; i < NS; ++i)
	{
		const std::string K = "campaign.steps." + std::to_string(i);
		C.Steps.push_back({R.Str(K + ".a"), static_cast<int>(R.Int(K + ".m", 0)), R.Bool(K + ".x")});
	}
	C.FieldAnswers = ReadAnswers(R, "campaign.field");
	const int NC = R.Count("campaign.custody");
	for (int i = 0; i < NC; ++i)
	{
		const std::string K = "campaign.custody." + std::to_string(i);
		C.Custody.push_back({R.Str(K + ".e"), static_cast<int>(R.Int(K + ".m", 0)), R.Str(K + ".t")});
	}
	C.SeenActions = R.List("campaign.seen");
	const int NR = R.Count("campaign.records");
	for (int i = 0; i < NR; ++i)
	{
		const std::string K = "campaign.records." + std::to_string(i);
		MissionRecord Rec;
		Rec.MissionId = R.Str(K + ".mission");
		Rec.Week = static_cast<int>(R.Int(K + ".week", 0));
		Rec.Level = static_cast<int>(R.Int(K + ".level", 1));
		Rec.Score = static_cast<int>(R.Int(K + ".score", 0));
		if (Rec.Score < 0 || Rec.Score > 100) R.bOk = false;
		Rec.bPassed = R.Bool(K + ".passed");
		Rec.bWhatIf = R.Bool(K + ".whatIf");
		for (const std::string& D : R.List(K + ".dims"))
		{
			char* End = nullptr;
			const long V = std::strtol(D.c_str(), &End, 10);
			if (End == D.c_str() || *End != '\0' || V < -1 || V > 100) R.bOk = false;
			Rec.DimensionScores.push_back(static_cast<int>(V));
		}
		Rec.Steps = R.List(K + ".steps");
		Rec.Answers = ReadAnswers(R, K + ".answers");
		C.Records.push_back(Rec);
	}
}

static void WriteWorld(KvWriter& W, const WorldState& S)
{
	W.Int("world.relations.n", static_cast<long long>(S.Relations.size()));
	for (size_t i = 0; i < S.Relations.size(); ++i)
	{
		W.Put("world.relations." + std::to_string(i) + ".id", S.Relations[i].CharacterId);
		W.Int("world.relations." + std::to_string(i) + ".v", S.Relations[i].Value);
	}
	W.Int("world.stress", S.Stress);
	W.Int("world.burnouts", S.Burnouts);
	W.List("world.clues", S.Clues);
	W.Int("world.inbox.n", static_cast<long long>(S.Inbox.size()));
	for (size_t i = 0; i < S.Inbox.size(); ++i)
	{
		const InboxMessage& M = S.Inbox[i];
		const std::string K = "world.inbox." + std::to_string(i);
		W.Put(K + ".id", M.Id);
		W.Put(K + ".event", M.EventId);
		W.Int(K + ".cat", static_cast<int>(M.Category));
		W.Put(K + ".from", M.From);
		W.Put(K + ".subject", M.Subject);
		W.Put(K + ".body", M.Body);
		W.Int(K + ".week", M.Week);
		W.Int(K + ".deadline", M.DeadlineWeek);
		W.Bool(K + ".read", M.bRead);
		W.Put(K + ".choice", M.ChoiceId);
		W.Put(K + ".outcome", M.Outcome);
		W.Bool(K + ".auto", M.bAutoResolved);
	}
	W.List("world.fired", S.FiredEvents);
	W.Int("world.news.n", static_cast<long long>(S.News.size()));
	for (size_t i = 0; i < S.News.size(); ++i)
	{
		const std::string K = "world.news." + std::to_string(i);
		W.Int(K + ".week", S.News[i].Week);
		W.Put(K + ".text", S.News[i].Text);
		W.Int(K + ".tone", S.News[i].Tone);
	}
	W.Int("world.crises.n", static_cast<long long>(S.CrisisOpened.size()));
	for (size_t i = 0; i < S.CrisisOpened.size(); ++i)
	{
		W.Put("world.crises." + std::to_string(i) + ".id", S.CrisisOpened[i].first);
		W.Int("world.crises." + std::to_string(i) + ".week", S.CrisisOpened[i].second);
	}
	W.Int("world.nextMessage", S.NextMessage);
	W.List("world.conversations", S.ConversationsHad);
}

static void ReadWorld(KvReader& R, WorldState& S)
{
	const int NR = R.Count("world.relations");
	for (int i = 0; i < NR; ++i)
	{
		Relation Rel{R.Str("world.relations." + std::to_string(i) + ".id"), static_cast<int>(R.Int("world.relations." + std::to_string(i) + ".v", 0))};
		if (Rel.Value < RelationMin || Rel.Value > RelationMax) R.bOk = false;
		S.Relations.push_back(Rel);
	}
	S.Stress = static_cast<int>(R.Int("world.stress", StressStart));
	if (S.Stress < 0 || S.Stress > StressMax) R.bOk = false;
	S.Burnouts = static_cast<int>(R.Int("world.burnouts", 0));
	S.Clues = R.List("world.clues");
	const int NI = R.Count("world.inbox");
	for (int i = 0; i < NI; ++i)
	{
		const std::string K = "world.inbox." + std::to_string(i);
		InboxMessage M;
		M.Id = R.Str(K + ".id");
		M.EventId = R.Str(K + ".event");
		M.Category = R.Enum<EMessageCategory>(K + ".cat", EMessageCategory::Team, static_cast<int>(EMessageCategory::Count) - 1);
		M.From = R.Str(K + ".from");
		M.Subject = R.Str(K + ".subject");
		M.Body = R.Str(K + ".body");
		M.Week = static_cast<int>(R.Int(K + ".week", 0));
		M.DeadlineWeek = static_cast<int>(R.Int(K + ".deadline", -1));
		M.bRead = R.Bool(K + ".read");
		M.ChoiceId = R.Str(K + ".choice");
		M.Outcome = R.Str(K + ".outcome");
		M.bAutoResolved = R.Bool(K + ".auto");
		S.Inbox.push_back(M);
	}
	S.FiredEvents = R.List("world.fired");
	const int NN = R.Count("world.news");
	for (int i = 0; i < NN; ++i)
	{
		const std::string K = "world.news." + std::to_string(i);
		S.News.push_back({static_cast<int>(R.Int(K + ".week", 0)), R.Str(K + ".text"), static_cast<int>(R.Int(K + ".tone", 0))});
	}
	const int NC = R.Count("world.crises");
	for (int i = 0; i < NC; ++i)
	{
		S.CrisisOpened.push_back({R.Str("world.crises." + std::to_string(i) + ".id"), static_cast<int>(R.Int("world.crises." + std::to_string(i) + ".week", 0))});
	}
	S.NextMessage = static_cast<int>(R.Int("world.nextMessage", 1));
	if (S.NextMessage < 1) R.bOk = false;
	S.ConversationsHad = R.List("world.conversations");
}

static void WriteChallenge(KvWriter& W, const ChallengeState& C)
{
	W.Int("challenge.mode", static_cast<int>(C.Mode));
	W.List("challenge.sequence", C.Sequence);
	W.Int("challenge.index", C.Index);
	W.Int("challenge.lives", C.Lives);
	W.Int("challenge.points", C.Points);
	W.Bool("challenge.over", C.bOver);
	W.Put("challenge.date", C.Date);
}

static void ReadChallenge(KvReader& R, ChallengeState& C)
{
	C.Mode = R.Enum<EGameMode>("challenge.mode", EGameMode::Career, static_cast<int>(EGameMode::Classroom));
	C.Sequence = R.List("challenge.sequence");
	C.Index = static_cast<int>(R.Int("challenge.index", 0));
	C.Lives = static_cast<int>(R.Int("challenge.lives", 3));
	C.Points = static_cast<int>(R.Int("challenge.points", 0));
	C.bOver = R.Bool("challenge.over");
	C.Date = R.Str("challenge.date");
	if (C.Index < 0 || C.Index > static_cast<int>(C.Sequence.size())) R.bOk = false;
}

std::string SerializeGame(const GameState& S)
{
	KvWriter W;
	W.Body = std::string(Magic) + "\n";
	W.Int("version", CurrentSaveVersion);
	W.Put("profile.name", S.ProfileName);
	W.Int("profile.difficulty", static_cast<int>(S.Difficulty));
	W.U64("profile.seed", S.Seed);
	W.Put("checkpoint", S.Checkpoint);
	W.Dbl("settings.mouse", S.Settings.MouseSensitivity);
	W.Dbl("settings.text", S.Settings.TextScale);
	W.Dbl("settings.volume", S.Settings.MasterVolume);
	W.Bool("settings.reducedMotion", S.Settings.bReducedMotion);
	W.Bool("settings.hints", S.Settings.bHints);
	W.Bool("settings.fullscreen", S.Settings.bFullscreen);
	W.Int("settings.gfx", S.Settings.GraphicsQuality);
	W.Int("settings.upscale", S.Settings.Upscaling);
	W.Int("settings.fps", S.Settings.FrameCap);
	W.Bool("settings.vsync", S.Settings.bVSync);
	W.Bool("settings.invertY", S.Settings.bInvertY);
	W.Dbl("settings.pad", S.Settings.GamepadSensitivity);
	W.Bool("settings.colourSafe", S.Settings.bColourSafe);
	WriteCareer(W, S.Career);
	W.List("manual", S.FieldManual);
	W.List("learning", S.LearningHistory);
	W.List("achievements", S.Achievements);
	WriteProperty(W, S.Property);
	WriteCampaign(W, S.Campaign);
	WriteWorld(W, S.World);
	WriteChallenge(W, S.Challenge);
	W.Bool("ceremony.pending", S.bCeremonyPending);
	W.Int("ceremony.level", S.CeremonyLevel);
	char Sum[32];
	std::snprintf(Sum, sizeof(Sum), "%016" PRIx64, Fnv1a64(W.Body));
	return W.Body + "checksum\t" + Sum + "\n";
}

// Migrations: one function per version step, run in order on the raw key/value map before the
// fields are read (Docs/SaveFormat.md).
// v1 -> v2: adds the hotel, the campaign, the world and challenges. A v1 career keeps everything it
// had; the hotel starts from the Harbourfront profile with its event stream derived from the seed.
static void MigrateV1ToV2(KvReader& R)
{
	const uint64_t Seed = R.U64("profile.seed", 1);
	R.Map["property.id"] = "harbourfront";
	R.Map["property.seed"] = std::to_string(Seed ^ 0xC0FFEE5EEDULL);
	R.Map["world.stress"] = std::to_string(StressStart);
}

static void Migrate(KvReader& R, int FromVersion, LoadResult& Info)
{
	if (FromVersion < 2)
	{
		MigrateV1ToV2(R);
		Info.bMigrated = true;
	}
}

bool DeserializeGame(const std::string& Text, GameState& Out, LoadResult& Info)
{
	Info = LoadResult();
	Info.Status = ELoadStatus::Corrupt;
	const std::string Header = std::string(Magic) + "\n";
	if (Text.compare(0, Header.size(), Header) != 0)
	{
		Info.Message = "Missing save header";
		return false;
	}
	const size_t SumPos = Text.rfind("checksum\t");
	if (SumPos == std::string::npos || (SumPos > 0 && Text[SumPos - 1] != '\n'))
	{
		Info.Message = "Missing checksum";
		return false;
	}
	std::string Expected = Text.substr(SumPos + 9);
	while (!Expected.empty() && (Expected.back() == '\n' || Expected.back() == '\r')) Expected.pop_back();
	char Actual[32];
	std::snprintf(Actual, sizeof(Actual), "%016" PRIx64, Fnv1a64(Text.substr(0, SumPos)));
	if (Expected != Actual)
	{
		Info.Message = "Checksum mismatch";
		return false;
	}

	KvReader R;
	std::istringstream In(Text.substr(Header.size(), SumPos - Header.size()));
	std::string Line;
	while (std::getline(In, Line))
	{
		const size_t Tab = Line.find('\t');
		if (Tab == std::string::npos)
		{
			Info.Message = "Malformed line";
			return false;
		}
		R.Map[Line.substr(0, Tab)] = Unescape(Line.substr(Tab + 1));
	}

	Info.LoadedVersion = static_cast<int>(R.Int("version", 0));
	if (Info.LoadedVersion > CurrentSaveVersion)
	{
		Info.Status = ELoadStatus::UnsupportedVersion;
		Info.Message = "Save is from a newer version of Keycard";
		return false;
	}
	if (Info.LoadedVersion < 1)
	{
		Info.Message = "Invalid version";
		return false;
	}
	Migrate(R, Info.LoadedVersion, Info);

	GameState S;
	S.ProfileName = R.Str("profile.name", "Officer");
	S.Difficulty = R.Enum<EDifficulty>("profile.difficulty", EDifficulty::Professional, 2);
	S.Seed = R.U64("profile.seed", 1);
	S.Checkpoint = R.Str("checkpoint");
	S.Settings.MouseSensitivity = R.Dbl("settings.mouse", 1.0);
	S.Settings.TextScale = R.Dbl("settings.text", 1.0);
	S.Settings.MasterVolume = R.Dbl("settings.volume", 0.8);
	S.Settings.bReducedMotion = R.Bool("settings.reducedMotion", false);
	S.Settings.bHints = R.Bool("settings.hints", true);
	S.Settings.bFullscreen = R.Bool("settings.fullscreen", true);
	S.Settings.GraphicsQuality = static_cast<int>(R.Int("settings.gfx", 2));
	S.Settings.Upscaling = static_cast<int>(R.Int("settings.upscale", 0));
	S.Settings.FrameCap = static_cast<int>(R.Int("settings.fps", 0));
	S.Settings.bVSync = R.Bool("settings.vsync", true);
	S.Settings.bInvertY = R.Bool("settings.invertY", false);
	S.Settings.GamepadSensitivity = R.Dbl("settings.pad", 1.0);
	S.Settings.bColourSafe = R.Bool("settings.colourSafe", false);
	S.Settings = ClampSettings(S.Settings); // a hand-edited save must not break the UI
	ReadCareer(R, S.Career);
	S.FieldManual = R.List("manual");
	S.LearningHistory = R.List("learning");
	S.Achievements = R.List("achievements");
	ReadProperty(R, S.Property);
	ReadCampaign(R, S.Campaign);
	ReadWorld(R, S.World);
	ReadChallenge(R, S.Challenge);
	S.bCeremonyPending = R.Bool("ceremony.pending");
	S.CeremonyLevel = static_cast<int>(R.Int("ceremony.level", 0));
	if (!R.bOk)
	{
		Info.Message = "Invalid field values";
		return false;
	}
	Out = std::move(S);
	Info.Status = ELoadStatus::Ok;
	return true;
}

std::string BackupPath(const std::string& Path)
{
	return Path + ".bak";
}

static bool ReadFile(const std::string& Path, std::string& Out)
{
	std::ifstream F(ToPath(Path), std::ios::binary);
	if (!F) return false;
	std::ostringstream Ss;
	Ss << F.rdbuf();
	Out = Ss.str();
	return true;
}

static bool IsValidSaveFile(const std::string& Path)
{
	std::string Text;
	GameState Tmp;
	LoadResult Info;
	return ReadFile(Path, Text) && DeserializeGame(Text, Tmp, Info);
}

bool SaveGameToFile(const std::string& Path, const GameState& State, std::string* OutError)
{
	const auto Fail = [&](const std::string& Msg) {
		if (OutError) *OutError = Msg;
		return false;
	};
	const fs::path Primary = ToPath(Path);
	const fs::path Temp = ToPath(Path + ".tmp");
	const fs::path Backup = ToPath(BackupPath(Path));
	std::error_code Ec;
	if (Primary.has_parent_path()) fs::create_directories(Primary.parent_path(), Ec);

	const std::string Text = SerializeGame(State);
	{
		std::ofstream F(Temp, std::ios::binary | std::ios::trunc);
		if (!F) return Fail("Cannot open temp file");
		F.write(Text.data(), static_cast<std::streamsize>(Text.size()));
		F.flush();
		if (!F) return Fail("Write failed");
	}
	if (!IsValidSaveFile(Path + ".tmp"))
	{
		fs::remove(Temp, Ec);
		return Fail("Verification of written save failed");
	}
	if (fs::exists(Primary, Ec))
	{
		if (IsValidSaveFile(Path))
		{
			fs::copy_file(Primary, Backup, fs::copy_options::overwrite_existing, Ec);
			if (Ec) return Fail("Backup rotation failed: " + Ec.message());
		}
		else
		{
			// Keep the corrupt file for inspection; do not let it replace a good backup.
			fs::rename(Primary, ToPath(Path + ".corrupt"), Ec);
		}
	}
	fs::rename(Temp, Primary, Ec);
	if (Ec) return Fail("Final rename failed: " + Ec.message());
	return true;
}

LoadResult LoadGameFromFile(const std::string& Path, GameState& Out)
{
	LoadResult Primary;
	std::string Text;
	const bool bPrimaryExists = ReadFile(Path, Text);
	if (bPrimaryExists && DeserializeGame(Text, Out, Primary))
	{
		return Primary;
	}
	if (Primary.Status == ELoadStatus::UnsupportedVersion)
	{
		return Primary;
	}
	LoadResult Backup;
	std::string BakText;
	const bool bBackupExists = ReadFile(BackupPath(Path), BakText);
	if (bBackupExists && DeserializeGame(BakText, Out, Backup))
	{
		Backup.Status = ELoadStatus::RecoveredFromBackup;
		Backup.Message = bPrimaryExists ? "Primary save invalid (" + Primary.Message + "); loaded backup" : "Primary save missing; loaded backup";
		return Backup;
	}
	LoadResult R;
	R.Status = (bPrimaryExists || bBackupExists) ? ELoadStatus::Corrupt : ELoadStatus::NotFound;
	R.Message = bPrimaryExists ? Primary.Message : "No save found";
	return R;
}
} // namespace kc
