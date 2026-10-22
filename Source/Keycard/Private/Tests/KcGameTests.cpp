// Automation tests for the game layer through the UE subsystems (Keycard.*). Run: Scripts\RunUnrealTests.bat <UE dir>.
// The rules themselves are tested in Tests/Core; these check that the subsystems the HUD calls wire them up correctly.
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "KcAudioSubsystem.h"
#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcSaveSubsystem.h"
#include "KcVoice.h"

using namespace KcConvert;

namespace
{
constexpr EAutomationTestFlags GameFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::ProductFilter;
const TCHAR* const Slice[] = {TEXT("M001"), TEXT("M002"), TEXT("M003")};

struct FGameSession
{
	UGameInstance* Instance = nullptr;
	UKcGameSubsystem* Game = nullptr;
	UKcCampaignSubsystem* Campaign = nullptr;
	UKcSaveSubsystem* Saves = nullptr;

	// bNewGame false: no game is started (a new game autosaves, which would overwrite a slot the test means to load).
	explicit FGameSession(bool bNewGame = true)
	{
		Instance = NewObject<UGameInstance>(GEngine);
		Instance->AddToRoot();
		Instance->InitializeStandalone();
		Game = Instance->GetSubsystem<UKcGameSubsystem>();
		Campaign = Instance->GetSubsystem<UKcCampaignSubsystem>();
		Saves = Instance->GetSubsystem<UKcSaveSubsystem>();
		if (Saves) Saves->SetSlotName(TEXT("uetest")); // never the player's slots
		if (Game && bNewGame) Game->NewGame(EKcDifficulty::Professional);
	}

	~FGameSession()
	{
		UWorld* World = Instance->GetWorld();
		Instance->Shutdown();
		if (World)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		Instance->RemoveFromRoot();
	}

