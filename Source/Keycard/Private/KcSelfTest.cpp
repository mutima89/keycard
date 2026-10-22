#include "KcSelfTest.h"

#include "Camera/PlayerCameraManager.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "KcCampaignSubsystem.h"
#include "KcAudioSubsystem.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcWalkthrough.h"
#include "UI/SKcCaseFiles.h"
#include "KcHUD.h"
#include "KcInteractable.h"
#include "KcLog.h"
#include "KcValtrenHQ.h"
#include "KcPerson.h"
#include "KcSaveSubsystem.h"
#include "RenderTimer.h"
#include "DynamicRHI.h"
#include "TimerManager.h"
#include "Containers/Ticker.h"
#include "UnrealClient.h"
#include "UObject/UObjectIterator.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/Pawn.h"

extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

namespace KcSelfTest
{
const TCHAR* const SlotName = TEXT("selftest");

FString RequestedMode()
{
	FString Mode;
	FParse::Value(FCommandLine::Get(), TEXT("KcSelfTest="), Mode);
	return Mode.ToLower();
}

bool IsRequested()
{
	return !RequestedMode().IsEmpty();
}

FKcSubmissionInput StrongAssessment()
{
	FKcSubmissionInput S;
	S.MissingInfo = {TEXT("compressor_failure_history"), TEXT("after_hours_escalation"), TEXT("alarm_threshold_config")};
	S.InherentLikelihood = 4;
	S.InherentImpact = 4;
	S.ControlRatings = {EKcControlRating::Ineffective, EKcControlRating::PartiallyEffective, EKcControlRating::Ineffective};
	S.ResidualLikelihood = 4;
	S.ResidualImpact = 3;
	S.TreatmentId = TEXT("repair_and_escalation");
	S.ActionOwnerId = TEXT("ops_site_manager");
	S.RationaleTagIds = {TEXT("alarm_ignored"), TEXT("service_overdue"), TEXT("penalty_exposure"), TEXT("outside_appetite")};
	S.RationaleText = TEXT("Controls exist on paper but are not operating; the alarm is silenced without action and servicing is overdue.");
	return S;
}

void PlayMission001(UKcGameSubsystem* Game)
{
	Game->TalkToManager();
	for (const FKcEvidenceView& E : Game->GetEvidence())
	{
		Game->ViewEvidence(E.Id);
	}
	Game->SubmitAssessment(StrongAssessment());
	Game->TalkToManager();
	Game->MarkDebriefReceived();
}

// Best answers for a campaign mission, read from the content (the self-test may see the rubric).
TArray<FKcMissionAnswer> BestAnswers(const kc::MissionDef& M)
{
	TArray<FKcMissionAnswer> Out;
	for (const kc::MissionQuestionDef& Q : M.Questions)
	{
		const kc::MissionAnswer Best = kc::BestAnswer(Q);
		FKcMissionAnswer A;
		A.QuestionId = KcConvert::ToFString(Q.Id);
		A.OptionIds = KcConvert::ToFStrings(Best.OptionIds);
		Out.Add(A);
	}
	return Out;
}

// Plays a campaign mission through the case files screen with the best answers.
EKcMissionResult PlayCaseBest(AKcHUD* Hud, const FString& MissionId)
{
	UKcCampaignSubsystem* Campaign = Hud->GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>();
	const kc::MissionDef* Def = Campaign->GetCatalogue().Find(KcConvert::ToStd(MissionId));
	if (!Def) return EKcMissionResult::UnknownMission;
	TSharedPtr<SKcCaseFiles> Files = Hud->OpenCaseFiles(MissionId); // starts the mission
	for (const FKcMissionEvidenceView& E : Campaign->GetMission(MissionId).Evidence) Campaign->ViewEvidence(MissionId, E.Id, E.At);
	for (const FKcMissionAnswer& A : BestAnswers(*Def))
	{
		Files->SetAnswerValues(A.QuestionId, A.OptionIds);
	}
	return Campaign->SubmitMission(MissionId, Files->CurrentAnswers(), KcConvert::ToFString(Def->PresentAt));
}

namespace
{
struct FReport
{
	TArray<FString> Lines;
	bool bPass = true;

