#include "KcTest.h"

#include "KcCareer.h"
#include "KcGameState.h"

#include <set>

using namespace kc;

TEST(Career_LadderHasTenLevelsFromOfficerToVp)
{
	const auto& L = CareerLadder();
	REQUIRE(L.size() == 10u);
	CHECK(L.front().Title == "LP Officer");
	CHECK(L.back().Title == "Vice President, Global Safety & Security");
	std::set<std::string> Missions;
	for (size_t i = 0; i < L.size(); ++i)
	{
		CHECK(L[i].Level == static_cast<int>(i) + 1);
		if (i + 1 < L.size())
		{
			// 3 or 4 missions per level; levels 6-9 also need one career crisis.
			CHECK(L[i].RequiredMissions.size() >= 3u && L[i].RequiredMissions.size() <= 4u);
			CHECK(!L[i].PromotionRequirements.empty());
			const bool bCrisisLevel = L[i].Level >= 6 && L[i].Level <= 9;
			CHECK_EQ(L[i].RequiredCrises, bCrisisLevel ? 1 : 0);
			CHECK(static_cast<int>(L[i].CrisisPool.size()) >= L[i].RequiredCrises);
		}
		for (const std::string& M : L[i].RequiredMissions) CHECK(Missions.insert(M).second); // never reused
		if (i > 0) CHECK(L[i].DecisionAuthority >= L[i - 1].DecisionAuthority);
		CHECK(L[i].CeremonyStage == "property" || L[i].CeremonyStage == "area" || L[i].CeremonyStage == "board");
	}
}

TEST(Career_CompetencyNamesRoundTrip)
{
	for (int i = 0; i < CompetencyCount; ++i)
	{
		CHECK(std::string(ToString(static_cast<ECompetency>(i))) != "?");
	}
	ECompetency Out = ECompetency::Count;
	CHECK(CompetencyFromName("KeyAccessControl", Out) && Out == ECompetency::KeyAccessControl);
	CHECK(!CompetencyFromName("Key & Access Control", Out));
}

TEST(Career_XpAloneNeverPromotes)
{
	CareerState C;
	C.Xp = 1000000;
	CHECK(!C.CheckPromotion().bEligible);
	CHECK(!C.TryPromote());
	CHECK_EQ(C.Level, 1);
}

TEST(Career_BackgroundScoresAreNotEvidence)
{
	// A strong background still needs assessed work: starting scores carry no assessments.
	GameState S = NewGame("Test", EBackground::ExPolice, EDifficulty::Professional, 7);
	for (const std::string& M : LevelDef(1).RequiredMissions) S.Career.RecordMission(M, true, 0);
	S.Career.Competencies[static_cast<size_t>(ECompetency::IncidentResponse)].Score = 99;
	S.Career.Competencies[static_cast<size_t>(ECompetency::EvidenceReporting)].Score = 99;
	S.Career.Competencies[static_cast<size_t>(ECompetency::GuestServiceDeescalation)].Score = 99;
	CHECK(!S.Career.CheckPromotion().bEligible);
	CHECK_EQ(S.Career.Get(ECompetency::IncidentResponse).Assessments, 0);
}

TEST(Career_BackgroundsDiffer)
{
	for (int a = 0; a < BackgroundCount; ++a)
	{
		const BackgroundDef& A = Background(static_cast<EBackground>(a));
		int Sum = 0;
		for (int V : A.StartScores)
		{
			CHECK(V >= 0 && V <= 50);
			Sum += V;
		}
		CHECK_EQ(Sum, 255); // different strengths and gaps, same total
		for (int b = a + 1; b < BackgroundCount; ++b) CHECK(A.StartScores != Background(static_cast<EBackground>(b)).StartScores);
	}
}

TEST(Career_CrisisLevelsNeedACrisis)
{
	CareerState C;
	C.Level = 6;
	const CareerLevelDef& Def = LevelDef(6);
	for (const std::string& M : Def.RequiredMissions) C.RecordMission(M, true, 0);
	for (const CompetencyRequirement& R : Def.PromotionRequirements)
		for (int i = 0; i < Def.MinAssessmentsPerRequirement; ++i) C.ApplyAssessment({R.Competency, 100});
	CHECK(!C.CheckPromotion().bEligible);
	C.RecordMission(Def.CrisisPool.front(), true, 0);
	CHECK(C.TryPromote());
	CHECK_EQ(C.Level, 7);
}

TEST(Career_WeakCompetenceBlocksPromotionDespitePassedMissions)
{
	CareerState C;
	const CareerLevelDef& Def = LevelDef(1);
	for (const std::string& M : Def.RequiredMissions) C.RecordMission(M, true, 500);
	for (const CompetencyRequirement& R : Def.PromotionRequirements) C.ApplyAssessment({R.Competency, R.MinScore - 1});
	const PromotionCheck P = C.CheckPromotion();
	CHECK(!P.bEligible);
	CHECK_EQ(static_cast<int>(P.UnmetGates.size()), static_cast<int>(Def.PromotionRequirements.size()));
}

// Phase 0 form of the best-play test: strong work on every required mission and crisis promotes
// level by level from LP Officer to VP, on competence and missions alone (XP stays 0). The full
// campaign replay through mission data replaces this in Phase 2.
TEST(Career_BestPlayReachesVpOnCompetenceAlone)
{
	GameState S = NewGame("Best", EBackground::FrontOffice, EDifficulty::Professional, 1);
	int Promotions = 0;
	while (S.Career.Level < TopLevel)
	{
		const CareerLevelDef& Def = LevelDef(S.Career.Level);
		REQUIRE(!S.Career.CheckPromotion().bEligible); // nothing is free
		for (const std::string& M : Def.RequiredMissions)
		{
			for (const CompetencyRequirement& R : Def.PromotionRequirements) S.Career.ApplyAssessment({R.Competency, 92});
			S.Career.RecordMission(M, true, 0);
		}
		for (int i = 0; i < Def.RequiredCrises; ++i) S.Career.RecordMission(Def.CrisisPool[static_cast<size_t>(i)], true, 0);
		const PromotionCheck P = S.Career.CheckPromotion();
		for (const std::string& G : P.UnmetGates) std::printf("    level %d gate: %s\n", S.Career.Level, G.c_str());
		REQUIRE(S.Career.TryPromote());
		++Promotions;
	}
	CHECK_EQ(Promotions, 9);
	CHECK_EQ(S.Career.Xp, 0);
	CHECK(!S.Career.TryPromote());
}