	// Plays one case the expert's way through the same calls the self-test and HUD use.
	bool PlayBest(FAutomationTestBase& T, const TCHAR* MissionId)
	{
		const kc::MissionDef* M = Campaign->Find(MissionId);
		if (!T.TestNotNull(FString::Printf(TEXT("%s exists"), MissionId), M)) return false;
		if (!T.TestTrue(FString::Printf(TEXT("%s starts"), MissionId), Campaign->StartCase(MissionId) == EKcCaseResult::Ok)) return false;
		FKcCaseCall Call;
		T.TestTrue(FString::Printf(TEXT("%s reaches the player as a call"), MissionId), Campaign->TakeCaseCall(Call) && Call.MissionId == MissionId && !Call.Text.IsEmpty());
		// The previous case's debrief stays reopenable; this case's rubric must not be shown before filing.
		const kc::MissionDebrief* Before = Campaign->LastDebrief();
		T.TestTrue(FString::Printf(TEXT("%s has no debrief before filing (rubric hidden)"), MissionId), !Before || Before->MissionId != M->Id);
		const EKcCaseResult R = Campaign->PlayBestAndFile(ToFString(M->ReportAt));
		if (!T.TestTrue(FString::Printf(TEXT("%s filed: %s"), MissionId, *UKcCampaignSubsystem::ResultText(R)), R == EKcCaseResult::Ok)) return false;
		const kc::MissionDebrief* D = Campaign->LastDebrief();
		return T.TestNotNull(TEXT("debrief"), D) && T.TestTrue(FString::Printf(TEXT("%s passed with the best play (score %d)"), MissionId, D->Score), D->bPassed && D->Score >= 90);
	}
};
} // namespace

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKcContentTest, "Keycard.Content.ShippedContentLoads", GameFlags)
bool FKcContentTest::RunTest(const FString& Parameters)
{
	FGameSession S;
	if (!TestNotNull(TEXT("campaign subsystem"), S.Campaign)) return false;
	for (const FString& E : S.Campaign->GetLoadErrors()) AddError(E);
	for (const TCHAR* Id : Slice) TestNotNull(FString::Printf(TEXT("%s loaded"), Id), S.Campaign->Find(Id));
	TestTrue(TEXT("cast loaded"), S.Campaign->GetWorldContent().People.People.size() >= 20u);
	TestTrue(TEXT("Field Manual loaded"), !S.Campaign->GetCards().empty());
	for (const kc::MissionDef& M : S.Campaign->GetCatalogue().Missions)
	{
		// Every place a step happens has a player-facing name (the HUD and notebook show it).
		for (const kc::ActionDef& A : M.Actions)
		{
			const FString Name = UKcCampaignSubsystem::LocationName(ToFString(A.At));
			TestTrue(FString::Printf(TEXT("%s/%s place '%s' has a name"), *ToFString(M.Id), *ToFString(A.Id), *ToFString(A.At)), !Name.IsEmpty() && !Name.Equals(ToFString(A.At), ESearchCase::CaseSensitive));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKcSliceTest, "Keycard.Campaign.SlicePromotesWithCeremony", GameFlags)
bool FKcSliceTest::RunTest(const FString& Parameters)
{
	FGameSession S;
	if (!TestNotNull(TEXT("game subsystem"), S.Game) || !TestNotNull(TEXT("campaign subsystem"), S.Campaign)) return false;
	TestEqual(TEXT("starts at level 1"), S.Game->GetLevel(), 1);
	TestTrue(TEXT("first case offered is M001"), S.Campaign->NextCase() && S.Campaign->NextCase()->Id == "M001");
	for (const TCHAR* Id : Slice)
	{
		if (!S.PlayBest(*this, Id)) return false;
	}
	TestEqual(TEXT("promoted to level 2 by evidence and passed cases"), S.Game->GetLevel(), 2);
	TestEqual(TEXT("ceremony pending for level 2"), S.Game->PeekCeremony(), 2);
	S.Game->CeremonyPlayed();
	TestEqual(TEXT("ceremony plays once"), S.Game->PeekCeremony(), 0);
	TestNull(TEXT("no case open after filing"), S.Campaign->Active());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKcCaseRulesTest, "Keycard.Campaign.StepsNeedThePlaceAndTheOrder", GameFlags)
bool FKcCaseRulesTest::RunTest(const FString& Parameters)
{
	FGameSession S;
	if (!TestNotNull(TEXT("campaign subsystem"), S.Campaign)) return false;
	TestTrue(TEXT("M001 starts"), S.Campaign->StartCase(TEXT("M001")) == EKcCaseResult::Ok);
	TestTrue(TEXT("a second case cannot start"), S.Campaign->StartCase(TEXT("M002")) != EKcCaseResult::Ok);
	const kc::MissionDef* M = S.Campaign->Active();
	if (!TestNotNull(TEXT("active case"), M) || !TestTrue(TEXT("best order"), !M->BestOrder.empty())) return false;
	const kc::ActionDef* First = nullptr;
	for (const kc::ActionDef& A : M->Actions)
	{
		if (A.Id == M->BestOrder[0]) First = &A;
	}
	if (!TestNotNull(TEXT("first step"), First)) return false;
	const FString Elsewhere = First->At == "lp_office" ? TEXT("lobby") : TEXT("lp_office");
	TestTrue(TEXT("a step done in the wrong place is refused"), S.Campaign->DoAction(ToFString(First->Id), Elsewhere) == EKcCaseResult::WrongPlace);
	TestTrue(TEXT("unknown step refused"), S.Campaign->DoAction(TEXT("no_such_step"), ToFString(First->At)) == EKcCaseResult::UnknownAction);
	TestTrue(TEXT("the step works where it belongs"), S.Campaign->DoAction(ToFString(First->Id), ToFString(First->At)) == EKcCaseResult::Ok);
	TestTrue(TEXT("and only once"), S.Campaign->DoAction(ToFString(First->Id), ToFString(First->At)) == EKcCaseResult::AlreadyDone);
	TestTrue(TEXT("an empty report is refused"), S.Campaign->FileReport(TMap<FString, TArray<FString>>(), ToFString(M->ReportAt)) != EKcCaseResult::Ok);
	TestNull(TEXT("rubric still hidden"), S.Campaign->LastDebrief());
	TestTrue(TEXT("the case can be set aside"), S.Campaign->AbandonCase() == EKcCaseResult::Ok && !S.Campaign->Active());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKcSaveTest, "Keycard.Save.RoundTripAfterPromotion", GameFlags)
bool FKcSaveTest::RunTest(const FString& Parameters)
{
	FString Path;
	{
		FGameSession S;
		if (!TestNotNull(TEXT("save subsystem"), S.Saves)) return false;
		for (const TCHAR* Id : Slice)
		{
			if (!S.PlayBest(*this, Id)) return false;
		}
		FString Error;
		if (!TestTrue(TEXT("saved: ") + Error, S.Saves->Save(Error))) return false;
		Path = S.Saves->GetSlotPath();
	}
	{
		FGameSession S(false);
		FString Message;
		const EKcLoadOutcome Outcome = S.Saves->Load(Message);
		TestTrue(TEXT("loaded: ") + Message, Outcome == EKcLoadOutcome::Ok || Outcome == EKcLoadOutcome::RecoveredFromBackup);
		TestEqual(TEXT("level kept"), S.Game->GetLevel(), 2);
		TestEqual(TEXT("ceremony still pending"), S.Game->PeekCeremony(), 2);
		for (const TCHAR* Id : Slice) TestTrue(FString::Printf(TEXT("%s recorded as passed"), Id), S.Game->GetState().Career.HasPassed(ToStd(Id)));
	}
	IFileManager::Get().Delete(*Path); // the test's own slot only
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FKcVoiceTest, "Keycard.Audio.VoiceLookupIsSafe", GameFlags)
bool FKcVoiceTest::RunTest(const FString& Parameters)
{
	// Same key as Scripts/BuildVoices.py; a line with no clip falls back to text-timed lip-sync, never a crash.
	TestEqual(TEXT("key"), ToFString(kc::VoiceLineKey("priya", "Two cases in. How are you?")), FString(TEXT("priya_4001ff99")));
	TestNull(TEXT("missing clip"), UKcAudioSubsystem::FindVoice(TEXT("nobody_00000000")));
	TestNull(TEXT("empty key"), UKcAudioSubsystem::FindVoice(FString()));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
