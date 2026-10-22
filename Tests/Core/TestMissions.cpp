#include "KcTest.h"
#include "KcTestContent.h"

#include "KcGameState.h"
#include "KcLpMath.h"
#include "KcSave.h"

#include <cstdio>
#include <map>

using namespace kc;

static GameState Fresh(uint64_t Seed = 7)
{
	GameState S = NewGame("Test", EBackground::FrontOffice, EDifficulty::Professional, Seed);
	InitWorld(S, LoadContent().World);
	return S;
}

static const MissionDef& Mission(const char* Id)
{
	static const MissionDef Empty;
	const MissionDef* M = LoadContent().Missions.Find(Id);
	return M ? *M : Empty;
}

// Plays Order (action ids), answering every in-person question with the best answer unless
// Picks names an option for it. Returns the report answers (best unless Picks says otherwise).
static bool Play(GameState& S, const MissionDef& M, const std::vector<std::string>& Order, std::vector<MissionAnswer>& Report,
	const std::map<std::string, std::string>& Picks = {})
{
	const auto AnswerFor = [&](const QuestionDef& Q) {
		const auto It = Picks.find(Q.Id);
		return It == Picks.end() ? BestAnswer(Q) : MissionAnswer{Q.Id, {It->second}};
	};
	const auto AskHere = [&](const std::string& Where) {
		for (const QuestionDef* Q : QuestionsAt(S, M, Where))
		{
			if (AnswerInPerson(S, M, AnswerFor(*Q), Where) != EMissionError::None) return false;
		}
		return true;
	};
	if (!AskHere(S.Campaign.Location)) return false;
	for (const std::string& Id : Order)
	{
		const ActionDef* A = M.FindAction(Id);
		if (!A || DoAction(S, M, Id, A->At) != EMissionError::None) return false;
		if (!AskHere(A->At)) return false;
	}
	Report.clear();
	for (const QuestionDef* Q : ReportQuestions(M)) Report.push_back(AnswerFor(*Q));
	return true;
}

static MissionDebrief PlayBest(GameState& S, const char* Id)
{
	MissionDebrief D;
	const MissionDef& M = Mission(Id);
	std::vector<MissionAnswer> Report;
	if (StartMission(S, M, &LoadContent().Missions) != EMissionError::None) return D;
	if (!PlayBestFieldwork(S, M, Report)) return D;
	SubmitMission(S, M, Report, &D, &LoadContent().Missions, &LoadContent().World, M.ReportAt);
	return D;
}

TEST(Content_EveryDataFileIsValid)
{
	const KcContent& C = LoadContent();
	for (const std::string& E : C.Errors) std::printf("    content: %s\n", E.c_str());
	CHECK(C.Errors.empty());
	for (const char* Id : {"M001", "M002", "M003"}) CHECK(C.Missions.Find(Id) != nullptr);
	CHECK(C.World.People.People.size() >= 20u);
	CHECK(!C.Cards.empty());
	CHECK(!C.Endings.empty());
}

TEST(Content_EveryMissionIsOnFootAndAssessesFourDimensions)
{
	for (const MissionDef& M : LoadContent().Missions.Missions)
	{
		std::vector<std::string> Places;
		int Interviews = 0;
		for (const ActionDef& A : M.Actions)
		{
			if (std::find(Places.begin(), Places.end(), A.At) == Places.end()) Places.push_back(A.At);
			if (A.Kind == EActionKind::Interview) ++Interviews;
		}
		CHECK(Places.size() >= 2u);
		CHECK(Interviews >= 1);
		GameState S = Fresh();
		std::vector<MissionAnswer> Report;
		REQUIRE(StartMission(S, M) == EMissionError::None);
		REQUIRE(PlayBestFieldwork(S, M, Report));
		const MissionDebrief D = ScoreMission(M, S.Campaign.Steps, [&] {
			std::vector<MissionAnswer> All = S.Campaign.FieldAnswers;
			All.insert(All.end(), Report.begin(), Report.end());
			return All;
		}(), 1);
		CHECK_EQ(D.Score, 100);
		if (D.Score != 100)
		{
			for (const MissionCriterion& C : D.Criteria)
			{
				if (C.Score < 100) std::printf("    %s %s scored %d\n", M.Id.c_str(), C.Id.c_str(), C.Score);
			}
		}
		CHECK(D.bPassed && !D.bCriticalBreach);
		int Assessed = 0;
		for (int V : D.DimensionScores) Assessed += V >= 0 ? 1 : 0;
		CHECK(Assessed >= 4);
	}
}