	void Check(const FString& Name, bool bOk, const FString& Detail)
	{
		bPass &= bOk;
		Lines.Add(FString::Printf(TEXT("%s %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *Name, *Detail));
	}

	void Write(const FString& Mode, const FString& FileName) const
	{
		FString Text = FString::Printf(TEXT("%s\nmode=%s\n"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Mode);
		for (const FString& L : Lines) Text += L + TEXT("\n");
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / FileName);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogKeycard, Display, TEXT("KcSelfTest wrote %s:\n%s"), *Path, *Text);
	}
};

void CheckFinalState(FReport& R, UKcGameSubsystem* Game)
{
	const FKcCareerView Career = Game->GetCareer();
	const FKcRegisterEntryView Entry = Game->GetRegisterEntry(TEXT("R-014"));
	R.Check(TEXT("objective"), Game->IsDebriefed(), Game->GetCurrentObjectiveText());
	R.Check(TEXT("checkpoint"), Game->IsDebriefed(), Game->GetCheckpoint());
	R.Check(TEXT("career_level"), Career.Level == 2, FString::Printf(TEXT("%d %s"), Career.Level, *Career.Title));
	R.Check(TEXT("passed_missions"), Career.PassedMissions.Contains(TEXT("M001")), FString::Join(Career.PassedMissions, TEXT(",")));
	R.Check(TEXT("r014_treatment"), Entry.bFound && Entry.Treatment == TEXT("Reduce"),
		FString::Printf(TEXT("%s (owner %s, status %s)"), *Entry.Treatment, *Entry.ActionOwner, *Entry.Status));
	const FKcDebriefView Debrief = Game->GetDebrief();
	R.Check(TEXT("debrief"), Debrief.bAvailable && Debrief.bPassed, FString::Printf(TEXT("score %d, passed %d"), Debrief.OverallScore, Debrief.bPassed ? 1 : 0));
}

void CheckCampaign(FReport& R, UKcGameSubsystem* Game)
{
	UKcCampaignSubsystem* Campaign = Game->GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>();
	const TArray<FKcMissionSummary> Missions = Campaign->GetMissions(true);
	R.Check(TEXT("mission_data"), Missions.Num() == 52 && Campaign->GetAchievementDefs().size() == 50 && Campaign->GetLoadErrors().IsEmpty(),
		FString::Printf(TEXT("%d missions, %d achievements, %d problems"), Missions.Num(), static_cast<int32>(Campaign->GetAchievementDefs().size()), Campaign->GetLoadErrors().Num()));
	const FKcMissionSummary* M2 = Missions.FindByPredicate([](const FKcMissionSummary& M) { return M.Id == TEXT("M002"); });
	const FKcMissionDebriefView D = Campaign->GetDebrief(TEXT("M002"));
	R.Check(TEXT("m002_passed"), M2 && M2->bPassed && D.bAvailable && D.bPassed, FString::Printf(TEXT("score %d, attempts %d"), D.Score, M2 ? M2->Attempts : 0));
	const FKcMissionSummary* M3 = Missions.FindByPredicate([](const FKcMissionSummary& M) { return M.Id == TEXT("M003"); });
	R.Check(TEXT("m003_open"), M3 && M3->bAvailable, M3 ? FString::Join(M3->Reasons, TEXT(",")) : FString(TEXT("missing")));
	R.Check(TEXT("objective_next"), Game->GetCurrentObjectiveText().Contains(TEXT("Case files")), Game->GetCurrentObjectiveText());
	R.Check(TEXT("campaign_checkpoint"), Game->GetCheckpoint() == TEXT("M002:debriefed"), Game->GetCheckpoint());
	// The living world: content loaded, the first inbox messages delivered and kept across save/load, sound present.
	const FKcWorldView W = Campaign->GetWorldView();
	const TArray<FKcInboxMessageView> Inbox = Campaign->GetInbox();
	R.Check(TEXT("world_content"), Campaign->GetPeople().Num() >= 12 && W.CluesTotal == 10, FString::Printf(TEXT("%d people, %d clues"), Campaign->GetPeople().Num(), W.CluesTotal));
	R.Check(TEXT("inbox"), Inbox.Num() >= 2 && Inbox.ContainsByPredicate([](const FKcInboxMessageView& M) { return M.Subject == TEXT("Welcome to the team"); }),
		FString::Printf(TEXT("%d messages, %d awaiting reply, stress %d, share %.1f"), Inbox.Num(), W.AwaitingReply, W.Stress, W.ShareIndex));
	const UKcAudioSubsystem* Audio = Game->GetGameInstance()->GetSubsystem<UKcAudioSubsystem>();
	R.Check(TEXT("audio"), Audio && Audio->LoadedSoundCount() >= 20, FString::Printf(TEXT("%d sounds"), Audio ? Audio->LoadedSoundCount() : 0));
}

void RunPlay(FReport& R, AKcHUD* Hud, UWorld* World)
{
	UKcGameSubsystem* Game = Hud->Game();
	Hud->StartNewGame(EKcDifficulty::Professional);

	AKcNpc* Npc = nullptr;
	AKcWorkstation* Workstation = nullptr;
	TArray<AKcEvidenceProp*> Props;
	for (TActorIterator<AKcInteractable> It(World); It; ++It)
	{
		if (AKcNpc* N = Cast<AKcNpc>(*It)) Npc = N;
		else if (AKcWorkstation* W = Cast<AKcWorkstation>(*It)) Workstation = W;
		else if (AKcEvidenceProp* P = Cast<AKcEvidenceProp>(*It)) Props.Add(P);
	}
	R.Check(TEXT("level_interactables"), Npc && Workstation && Props.Num() == 5,
		FString::Printf(TEXT("npc %d, workstation %d, evidence props %d"), Npc ? 1 : 0, Workstation ? 1 : 0, Props.Num()));
	if (!Npc || !Workstation) return;

	Npc->Interact(Hud); // briefing: MeetManager + autosave
	Hud->CloseScreen();
	R.Check(TEXT("met_manager"), Game->HasMetManager(), Game->GetCheckpoint());

	Workstation->Interact(Hud);
	Hud->OpenEvidence(TEXT("incident_report")); // the workstation inbox button
	Hud->OpenEvidence(TEXT("risk_register_R014")); // the workstation register button
	for (AKcEvidenceProp* Prop : Props)
	{
		Prop->Interact(Hud);
	}
	Hud->CloseScreen();
	int32 Viewed = 0;
	for (const FKcEvidenceView& E : Game->GetEvidence()) Viewed += E.bViewed ? 1 : 0;
	R.Check(TEXT("evidence_viewed"), Viewed == 7, FString::Printf(TEXT("%d of 7"), Viewed));

	Workstation->Interact(Hud);
	Hud->OpenAssessment();
	const EKcSubmitResult Submit = Hud->SubmitAssessment(StrongAssessment()); // the form's Submit button
	R.Check(TEXT("submit"), Submit == EKcSubmitResult::Ok, UKcGameSubsystem::SubmitResultText(Submit));

	Npc->Interact(Hud); // debrief
	Hud->CloseScreen();
	CheckFinalState(R, Game);

	Workstation->Interact(Hud);
	const EKcMissionResult Case = PlayCaseBest(Hud, TEXT("M002")); // the case files Submit button
	R.Check(TEXT("m002_submit"), Case == EKcMissionResult::Ok, UKcCampaignSubsystem::ResultText(Case));
	Hud->CloseScreen();
	CheckCampaign(R, Game);

	FString SaveMessage;
	R.Check(TEXT("save"), Hud->SaveNow(SaveMessage), Hud->Saves()->GetSlotPath());
}

void RunResume(FReport& R, AKcHUD* Hud)
{
	FString Message;
	const EKcLoadOutcome Outcome = Hud->ContinueGame(Message); // the main menu Continue button
	R.Check(TEXT("load"), Outcome == EKcLoadOutcome::Ok, FString::Printf(TEXT("outcome %d %s %s"), static_cast<int32>(Outcome), *Message, *Hud->Saves()->GetSlotPath()));
	if (Outcome == EKcLoadOutcome::Ok || Outcome == EKcLoadOutcome::RecoveredFromBackup)
	{
		CheckFinalState(R, Hud->Game());
		CheckCampaign(R, Hud->Game());
	}
}
} // namespace

struct FTourState
{
	TArray<FString> Names;
	TArray<FTransform> Views;
	TArray<FString> Lines;
	int32 Index = 0;
	bool bCaptured = false;
	double SumMs = 0.0;
	FTimerHandle Timer;
};

static FString FileSafe(const FString& Name)
{
	FString Out;
	for (TCHAR C : Name) Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	return Out;
}

// Teleports the player through every room; each stop settles for 2.5 s, then records timings and a screenshot.
static void RunTour(UWorld* World, AKcHUD* Hud)
{
	APlayerController* PC = World->GetFirstPlayerController();
	AKcValtrenHQ* HQ = nullptr;
	for (TActorIterator<AKcValtrenHQ> It(World); It; ++It) HQ = *It;
	if (!PC || !HQ || !PC->GetPawn())
	{
		FFileHelper::SaveStringToFile(TEXT("FAIL\nno player or HQ\n"), *(FPaths::ProjectSavedDir() / TEXT("KcTour.txt")));
		FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
		return;
	}
	Hud->StartNewGame(EKcDifficulty::Professional);
	PC->SetIgnoreLookInput(true);
	PC->SetIgnoreMoveInput(true);
	TSharedRef<FTourState> State = MakeShared<FTourState>();
	HQ->GetTourViewpoints(State->Names, State->Views);
	if (FParse::Param(FCommandLine::Get(), TEXT("KcTourEggs")))
	{
		// Local check of the hidden credits; these close-ups are not published.
		auto View = [](const FVector& From, const FVector& To) { return FTransform((To - From).Rotation(), From); };
		State->Names = {TEXT("Egg_Certificate"), TEXT("Egg_Book"), TEXT("Egg_Plate")};
		State->Views = {View(FVector(70.0f, -1010.0f, 160.0f), FVector(8.0f, -1010.0f, 150.0f)),
			View(FVector(2085.0f, 654.0f, 165.0f), FVector(2030.0f, 654.0f, 153.0f)),
			View(FVector(3282.0f, -850.0f, 165.0f), FVector(3282.0f, -770.0f, 74.0f))};
	}
	const FIntPoint Size = World->GetGameViewport() ? World->GetGameViewport()->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	State->Lines.Add(FString::Printf(TEXT("resolution=%dx%d"), Size.X, Size.Y));

	auto Place = [PC](const FTransform& View)
	{
		PC->GetPawn()->SetActorLocation(View.GetLocation() - FVector(0.0f, 0.0f, 70.0f), false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(View.Rotator());
		if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame(); // no motion blur from the jump
	};
	Place(State->Views[0]);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	World->GetTimerManager().SetTimer(State->Timer, [StateRef = State, WeakWorld, Place]()
	{
		// Clearing this timer below destroys the lambda and its captures, so work on local copies.
		const TSharedRef<FTourState> State = StateRef;
		const TWeakObjectPtr<UWorld> TourWorld = WeakWorld;
		const auto PlaceAt = Place;
		if (!TourWorld.IsValid()) return;
		// Alternate ticks: capture the current view, then move on the next tick, so the screenshot
		// (taken at the end of the frame) always shows the view it is named after.
		if (State->bCaptured)
		{
			State->bCaptured = false;
			if (++State->Index < State->Views.Num())
			{
				PlaceAt(State->Views[State->Index]);
				return;
			}
		}
		else
		{
			State->bCaptured = true;
		}
		const int32 I = State->Index;
		if (I >= State->Views.Num())
		{
			TourWorld->GetTimerManager().ClearTimer(State->Timer);
			const double AvgMs = State->SumMs / State->Views.Num();
			FString Text = FString::Printf(TEXT("PASS\nmode=tour\naverage_frame_ms=%.2f average_fps=%.1f\n"), AvgMs, AvgMs > 0.0 ? 1000.0 / AvgMs : 0.0);
			for (const FString& L : State->Lines) Text += L + TEXT("\n");
			const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KcTour.txt"));
			FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			UE_LOG(LogKeycard, Display, TEXT("KcSelfTest wrote %s:\n%s"), *Path, *Text);
			FTimerHandle Quit;
			TourWorld->GetTimerManager().SetTimer(Quit, []() { FPlatformMisc::RequestExit(false, TEXT("KcSelfTest")); }, 1.0f, false);
			return;
		}
		const double Gpu = FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());
		State->Lines.Add(FString::Printf(TEXT("%s: fps=%.1f frame_ms=%.2f game_ms=%.2f render_ms=%.2f gpu_ms=%.2f"), *State->Names[I], GAverageFPS, GAverageMS,
			FPlatformTime::ToMilliseconds(GGameThreadTime), FPlatformTime::ToMilliseconds(GRenderThreadTime), Gpu));
		State->SumMs += GAverageMS;
		FScreenshotRequest::RequestScreenshot(TEXT("Tour_") + FileSafe(State->Names[I]), false, false);
	}, 2.0f, true);
}

// Opens every screen through the same HUD calls the UI uses and captures each with the UI visible
// (Saved/Screenshots/<platform>/Ui_<n>_<name>.png). Uses the self-test save slot.
static void RunUiTour(UWorld* World, AKcHUD* Hud)
{
	struct FStep
	{
		FString Name;
		TFunction<void(AKcHUD*)> Open;
	};
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	Steps->Add({TEXT("MainMenu"), [](AKcHUD* H) { H->ShowMainMenu(); }});
	Steps->Add({TEXT("Difficulty"), [](AKcHUD* H) { H->ShowDifficultyPicker(); }});
	Steps->Add({TEXT("Hud"), [](AKcHUD* H) { H->StartNewGame(EKcDifficulty::Professional); }});
	Steps->Add({TEXT("Briefing"), [](AKcHUD* H) { H->TalkToManager(); }});
	Steps->Add({TEXT("Workstation"), [](AKcHUD* H) { H->OpenWorkstation(); }});
	Steps->Add({TEXT("Document"), [](AKcHUD* H) { H->OpenEvidence(TEXT("risk_register_R014")); }});
	Steps->Add({TEXT("Assessment"), [](AKcHUD* H) { H->OpenAssessment(); }});
	Steps->Add({TEXT("Journal"), [](AKcHUD* H) { H->CloseScreen(); H->OpenJournal(); }});
	Steps->Add({TEXT("Pause"), [](AKcHUD* H) { H->CloseScreen(); H->OpenPauseMenu(); }});
	Steps->Add({TEXT("Settings"), [](AKcHUD* H) { H->OpenSettings([H]() { H->CloseScreen(); }); }});
	Steps->Add({TEXT("Credits"), [](AKcHUD* H) { H->OpenCredits([H]() { H->CloseScreen(); }); }});
	const TCHAR* TabNames[] = {TEXT("Register"), TEXT("Kri"), TEXT("Loss"), TEXT("CostBenefit"), TEXT("Sensitivity"), TEXT("Stress"), TEXT("MonteCarlo"), TEXT("Actions"), TEXT("Report")};
	for (int32 Tab = 0; Tab < UE_ARRAY_COUNT(TabNames); ++Tab)
	{
		Steps->Add({FString(TEXT("Tools_")) + TabNames[Tab], [Tab](AKcHUD* H) { H->CloseScreen(); H->OpenToolHub(Tab); }});
	}
	Steps->Add({TEXT("Debrief"), [](AKcHUD* H)
	{
		H->CloseScreen();
		for (const FKcEvidenceView& E : H->Game()->GetEvidence()) H->Game()->ViewEvidence(E.Id);
		H->SubmitAssessment(StrongAssessment());
		H->TalkToManager();
	}});
	Steps->Add({TEXT("CaseFiles"), [](AKcHUD* H) { H->CloseScreen(); H->Game()->MarkDebriefReceived(); H->OpenCaseFiles(); }});
	Steps->Add({TEXT("CaseMission"), [](AKcHUD* H) { H->OpenCaseFiles(TEXT("M002")); }});
	Steps->Add({TEXT("CaseDebrief"), [](AKcHUD* H) { PlayCaseBest(H, TEXT("M002")); H->OpenCaseFiles(TEXT("M002"), true); }});
	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<bool> bOpened = MakeShared<bool>(false);
	TWeakObjectPtr<AKcHUD> WeakHud(Hud);
	// Core ticker, not a world timer: menus pause the world, and world timers stop while paused.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, Index, bOpened, WeakHud](float)
	{
		if (!WeakHud.IsValid()) return false;
		if (*Index >= Steps->Num())
		{
			FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
			return false;
		}
		if (!*bOpened)
		{
			(*Steps)[*Index].Open(WeakHud.Get());
			*bOpened = true;
			return true;
		}
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Ui_%02d_%s"), *Index, *(*Steps)[*Index].Name), true, false);
		*bOpened = false;
		++*Index;
		return true;
	}), 0.8f);
}

// Shoots the walk-around case markers: M002 in its four rooms, M006 in the training area (a desk document and
// Grace) and M009 in the workspace (two reading stands). Earlier cases are played with the best answers to open
// the later ones. Screenshots go to Saved/Screenshots/<platform>/Case_<n>_<name>.png; KcCases.txt lists the markers.
static void RunCaseTour(UWorld* World, AKcHUD* Hud)
{
	struct FStep
	{
		FString Name;
		TFunction<void(AKcHUD*, TArray<FString>&)> Setup;
		FTransform View;
	};
	auto View = [](const FVector& From, const FVector& To) { return FTransform((To - From).Rotation(), From); };
	auto Open = [](const FString& Id, TArray<FString> Before)
	{
		return [Id, Before](AKcHUD* H, TArray<FString>& Lines)
		{
			for (const FString& B : Before) Lines.Add(FString::Printf(TEXT("%s played: %s"), *B, *UKcCampaignSubsystem::ResultText(PlayCaseBest(H, B))));
			H->CloseScreen();
			const EKcMissionResult R = H->GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>()->StartMission(Id);
			Lines.Add(FString::Printf(TEXT("%s started: %s"), *Id, *UKcCampaignSubsystem::ResultText(R)));
		};
	};
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	Steps->Add({TEXT("M002_Meeting"), Open(TEXT("M002"), {}), View(FVector(3150.0f, 1100.0f, 185.0f), FVector(3700.0f, 580.0f, 80.0f))});
	Steps->Add({TEXT("M002_Monitoring"), nullptr, View(FVector(1600.0f, -300.0f, 180.0f), FVector(1150.0f, -620.0f, 90.0f))});
	Steps->Add({TEXT("M002_Reception"), nullptr, View(FVector(300.0f, 400.0f, 180.0f), FVector(590.0f, 995.0f, 100.0f))});
	Steps->Add({TEXT("M002_Manager"), nullptr, View(FVector(2750.0f, 350.0f, 180.0f), FVector(2400.0f, 850.0f, 100.0f))});
	Steps->Add({TEXT("M006_Training"), Open(TEXT("M006"), {TEXT("M002"), TEXT("M003"), TEXT("M004"), TEXT("M005")}),
		View(FVector(850.0f, -1050.0f, 190.0f), FVector(420.0f, -470.0f, 90.0f))});
	Steps->Add({TEXT("M009_Workspace"), Open(TEXT("M009"), {TEXT("M006"), TEXT("M007"), TEXT("M008")}),
		View(FVector(1850.0f, 1050.0f, 190.0f), FVector(1250.0f, 560.0f, 80.0f))});

	Hud->StartNewGame(EKcDifficulty::Professional);
	PlayMission001(Hud->Game());
	TSharedRef<TArray<FString>> Lines = MakeShared<TArray<FString>>();
	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<bool> bPlaced = MakeShared<bool>(false);
	TSharedRef<bool> bSetUp = MakeShared<bool>(false);
	TSharedRef<int32> Settle = MakeShared<int32>(0);
	TWeakObjectPtr<AKcHUD> WeakHud(Hud);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	// Core ticker: the case files screen pauses the world while it is open.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, Lines, Index, bPlaced, bSetUp, Settle, WeakHud, WeakWorld](float)
	{
		if (!WeakHud.IsValid() || !WeakWorld.IsValid()) return false;
		APlayerController* PC = WeakWorld->GetFirstPlayerController();
		if (*Index >= Steps->Num() || !PC || !PC->GetPawn())
		{
			FString Text = TEXT("mode=cases\n");
			for (const FString& L : *Lines) Text += L + TEXT("\n");
			FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("KcCases.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
			return false;
		}
		const FStep& S = (*Steps)[*Index];
		if (S.Setup && !*bSetUp)
		{
			S.Setup(WeakHud.Get(), *Lines);
			*bSetUp = true;
			*Settle = 2;
			return true;
		}
		// Promotions from the cases just played queue boardroom ceremonies that hold the camera; let them finish.
		if (WeakHud->IsCeremonyPlaying())
		{
			*Settle = 2;
			return true;
		}
		if (*Settle > 0)
		{
			--*Settle;
			return true;
		}
		if (!*bPlaced)
		{
			PC->SetIgnoreLookInput(true);
			PC->SetIgnoreMoveInput(true);
			PC->GetPawn()->SetActorLocation(S.View.GetLocation() - FVector(0.0f, 0.0f, 70.0f), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(S.View.Rotator());
			if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			*bPlaced = true;
			return true;
		}
		for (TActorIterator<AKcCaseMarker> It(WeakWorld.Get()); It; ++It)
		{
			const FVector L = It->GetActorLocation();
			Lines->Add(FString::Printf(TEXT("%s: %s '%s' at %s (%.0f, %.0f, %.0f)"), *S.Name, *It->MissionId, *It->Title, *It->Where, L.X, L.Y, L.Z));
		}
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Case_%02d_%s"), *Index, *S.Name), false, false);
		*bPlaced = false;
		*bSetUp = false;
		++*Index;
		return true;
	}), 2.0f);
}

// The office's people (Docs/GameDesign.md "People and physical work"): Priya talking and reacting at
// conversation distance, the meeting-room table, a colleague walking up with a message and the corridor
// with commuters. Screenshots go to Saved/Screenshots/<platform>/Person_<n>_<name>.png; KcPeople.txt lists
// every person with whether their face shapes (visemes) loaded.
static void RunPeopleTour(UWorld* World, AKcHUD* Hud)
{
	struct FStep
	{
		FString Name;
		FTransform View;
		float Wait;  // seconds between the setup and the shot
		TFunction<void(AKcHUD*, TArray<FString>&)> Setup;
		int32 WalkShots = 0; // > 0: a burst of shots 0.15 s apart of a walking colleague from a fixed camera
	};
	auto View = [](const FVector& From, const FVector& To) { return FTransform((To - From).Rotation(), From); };
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	const FTransform PriyaView = View(FVector(2330.0f, 735.0f, 165.0f), FVector(2330.0f, 880.0f, 158.0f));
	Steps->Add({TEXT("PriyaTalking"), PriyaView, 1.2f, [](AKcHUD* H, TArray<FString>&) { H->Speak(TEXT("priya"), TEXT("Morning. Before anything else, open the register and look at R-014 with me.")); }});
	Steps->Add({TEXT("PriyaPleased"), PriyaView, 1.0f, [](AKcHUD* H, TArray<FString>&) { H->Speak(TEXT("priya"), FString(), 1); }});
	Steps->Add({TEXT("PriyaUnhappy"), PriyaView, 1.4f, [](AKcHUD* H, TArray<FString>&) { H->Speak(TEXT("priya"), FString(), -1); }});
	Steps->Add({TEXT("Meeting"), View(FVector(3150.0f, 1050.0f, 175.0f), FVector(3400.0f, 700.0f, 100.0f)), 0.5f, nullptr});
	Steps->Add({TEXT("Approach"), View(FVector(1000.0f, 0.0f, 165.0f), FVector(2500.0f, 0.0f, 150.0f)), 14.0f, [](AKcHUD* H, TArray<FString>& Lines)
	{
		for (TActorIterator<AKcValtrenHQ> It(H->GetWorld()); It; ++It)
		{
			const bool bSent = It->SendColleague(TEXT("callum"), [](UKcPersonComponent* P) { P->Say(TEXT("Have you got a minute?")); }) != nullptr;
			Lines.Add(FString::Printf(TEXT("approach: callum sent=%d"), bSent ? 1 : 0));
		}
	}});
	Steps->Add({TEXT("Corridor"), View(FVector(150.0f, 0.0f, 165.0f), FVector(4000.0f, 0.0f, 120.0f)), 8.0f, nullptr});
	Steps->Add({TEXT("Walk"), FTransform::Identity, 0.0f, nullptr, 12});

	Hud->StartNewGame(EKcDifficulty::Professional);
	PlayMission001(Hud->Game());
	Hud->CloseScreen();
	TSharedRef<TArray<FString>> Lines = MakeShared<TArray<FString>>();
	TSharedRef<int32> Index = MakeShared<int32>(0);
	TSharedRef<double> ShootAt = MakeShared<double>(0.0);
	TWeakObjectPtr<AKcHUD> WeakHud(Hud);
	TWeakObjectPtr<UWorld> WeakWorld(World);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Steps, Lines, Index, ShootAt, WeakHud, WeakWorld](float)
	{
		if (!WeakHud.IsValid() || !WeakWorld.IsValid()) return false;
		APlayerController* PC = WeakWorld->GetFirstPlayerController();
		const double Now = FPlatformTime::Seconds();
		if (*Index >= Steps->Num() || !PC || !PC->GetPawn())
		{
			FString Text = TEXT("mode=people\n");
			int32 People = 0, WithFaces = 0;
			for (TObjectIterator<UKcPersonComponent> It; It; ++It)
			{
				if (It->GetWorld() != WeakWorld.Get() || !It->GetSkinnedAsset()) continue;
				++People;
				if (It->HasFaceShapes()) ++WithFaces;
				const FVector L = It->GetComponentLocation();
				Text += FString::Printf(TEXT("person '%s' mesh=%s faces=%d walking=%d at (%.0f, %.0f)\n"), *It->GetPersonId(), *It->GetSkinnedAsset()->GetName(),
					It->HasFaceShapes() ? 1 : 0, It->IsWalking() ? 1 : 0, L.X, L.Y);
			}
			Text += FString::Printf(TEXT("people=%d with_face_shapes=%d\n"), People, WithFaces);
			for (const FString& L : *Lines) Text += L + TEXT("\n");
			FFileHelper::SaveStringToFile(Text, *(FPaths::ProjectSavedDir() / TEXT("KcPeople.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
			return false;
		}
		if (WeakHud->IsCeremonyPlaying()) return true;
		const FStep& S = (*Steps)[*Index];
		if (S.WalkShots > 0)
		{
			// Wait (up to 40 s) for someone setting off, find their heading, then shoot them from a fixed camera.
			static TWeakObjectPtr<UKcPersonComponent> Walker;
			static FVector Start;
			static float StartSpeed = 0.0f;
			static bool bPlaced = false;
			static int32 Shot = 0;
			static double Deadline = 0.0, BurstStart = 0.0;
			static uint64 BurstFrame = 0;
			if (Deadline == 0.0) Deadline = Now + 40.0;
			if (!Walker.IsValid())
			{
				for (TObjectIterator<UKcPersonComponent> It; It; ++It)
				{
					if (It->GetWorld() == WeakWorld.Get() && It->IsWalking() && It->GetWalkSpeed() < 80.0f) { Walker = *It; break; }
				}
				if (!Walker.IsValid())
				{
					if (Now < Deadline) return true;
					Lines->Add(TEXT("walk: no one set off within 40 s"));
					++*Index;
					return true;
				}
				Start = Walker->GetComponentLocation();
				StartSpeed = Walker->GetWalkSpeed();
				*ShootAt = Now + 0.4;
				return true;
			}
			if (Now < *ShootAt) return true;
			if (!bPlaced && (!Walker->IsWalking() || Walker->GetWalkSpeed() <= StartSpeed))
			{
				Walker.Reset(); // slowing down to arrive, not setting off: look again
				return true;
			}
			if (!bPlaced)
			{
				const FVector L = Walker->GetComponentLocation();
				const FVector Dir = (L - Start).GetSafeNormal2D();
				const FVector Side(-Dir.Y, Dir.X, 0.0f);
				const FVector Camera = L + Dir * 300.0f + Side * 260.0f + FVector(0.0f, 0.0f, 150.0f);
				PC->SetIgnoreLookInput(true);
				PC->SetIgnoreMoveInput(true);
				PC->GetPawn()->SetActorLocation(Camera - FVector(0.0f, 0.0f, 70.0f), false, nullptr, ETeleportType::TeleportPhysics);
				// One fixed aim along the path, so the floor is a still reference (no blur from turning).
				PC->SetControlRotation((L + Dir * 120.0f + FVector(0.0f, 0.0f, 70.0f) - Camera).Rotation());
				if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
				Lines->Add(FString::Printf(TEXT("walk: '%s' cruise=%.1f cm/s, heading (%.2f, %.2f)"), *Walker->GetPersonId(), Walker->GetCruiseSpeed(), Dir.X, Dir.Y));
				bPlaced = true;
				BurstStart = Now;
				BurstFrame = GFrameCounter;
				*ShootAt = Now + 1.5; // frames before the first shot measure the frame rate with the walker on screen
				return true;
			}
			const FVector L = Walker->GetComponentLocation();
			if (Shot == 0)
			{
				Lines->Add(FString::Printf(TEXT("walk: %.1f fps over %.2f s with the walker on screen (before screenshots)"),
					(GFrameCounter - BurstFrame) / FMath::Max(Now - BurstStart, 0.01), Now - BurstStart));
			}
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Person_%02d_Walk_%02d"), *Index, Shot), false, false);
			Lines->Add(FString::Printf(TEXT("walk shot %d: t=%.2f at (%.0f, %.0f) yaw=%.0f speed=%.1f walking=%d"), Shot, Now - BurstStart, L.X, L.Y,
				Walker->GetComponentRotation().Yaw, Walker->GetWalkSpeed(), Walker->IsWalking() ? 1 : 0));
			*ShootAt = Now + 0.15;
			if (++Shot >= S.WalkShots)
			{
				*ShootAt = 0.0;
				++*Index;
			}
			return true;
		}
		if (*ShootAt <= 0.0)
		{
			PC->SetIgnoreLookInput(true);
			PC->SetIgnoreMoveInput(true);
			PC->GetPawn()->SetActorLocation(S.View.GetLocation() - FVector(0.0f, 0.0f, 70.0f), false, nullptr, ETeleportType::TeleportPhysics);
			PC->SetControlRotation(S.View.Rotator());
			if (PC->PlayerCameraManager) PC->PlayerCameraManager->SetGameCameraCutThisFrame();
			if (S.Setup) S.Setup(WeakHud.Get(), *Lines);
			*ShootAt = Now + S.Wait + 0.5;
			return true;
		}
		if (Now < *ShootAt) return true;
		FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Person_%02d_%s"), *Index, *S.Name), false, false);
		*ShootAt = 0.0;
		++*Index;
		return true;
	}), 0.1f);
}

void Run(UWorld* World)
{
	const FString Mode = RequestedMode();
	FReport R;
	APlayerController* PC = World->GetFirstPlayerController();
	AKcHUD* Hud = PC ? Cast<AKcHUD>(PC->GetHUD()) : nullptr;
	R.Check(TEXT("hud"), Hud != nullptr, Hud ? Hud->GetName() : FString(TEXT("missing")));
	if (Hud && Mode == TEXT("ui"))
	{
		Hud->Saves()->SetSlotName(SlotName);
		RunUiTour(World, Hud);
		return;
	}
	if (Hud && (Mode == TEXT("walkthrough") || Mode == TEXT("walkthrough2")))
	{
		Hud->Saves()->SetSlotName(TEXT("walkthrough"));
		KcWalkthrough::Start(World, Hud, Mode == TEXT("walkthrough") ? 1 : 2);
		return;
	}
	if (Hud && Mode == TEXT("tour"))
	{
		RunTour(World, Hud);
		return;
	}
	if (Hud && Mode == TEXT("people"))
	{
		Hud->Saves()->SetSlotName(TEXT("selftest_people"));
		RunPeopleTour(World, Hud);
		return;
	}
	if (Hud && Mode == TEXT("cases"))
	{
		Hud->Saves()->SetSlotName(TEXT("selftest_cases"));
		RunCaseTour(World, Hud);
		return;
	}
	if (Hud)
	{
		Hud->Saves()->SetSlotName(SlotName);
		if (Mode == TEXT("play")) RunPlay(R, Hud, World);
		else if (Mode == TEXT("resume")) RunResume(R, Hud);
		else R.Check(TEXT("mode"), false, TEXT("expected play or resume"));
	}
	R.Write(Mode, Mode == TEXT("resume") ? TEXT("KcSelfTest.txt") : TEXT("KcSelfTest_play.txt"));
	FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
}
} // namespace KcSelfTest
