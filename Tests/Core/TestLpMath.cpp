#include "KcTest.h"

#include "KcLpMath.h"

#include <limits>

using namespace kc::lp;

// Worked examples match Docs/LpMethodology.md.
TEST(Lp_Shrink)
{
	CHECK_NEAR(ShrinkPercent(12000, 11520).Value, 4.0, 1e-9);
	CHECK_NEAR(ShrinkPercent(1000, 1050).Value, -5.0, 1e-9); // overage
	CHECK(!ShrinkPercent(0, 10).bValid);
	CHECK(!ShrinkPercent(100, -1).bValid);
	CHECK(!ShrinkPercent(std::numeric_limits<double>::quiet_NaN(), 1).bValid);
}

TEST(Lp_CostVariance)
{
	// Banquet liquor: sales 40,000, ideal cost 8,000 (20%), actual 9,400 (23.5%) -> 3.5 points.
	CHECK_NEAR(CostVariancePoints(9400, 8000, 40000).Value, 3.5, 1e-9);
	CHECK(!CostVariancePoints(1, 1, 0).bValid);
}

TEST(Lp_LossRate)
{
	CHECK_NEAR(LossRatePer1000RoomNights(18, 15000).Value, 1.2, 1e-9);
	CHECK(!LossRatePer1000RoomNights(1, 0).bValid);
}

TEST(Lp_ClaimReserveAndIncurred)
{
	ClaimEstimate E{6500, 12000, 4000, 2500};
	CHECK_NEAR(ClaimReserve(E).Value, 20000, 1e-9);
	CHECK_NEAR(ClaimIncurred(E).Value, 22500, 1e-9);
	E.PaidToDate = 30000; // overpaid estimate: reserve never negative
	CHECK_NEAR(ClaimReserve(E).Value, 0, 1e-9);
	CHECK(!ClaimReserve({-1, 0, 0, 0}).bValid);
}

TEST(Lp_IncidentCostKeepsTheDeductible)
{
	IncidentCostInputs In;
	In.UninsuredLoss = 300;
	In.ResponseHours = 6;
	In.HourlyRate = 35;
	In.Repairs = 450;
	In.GuestRecovery = 600;
	In.InsuredLoss = 8000;
	In.Deductible = 5000;
	CHECK_NEAR(IncidentCost(In).Value, 300 + 210 + 450 + 600 + 5000, 1e-9);
	CHECK_NEAR(InsurerRecovery(In).Value, 3000, 1e-9);
	In.InsuredLoss = 1200; // below the deductible: the hotel keeps all of it
	CHECK_NEAR(IncidentCost(In).Value, 300 + 210 + 450 + 600 + 1200, 1e-9);
	CHECK_NEAR(InsurerRecovery(In).Value, 0, 1e-9);
}

TEST(Lp_PatrolCompliance)
{
	CHECK_NEAR(PatrolCompliancePercent(57, 60).Value, 95.0, 1e-9);
	CHECK(!PatrolCompliancePercent(61, 60).bValid);
	CHECK(!PatrolCompliancePercent(0, 0).bValid);
}

TEST(Lp_AuditCriticalItemFailsWhateverTheScore)
{
	std::vector<AuditItem> Items = {{2, 2, 1, false, false}, {1, 2, 2, false, false}, {3, 3, 1, true, false}, {0, 5, 1, false, true}};
	AuditResult R = AuditScore(Items, 80);
	REQUIRE(R.bValid);
	CHECK_NEAR(R.ScorePercent, (2 + 2 + 3) / (2.0 + 4 + 3) * 100.0, 1e-9); // N/A item left out
	CHECK(!R.bPass);                                                     // 77.8 < 80
	Items[1].Points = 2;
	R = AuditScore(Items, 80);
	CHECK(R.bPass);
	Items[2].Points = 2; // critical item not fully met
	R = AuditScore(Items, 80);
	CHECK(R.ScorePercent > 80);
	CHECK(!R.bPass);
	CHECK_EQ(R.CriticalFailures, 1);
	CHECK(!AuditScore({{0, 1, 1, false, true}}, 80).bValid); // nothing applicable
	CHECK(!AuditScore({{3, 2, 1, false, false}}, 80).bValid); // points above max
}

TEST(Lp_Staffing)
{
	const double Hours = PostHoursPerWeek(24, 7, 2).Value;
	CHECK_NEAR(Hours, 336, 1e-9);
	CHECK_NEAR(FtesRequired(Hours, 1800).Value, 336 * 52 / 1800.0, 1e-9); // about 9.7
	CHECK_NEAR(StaffingCoveragePercent(302.4, 336).Value, 90.0, 1e-9);
	CHECK(!PostHoursPerWeek(25, 7, 1).bValid);
	CHECK(!FtesRequired(10, 0).bValid);
}

TEST(Lp_RekeyCost)
{
	RekeyInputs In{120, 18, 10, 42, 40, 3.5};
	CHECK_NEAR(RekeyCost(In).Value, 120 * (18 + 7) + 140, 1e-9);
	In.Doors = -1;
	CHECK(!RekeyCost(In).bValid);
}

TEST(Lp_AnnualisedLoss)
{
	CHECK_NEAR(AnnualisedLoss(4, 2500).Value, 10000, 1e-9);
	CHECK(!AnnualisedLoss(-1, 1).bValid);
}

TEST(Lp_ScoreByCloseness)
{
	CHECK_EQ(ScoreByCloseness(101, 100, 0.02), CloseScoreExact);
	CHECK_EQ(ScoreByCloseness(105, 100, 0.02), CloseScoreClose);
	CHECK_EQ(ScoreByCloseness(118, 100, 0.02), CloseScoreNear);
	CHECK_EQ(ScoreByCloseness(150, 100, 0.02), 0);
	CHECK_EQ(ScoreByCloseness(0.01, 0, 0.02), CloseScoreExact);
	CHECK_EQ(ScoreByCloseness(std::numeric_limits<double>::infinity(), 100, 0.02), 0);
}