TEST(Campaign_FirstThreeCasesEarnThePromotionAndItsCeremony)
{
	GameState S = Fresh();
	MissionDebrief D1 = PlayBest(S, "M001");
	CHECK(D1.bPassed && D1.Score == 100 && !D1.bPromoted);
	CHECK(!S.bCeremonyPending);
	MissionDebrief D2 = PlayBest(S, "M002");
	CHECK(D2.bPassed && !D2.bPromoted);
	MissionDebrief D3 = PlayBest(S, "M003");
	CHECK(D3.bPassed);
	CHECK(D3.bPromoted);
	CHECK_EQ(S.Career.Level, 2);
	CHECK(S.bCeremonyPending);
	CHECK_EQ(S.CeremonyLevel, 2);
	for (const char* Card : {"room_theft_response", "incident_report_writing", "identity_before_entry", "noise_complaint"}) CHECK(HasCard(S, Card));
	CHECK(HasClue(S, "clone_1418"));
	CHECK_EQ(static_cast<int>(S.Campaign.Records.size()), 3);
}

TEST(Campaign_SameSeedSameCareer)
{
	GameState A = Fresh(11), B = Fresh(11);
	for (const char* Id : {"M001", "M002", "M003"})
	{
		PlayBest(A, Id);
		PlayBest(B, Id);
	}
	CHECK(SerializeGame(A) == SerializeGame(B));
}

TEST(Mission_CriticalBreachCapsTheScoreBelowThePassMark)
{
	const MissionDef& M = Mission("M002");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	std::vector<std::string> Order = M.BestOrder;
	Order.insert(Order.begin() + 3, "make_key");
	std::vector<MissionAnswer> Report;
	REQUIRE(Play(S, M, Order, Report));
	MissionDebrief D;
	REQUIRE(SubmitMission(S, M, Report, &D, &LoadContent().Missions, &LoadContent().World, M.ReportAt) == EMissionError::None);
	CHECK(D.bCriticalBreach);
	CHECK(!D.bPassed);
	CHECK(D.Score <= M.PassMark - 1);
	// The trap leaves a consequence that names the decision behind it.
	bool bFound = false;
	for (const PendingConsequence& P : S.Property.Pending) bFound |= P.Id.find("unwanted_entry") != std::string::npos && !P.Cause.empty();
	for (const ResolvedConsequence& R : S.Property.History) bFound |= R.Id.find("unwanted_entry") != std::string::npos && !R.Cause.empty();
	CHECK(bFound);
}

TEST(Mission_CriticalOptionCapsTheScore)
{
	const MissionDef& M = Mission("M002");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	std::vector<MissionAnswer> Report;
	REQUIRE(Play(S, M, M.BestOrder, Report, {{"q_visitor_leave", "disclose"}}));
	std::vector<MissionAnswer> All = S.Campaign.FieldAnswers;
	All.insert(All.end(), Report.begin(), Report.end());
	const MissionDebrief D = ScoreMission(M, S.Campaign.Steps, All, 1);
	CHECK(D.bCriticalBreach);
	CHECK(D.Score < M.PassMark);
}

TEST(Mission_LateInterviewGetsTheExpiredFinding)
{
	const MissionDef& M = Mission("M001");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	std::vector<MissionAnswer> Report;
	REQUIRE(Play(S, M, {"cctv_review", "export_footage", "log_evidence", "brief_night_manager", "check_key_log", "check_lost_property", "interview_attendant"}, Report));
	const ActionDef* A = M.FindAction("interview_attendant");
	REQUIRE(A != nullptr);
	REQUIRE(!S.Campaign.Steps.empty());
	CHECK(S.Campaign.Steps.back().ActionId == "interview_attendant");
	CHECK(S.Campaign.Steps.back().bExpired);
	CHECK(S.Campaign.Steps.back().Minute > A->ExpiresAt);
	CHECK(FindingFor(S, *A) == A->ExpiredFinding);
}

TEST(Mission_TravelBetweenFloorsCostsTime)
{
	CHECK_EQ(TravelMinutes("lp_office", "lp_office"), 0);
	CHECK_EQ(TravelMinutes("lp_office", "cctv_room"), 2);
	CHECK_EQ(TravelMinutes("lp_office", "f14_corridor"), 5);
	const MissionDef& M = Mission("M003");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	const int Start = S.Campaign.Clock;
	REQUIRE(DoAction(S, M, "radio_partner", "lp_office") == EMissionError::None);
	CHECK_EQ(S.Campaign.Clock, Start + 1);
	REQUIRE(DoAction(S, M, "listen_door", "f14_corridor") == EMissionError::None);
	CHECK_EQ(S.Campaign.Clock, Start + 1 + 5 + 2);
	CHECK(DoAction(S, M, "detector", "f14_corridor") != EMissionError::None); // wrong place and not yet knocked
}

TEST(Mission_WhatIfChangesNothingButStillScores)
{
	GameState S = Fresh();
	PlayBest(S, "M001");
	const std::string Before = SerializeGame(S);
	const int Level = S.Career.Level;
	const double Rep = S.Property.Get(EPropertyMetric::Reputation);
	const int Week = S.Property.Week;
	const MissionDef& M = Mission("M001");
	REQUIRE(StartWhatIf(S, M) == EMissionError::None);
	std::vector<MissionAnswer> Report;
	REQUIRE(Play(S, M, M.BestOrder, Report, {{"q_door", "hide"}}));
	MissionDebrief D;
	REQUIRE(SubmitMission(S, M, Report, &D, &LoadContent().Missions, &LoadContent().World, M.ReportAt) == EMissionError::None);
	CHECK(D.bWhatIf);
	CHECK(D.Score < 100);
	CHECK_EQ(S.Career.Level, Level);
	CHECK_NEAR(S.Property.Get(EPropertyMetric::Reputation), Rep, 1e-9);
	CHECK_EQ(S.Property.Week, Week);
	for (const PendingConsequence& P : S.Property.Pending) CHECK(P.Id.find("open_door_theft") == std::string::npos);
	REQUIRE(!S.Campaign.Records.empty());
	CHECK(S.Campaign.Records.back().bWhatIf);
	CHECK(Before != SerializeGame(S)); // the attempt itself is recorded
}

TEST(Mission_ExceedingAuthorityIsCapped)
{
	const MissionDef& M = Mission("M002");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	std::vector<MissionAnswer> Report;
	REQUIRE(Play(S, M, M.BestOrder, Report, {{"d_cost", "upgrade"}}));
	std::vector<MissionAnswer> All = S.Campaign.FieldAnswers;
	All.insert(All.end(), Report.begin(), Report.end());
	const MissionDebrief D = ScoreMission(M, S.Campaign.Steps, All, 1);
	bool bSeen = false;
	for (const MissionCriterion& C : D.Criteria)
	{
		if (C.Id == "d_cost")
		{
			bSeen = true;
			CHECK(C.Score <= AuthorityBreachScoreCap);
		}
	}
	CHECK(bSeen);
	CHECK(D.Score < 100);
}

TEST(Mission_SubmitNeedsEveryReportAnswerAndTheRightDesk)
{
	const MissionDef& M = Mission("M003");
	GameState S = Fresh();
	REQUIRE(StartMission(S, M) == EMissionError::None);
	std::vector<MissionAnswer> Report;
	REQUIRE(PlayBestFieldwork(S, M, Report));
	std::vector<MissionAnswer> Short(Report.begin(), Report.end() - 1);
	CHECK(ValidateSubmission(S, M, Short) == EMissionError::UnansweredQuestion);
	CHECK(SubmitMission(S, M, Report, nullptr, &LoadContent().Missions, &LoadContent().World, "lobby") == EMissionError::FileWhereReportsAre);
	CHECK(SubmitMission(S, M, Report, nullptr, &LoadContent().Missions, &LoadContent().World, M.ReportAt) == EMissionError::None);
}

TEST(Save_V2RoundTripsACampaignMidCase)
{
	GameState S = Fresh();
	PlayBest(S, "M001");
	const MissionDef& M = Mission("M002");
	REQUIRE(StartMission(S, M) == EMissionError::None);
	REQUIRE(DoAction(S, M, "talk_visitor", "lobby") == EMissionError::None);
	GameState L;
	LoadResult Info;
	REQUIRE(DeserializeGame(SerializeGame(S), L, Info));
	CHECK(!Info.bMigrated);
	CHECK(SerializeGame(L) == SerializeGame(S));
	// The loaded game finishes the case exactly as the original would.
	std::vector<MissionAnswer> RA, RB;
	std::vector<std::string> Rest(M.BestOrder.begin() + 1, M.BestOrder.end());
	REQUIRE(Play(S, M, Rest, RA));
	REQUIRE(Play(L, M, Rest, RB));
	MissionDebrief DA, DB;
	SubmitMission(S, M, RA, &DA, &LoadContent().Missions, &LoadContent().World, M.ReportAt);
	SubmitMission(L, M, RB, &DB, &LoadContent().Missions, &LoadContent().World, M.ReportAt);
	CHECK_EQ(DA.Score, DB.Score);
	CHECK(SerializeGame(L) == SerializeGame(S));
}

TEST(Save_V1MigratesToTheHarbourfront)
{
	// A v1 save: the Phase 0 career keys only.
	GameState S = NewGame("Old", EBackground::Military, EDifficulty::Expert, 99);
	std::string Text = SerializeGame(S);
	std::string Body = Text.substr(0, Text.rfind("checksum\t"));
	std::string V1;
	size_t Pos = 0;
	while (Pos < Body.size())
	{
		const size_t End = Body.find('\n', Pos);
		const std::string Line = Body.substr(Pos, End - Pos + 1);
		Pos = End + 1;
		const bool bV2 = Line.rfind("property.", 0) == 0 || Line.rfind("campaign.", 0) == 0 || Line.rfind("world.", 0) == 0 ||
			Line.rfind("challenge.", 0) == 0 || Line.rfind("ceremony.", 0) == 0;
		if (bV2) continue;
		V1 += Line.rfind("version\t", 0) == 0 ? "version\t1\n" : Line;
	}
	uint64_t H = 0xCBF29CE484222325ULL;
	for (unsigned char C : V1)
	{
		H ^= C;
		H *= 0x100000001B3ULL;
	}
	char Sum[32];
	std::snprintf(Sum, sizeof(Sum), "%016llx", static_cast<unsigned long long>(H));
	GameState L;
	LoadResult Info;
	REQUIRE(DeserializeGame(V1 + "checksum\t" + Sum + "\n", L, Info));
	CHECK(Info.bMigrated);
	CHECK_EQ(Info.LoadedVersion, 1);
	CHECK(L.Property.ProfileId == "harbourfront");
	CHECK(L.Property.EventSeed == (99ULL ^ 0xC0FFEE5EEDULL));
	CHECK_EQ(L.World.Stress, StressStart);
	CHECK(L.ProfileName == "Old");
	CHECK_EQ(L.Career.Level, 1);
}

TEST(Lp_NamedFormulasMatchTheFunctions)
{
	double V = 0;
	std::string Err;
	REQUIRE(lp::EvaluateFormula("ShrinkPercent", {{"book", 2000}, {"counted", 1900}}, V, Err));
	CHECK_NEAR(V, lp::ShrinkPercent(2000, 1900).Value, 1e-9);
	CHECK(!lp::EvaluateFormula("ShrinkPercent", {{"book", 2000}}, V, Err));
	CHECK(!Err.empty());
	CHECK(!lp::EvaluateFormula("NoSuchFormula", {}, V, Err));
	CHECK(lp::FormulaNames().size() >= 13u);
	CHECK(FormatMoney(1234567.5) == "1,234,567.50");
	CHECK(FormatMoney(-380) == "-380");
	CHECK(FormatMoney(0) == "0");
}

TEST(Learning_LeaderboardRoundTrips)
{
	std::vector<LeaderboardEntry> E;
	AddToLeaderboard(E, {"Survival", "Ines\tD", "3 cases", 420, 2, "2026-10-22"});
	AddToLeaderboard(E, {"Daily", "Felix", "2026-10-22", 300, 1, "2026-10-22"});
	const std::vector<LeaderboardEntry> L = DeserializeLeaderboard(SerializeLeaderboard(E));
	REQUIRE(L.size() == 2u);
	CHECK(SerializeLeaderboard(L) == SerializeLeaderboard(E));
	CHECK(DeserializeLeaderboard("garbage").empty());
}
