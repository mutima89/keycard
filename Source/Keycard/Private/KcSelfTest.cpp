#include "KcSelfTest.h"

#include "Camera/PlayerCameraManager.h"
#include "Containers/Ticker.h"
#include "DynamicRHI.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "KcAudioSubsystem.h"
#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcHUD.h"
#include "KcHotel.h"
#include "KcInteractable.h"
#include "KcLog.h"
#include "KcPerson.h"
#include "KcSaveSubsystem.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "RenderTimer.h"
#include "TimerManager.h"
#include "UnrealClient.h"

extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

using KcConvert::ToFString;
using KcConvert::ToStd;

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

void FillBestReport(AKcHUD* Hud)
{
	UKcCampaignSubsystem* C = Hud->Campaign();
	if (!C) return;
	for (const kc::QuestionDef* Q : C->ReportQuestions())
	{
		C->Draft().Add(ToFString(Q->Id), KcConvert::ToFStrings(kc::BestAnswer(*Q).OptionIds));
	}
}

int32 AnswerOpenQuestions(AKcHUD* Hud)
{
	UKcCampaignSubsystem* C = Hud->Campaign();
	int32 N = 0;
	if (!C) return N;
	for (const kc::LocationDef& L : kc::KnownLocations())
	{
		for (const kc::QuestionDef* Q : C->QuestionsAt(ToFString(L.Id), FString()))
		{
			if (C->AnswerInPerson(ToFString(Q->Id), KcConvert::ToFStrings(kc::BestAnswer(*Q).OptionIds), ToFString(Q->At)) == EKcCaseResult::Ok) ++N;
		}
	}
	return N;
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

	void Note(const FString& Line) { Lines.Add(TEXT("     ") + Line); }

	void Write(const FString& Mode, const FString& FileName) const
	{
		FString Text = FString::Printf(TEXT("%s\nmode=%s\n"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Mode);
		for (const FString& L : Lines) Text += L + TEXT("\n");
		const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / FileName);
		FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		UE_LOG(LogKeycard, Display, TEXT("KcSelfTest wrote %s:\n%s"), *Path, *Text);
	}
};

// Who or what in the hotel offers this action right now (a person, a door, a case point), or "".
FString OfferedBy(UWorld* World, AKcHUD* Hud, const kc::ActionDef& A)
{
	UKcCampaignSubsystem* C = Hud->Campaign();
	for (TActorIterator<AKcPersonActor> It(World); It; ++It)
	{
		if (C && C->ActionsAt(It->Location, It->PersonId).Contains(&A)) return FString::Printf(TEXT("person %s at %s"), *It->PersonId, *It->Location);
	}
	for (TActorIterator<AKcCasePoint> It(World); It; ++It)
	{
		if (It->Work().Contains(&A)) return FString::Printf(TEXT("case point '%s' at %s"), *It->Title, *It->Location);
	}
	return FString();
}

bool PersonPresent(UWorld* World, const std::string& PersonId, const std::string& Location)
{
	for (TActorIterator<AKcPersonActor> It(World); It; ++It)
	{
		if (It->PersonId == ToFString(PersonId) && It->Location == ToFString(Location)) return true;
	}
	for (TActorIterator<AKcCasePoint> It(World); It; ++It)
	{
		if (It->DoorPersonId == ToFString(PersonId)) return true;
	}
	return false;
}

// Steps run one per tick of the core ticker (screens pause nothing, but menus can pause the world).
// A step returns false to be called again on the next tick (waiting for the hotel or a ceremony).
struct FScript
{
	TArray<TFunction<bool()>> Steps;
	int32 Index = 0;
	int32 Waited = 0;
	TFunction<void()> OnEnd;
};

void RunScript(TSharedRef<FScript> Script, float Interval)
{
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Script](float)
	{
		if (Script->Index >= Script->Steps.Num())
		{
			if (Script->OnEnd) Script->OnEnd();
			return false;
		}
		if (Script->Steps[Script->Index]())
		{
			++Script->Index;
			Script->Waited = 0;
		}
		else if (++Script->Waited > 200) // a step that never completes fails the run instead of hanging it
		{
			++Script->Index;
			Script->Waited = 0;
		}
		return true;
	}), Interval);
}

void AddCase(TSharedRef<FScript> S, TSharedRef<FReport> R, UWorld* World, AKcHUD* Hud, const FString& MissionId)
{
	TWeakObjectPtr<UWorld> W(World);
	TWeakObjectPtr<AKcHUD> H(Hud);
	// Sign on at the LP office terminal, as a player does.
	S->Steps.Add([R, W, H, MissionId]()
	{
		if (!H.IsValid()) return true;
		AKcReportTerminal* Terminal = nullptr;
		for (TActorIterator<AKcReportTerminal> It(W.Get()); It; ++It) Terminal = *It;
		R->Check(MissionId + TEXT("_terminal"), Terminal != nullptr, Terminal ? Terminal->Location : FString(TEXT("missing")));
		if (Terminal) Terminal->Interact(H.Get()); // the shift board
		const kc::MissionDef* Next = H->Campaign()->NextCase();
		R->Check(MissionId + TEXT("_next"), Next && ToFString(Next->Id) == MissionId, Next ? ToFString(Next->Id) : FString(TEXT("none")));
		const EKcCaseResult Start = H->StartCase(MissionId); // the shift board's Sign on button
		R->Check(MissionId + TEXT("_start"), Start == EKcCaseResult::Ok, UKcCampaignSubsystem::ResultText(Start));
		return true;
	});
	// The case call plays by itself once the screen is clear.
	S->Steps.Add([R, H, MissionId]()
	{
		if (!H.IsValid()) return true;
		if (!H->IsScreenOpen()) return false;
		R->Check(MissionId + TEXT("_call"), true, H->Campaign()->Objective());
		H->CloseScreen();
		return true;
	});
	// The expert's fieldwork, each step taken where the hotel offers it.
	const kc::MissionDef* M = Hud->Campaign()->Find(MissionId);
	for (const std::string& ActionId : M ? M->BestOrder : std::vector<std::string>())
	{
		const kc::ActionDef* A = M->FindAction(ActionId);
		if (!A) continue;
		TSharedRef<int32> Tries = MakeShared<int32>(0);
		S->Steps.Add([R, W, H, A, MissionId, Tries]()
		{
			if (!H.IsValid()) return true;
			UKcCampaignSubsystem* C = H->Campaign();
			// The hotel refreshes people and doors twice a second: give it a moment before calling it missing.
			const FString By = OfferedBy(W.Get(), H.Get(), *A);
			if (By.IsEmpty() && ++*Tries < 4) return false;
			// Questions open now are put to people the player can reach.
			for (const kc::LocationDef& L : kc::KnownLocations())
			{
				for (const kc::QuestionDef* Q : C->QuestionsAt(ToFString(L.Id), FString()))
				{
					R->Check(MissionId + TEXT("_ask_") + ToFString(Q->Id), PersonPresent(W.Get(), Q->Person, Q->At), ToFString(Q->Person) + TEXT(" at ") + ToFString(Q->At));
				}
			}
			AnswerOpenQuestions(H.Get());
			R->Check(MissionId + TEXT("_world_") + ToFString(A->Id), !By.IsEmpty(), By.IsEmpty() ? FString(TEXT("nothing in the hotel offers it")) : By);
			const EKcCaseResult Done = H->DoAction(*A);
			R->Check(MissionId + TEXT("_do_") + ToFString(A->Id), Done == EKcCaseResult::Ok, UKcCampaignSubsystem::ResultText(Done));
			H->CloseScreen();
			return true;
		});
	}
	// The report, filed at the terminal.
	S->Steps.Add([R, H, MissionId]()
	{
		if (!H.IsValid()) return true;
		AnswerOpenQuestions(H.Get());
		UKcCampaignSubsystem* C = H->Campaign();
		const FString Where = ToFString(C->Active() ? C->Active()->ReportAt : std::string("lp_office"));
		H->OpenTerminal(Where);
		H->OpenReport(Where);
		FillBestReport(H.Get());
		const EKcCaseResult Filed = H->FileReport(Where); // the form's File button
		R->Check(MissionId + TEXT("_file"), Filed == EKcCaseResult::Ok, UKcCampaignSubsystem::ResultText(Filed));
		const kc::MissionDebrief* D = C->LastDebrief();
		R->Check(MissionId + TEXT("_debrief"), D && D->bPassed && D->Score >= 90 && H->IsScreenOpen(),
			D ? FString::Printf(TEXT("score %d, passed %d, cards %d, promoted %d"), D->Score, D->bPassed ? 1 : 0, static_cast<int32>(D->CardsEarned.size()), D->bPromoted ? 1 : 0)
			  : FString(TEXT("none")));
		H->CloseScreen(); // the debrief's Close button
		return true;
	});
}

// A promotion's ceremony in the lobby: it plays by itself after the case that earns it, then ends.
void AddCeremony(TSharedRef<FScript> S, TSharedRef<FReport> R, AKcHUD* Hud, int32 Level)
{
	TWeakObjectPtr<AKcHUD> H(Hud);
	S->Steps.Add([R, H, Level]()
	{
		if (!H.IsValid()) return true;
		if (!H->IsCeremonyPlaying()) return false;
		R->Check(FString::Printf(TEXT("ceremony_%d"), Level), true, H->Game()->GetLevelTitle());
		return true;
	});
	S->Steps.Add([R, H, Level]()
	{
		if (!H.IsValid()) return true;
		if (H->IsCeremonyPlaying()) return false;
		const kc::GameState& G = H->Game()->GetState();
		R->Check(FString::Printf(TEXT("promoted_%d"), Level), G.Career.Level == Level && !G.bCeremonyPending, FString::Printf(TEXT("level %d %s"), G.Career.Level, *H->Game()->GetLevelTitle()));
		return true;
	});
}

void RunPlay(TSharedRef<FReport> R, AKcHUD* Hud, UWorld* World)
{
	TSharedRef<FScript> S = MakeShared<FScript>();
	TWeakObjectPtr<AKcHUD> H(Hud);
	S->Steps.Add([R, H]()
	{
		if (!H.IsValid()) return true;
		H->StartNewGame(EKcDifficulty::Professional, TEXT("front_office"));
		UKcCampaignSubsystem* C = H->Campaign();
		R->Check(TEXT("content"), C->GetLoadErrors().IsEmpty() && C->GetCatalogue().Missions.size() >= 3,
			FString::Printf(TEXT("%d missions, %d cards, %d problems"), static_cast<int32>(C->GetCatalogue().Missions.size()), static_cast<int32>(C->GetCards().size()), C->GetLoadErrors().Num()));
		R->Check(TEXT("start_place"), H->PlayerLocation() == TEXT("lp_office"), H->PlayerLocation());
		return true;
	});
	S->Steps.Add([]() { return true; }); // a tick for the hotel to place people
	// Levels 1 to 3, every case played as the expert would, each promotion with its ceremony.
	for (const TCHAR* Id : {TEXT("M001"), TEXT("M002"), TEXT("M003")}) AddCase(S, R, World, Hud, Id);
	AddCeremony(S, R, Hud, 2);
	for (const TCHAR* Id : {TEXT("M004"), TEXT("M005"), TEXT("M006"), TEXT("M007")}) AddCase(S, R, World, Hud, Id);
	AddCeremony(S, R, Hud, 3);
	for (const TCHAR* Id : {TEXT("M008"), TEXT("M009"), TEXT("M010"), TEXT("M011")}) AddCase(S, R, World, Hud, Id);
	AddCeremony(S, R, Hud, 4);
	S->Steps.Add([R, H]()
	{
		if (!H.IsValid()) return true;
		const kc::GameState& G = H->Game()->GetState();
		R->Check(TEXT("field_manual"), G.FieldManual.size() >= 12, FString::Printf(TEXT("%d cards"), static_cast<int32>(G.FieldManual.size())));
		R->Check(TEXT("inbox"), G.World.Inbox.size() >= 6, FString::Printf(TEXT("%d messages"), static_cast<int32>(G.World.Inbox.size())));
		const UKcAudioSubsystem* Audio = H->GetGameInstance()->GetSubsystem<UKcAudioSubsystem>();
		R->Check(TEXT("audio"), Audio && Audio->LoadedSoundCount() >= 10, FString::Printf(TEXT("%d sounds"), Audio ? Audio->LoadedSoundCount() : 0));
		FString Message;
		R->Check(TEXT("save"), H->SaveNow(Message), H->Saves()->GetSlotPath());
		return true;
	});
	S->OnEnd = [R]()
	{
		R->Write(TEXT("play"), TEXT("KcSelfTest_play.txt"));
		FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
	};
	RunScript(S, 0.7f);
}

void RunResume(FReport& R, AKcHUD* Hud)
{
	FString Message;
	const EKcLoadOutcome Outcome = Hud->ContinueGame(Message); // the main menu Continue button
	R.Check(TEXT("load"), Outcome == EKcLoadOutcome::Ok, FString::Printf(TEXT("outcome %d %s %s"), static_cast<int32>(Outcome), *Message, *Hud->Saves()->GetSlotPath()));
	if (Outcome != EKcLoadOutcome::Ok && Outcome != EKcLoadOutcome::RecoveredFromBackup) return;
	const kc::GameState& G = Hud->Game()->GetState();
	TArray<FString> Passed = KcConvert::ToFStrings(G.Career.PassedMissions);
	R.Check(TEXT("passed"), Passed.Contains(TEXT("M001")) && Passed.Contains(TEXT("M007")) && Passed.Contains(TEXT("M011")), FString::Join(Passed, TEXT(",")));
	R.Check(TEXT("level"), G.Career.Level == 4, Hud->Game()->GetLevelTitle());
	R.Check(TEXT("ceremony_played"), !G.bCeremonyPending, FString::Printf(TEXT("pending %d"), G.bCeremonyPending ? 1 : 0));
	R.Check(TEXT("field_manual"), G.FieldManual.size() >= 12, FString::Printf(TEXT("%d cards"), static_cast<int32>(G.FieldManual.size())));
	R.Check(TEXT("records"), G.Campaign.Records.size() >= 11, FString::Printf(TEXT("%d records"), static_cast<int32>(G.Campaign.Records.size())));
	R.Check(TEXT("no_case_open"), G.Campaign.ActiveMissionId.empty(), ToFString(G.Campaign.ActiveMissionId));
	R.Check(TEXT("objective"), !Hud->Campaign()->Objective().IsEmpty(), Hud->Campaign()->Objective());
}
} // namespace

static FString FileSafe(const FString& Name)
{
	FString Out;
	for (TCHAR C : Name) Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	return Out;
}

struct FTourState
{
	TArray<FString> Names;
	TArray<FTransform> Views;
	TArray<FString> Lines;
	TArray<FString> Gaps;
	int32 Index = 0;
	bool bCaptured = false;
	double SumMs = 0.0;
	FTimerHandle Timer;
};

// Teleports the player through every place; each stop settles for 2 s, then records timings and a screenshot.
static void RunTour(UWorld* World, AKcHUD* Hud)
{
	APlayerController* PC = World->GetFirstPlayerController();
	AKcHotel* Hotel = Hud->Hotel();
	if (!PC || !Hotel || !PC->GetPawn())
	{
		FFileHelper::SaveStringToFile(TEXT("FAIL\nno player or hotel\n"), *(FPaths::ProjectSavedDir() / TEXT("KcTour.txt")));
		FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
		return;
	}
	Hud->StartNewGame(EKcDifficulty::Professional);
	PC->SetIgnoreLookInput(true);
	PC->SetIgnoreMoveInput(true);
	TSharedRef<FTourState> State = MakeShared<FTourState>();
	Hotel->GetTourViewpoints(State->Names, State->Views);
	if (const UKcCampaignSubsystem* Campaign = World->GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>())
	{
		for (const FString& Gap : Hotel->MissingWorkPoints(Campaign->GetCatalogue())) State->Gaps.Add(Gap);
	}
	const FIntPoint Size = World->GetGameViewport() ? World->GetGameViewport()->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	State->Lines.Add(FString::Printf(TEXT("resolution=%dx%d places=%d"), Size.X, Size.Y, State->Views.Num()));
	TWeakObjectPtr<AKcHotel> WeakHotel(Hotel);
	auto Place = [PC, WeakHotel](const FTransform& View)
	{
		PC->GetPawn()->SetActorLocation(View.GetLocation() - FVector(0.0f, 0.0f, 70.0f), false, nullptr, ETeleportType::TeleportPhysics);
		PC->SetControlRotation(View.Rotator());
		if (WeakHotel.IsValid()) WeakHotel->SetActiveFloor(AKcHotel::FloorAt(View.GetLocation().Z - 165.0f));
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
			const double AvgMs = State->SumMs / FMath::Max(1, State->Views.Num());
			// Every mission step must have somewhere in the building to be done.
			FString Text = FString::Printf(TEXT("%s\nmode=tour\naverage_frame_ms=%.2f average_fps=%.1f\nwork_point_gaps=%d\n"), State->Gaps.IsEmpty() ? TEXT("PASS") : TEXT("FAIL"),
				AvgMs, AvgMs > 0.0 ? 1000.0 / AvgMs : 0.0, State->Gaps.Num());
			for (const FString& G : State->Gaps) Text += TEXT("gap: ") + G + TEXT("\n");
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
	auto FirstAction = [](AKcHUD* H) -> const kc::ActionDef*
	{
		const kc::MissionDef* M = H->Campaign()->Active();
		return M && !M->BestOrder.empty() ? M->FindAction(M->BestOrder[0]) : nullptr;
	};
	TSharedRef<TArray<FStep>> Steps = MakeShared<TArray<FStep>>();
	Steps->Add({TEXT("MainMenu"), [](AKcHUD* H) { H->ShowMainMenu(); }});
	Steps->Add({TEXT("Difficulty"), [](AKcHUD* H) { H->ShowDifficultyPicker(); }});
	Steps->Add({TEXT("Background"), [](AKcHUD* H) { H->ShowBackgroundPicker(EKcDifficulty::Professional); }});
	Steps->Add({TEXT("Hud"), [](AKcHUD* H) { H->StartNewGame(EKcDifficulty::Professional); }});
	Steps->Add({TEXT("ShiftBoard"), [](AKcHUD* H) { H->OpenTerminal(TEXT("lp_office")); }});
	Steps->Add({TEXT("CaseCall"), [](AKcHUD* H) { H->StartCase(TEXT("M001")); }}); // the call shows on the next tick
	Steps->Add({TEXT("Lift"), [](AKcHUD* H) { H->CloseScreen(); H->OpenLift(-1); }});
	Steps->Add({TEXT("Talk"), [FirstAction](AKcHUD* H)
	{
		H->CloseScreen();
		if (const kc::ActionDef* A = FirstAction(H))
		{
			TArray<const kc::DialogueLine*> Lines;
			for (const kc::DialogueLine& L : A->Lines) Lines.Add(&L);
			H->PlayLines(Lines, FString(), [H]() { H->CloseScreen(); });
		}
	}});
	Steps->Add({TEXT("Tool"), [](AKcHUD* H)
	{
		H->CloseScreen();
		const kc::MissionDef* M = H->Campaign()->Active();
		const kc::ActionDef* Audit = M ? M->FindAction("lock_audit") : nullptr;
		if (Audit) H->DoAction(*Audit, [H]() { H->CloseScreen(); });
	}});
	Steps->Add({TEXT("ToolResult"), [](AKcHUD*) {}});
	Steps->Add({TEXT("Question"), [](AKcHUD* H)
	{
		H->CloseScreen();
		const kc::MissionDef* M = H->Campaign()->Active();
		const kc::ActionDef* Interview = M ? M->FindAction("interview_guest") : nullptr;
		if (Interview) H->Campaign()->DoAction(ToFString(Interview->Id), ToFString(Interview->At));
		const TArray<const kc::QuestionDef*> Qs = H->Campaign()->QuestionsAt(TEXT("room_1412"), TEXT("guest_whitlock"));
		if (!Qs.IsEmpty()) H->AskQuestion(*Qs[0], TEXT("room_1412"), [H]() { H->CloseScreen(); });
	}});
	Steps->Add({TEXT("Notebook"), [](AKcHUD* H) { H->CloseScreen(); H->OpenJournal(); }});
	Steps->Add({TEXT("Report"), [](AKcHUD* H) { H->CloseScreen(); H->OpenReport(TEXT("lp_office")); }});
	Steps->Add({TEXT("Debrief"), [](AKcHUD* H)
	{
		H->CloseScreen();
		H->Campaign()->PlayBestAndFile(TEXT("lp_office"));
		H->OpenDebrief();
	}});
	Steps->Add({TEXT("FieldManual"), [](AKcHUD* H) { H->CloseScreen(); H->OpenFieldManual([H]() { H->CloseScreen(); }); }});
	Steps->Add({TEXT("Career"), [](AKcHUD* H) { H->CloseScreen(); H->OpenCareer([H]() { H->CloseScreen(); }); }});
	Steps->Add({TEXT("Inbox"), [](AKcHUD* H) { H->CloseScreen(); H->OpenInbox(); }});
	Steps->Add({TEXT("Pause"), [](AKcHUD* H) { H->CloseScreen(); H->OpenPauseMenu(); }});
	Steps->Add({TEXT("Settings"), [](AKcHUD* H) { H->OpenSettings([H]() { H->CloseScreen(); }); }});
	Steps->Add({TEXT("Credits"), [](AKcHUD* H) { H->OpenCredits([H]() { H->CloseScreen(); }); }});
	Steps->Add({TEXT("Ceremony"), [](AKcHUD* H) { H->CloseScreen(); H->PlayCeremony(TEXT("Senior LP Officer"), TEXT("Promoted by the general manager and the executive committee, on the evidence of your work")); }});
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
	}), 1.2f);
}

void Run(UWorld* World)
{
	const FString Mode = RequestedMode();
	APlayerController* PC = World->GetFirstPlayerController();
	AKcHUD* Hud = PC ? Cast<AKcHUD>(PC->GetHUD()) : nullptr;
	if (!Hud)
	{
		FReport R;
		R.Check(TEXT("hud"), false, TEXT("missing"));
		R.Write(Mode, TEXT("KcSelfTest_play.txt"));
		FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
		return;
	}
	Hud->Saves()->SetSlotName(SlotName);
	if (Mode == TEXT("ui"))
	{
		RunUiTour(World, Hud);
		return;
	}
	if (Mode == TEXT("tour"))
	{
		RunTour(World, Hud);
		return;
	}
	if (Mode == TEXT("play"))
	{
		TSharedRef<FReport> R = MakeShared<FReport>();
		R->Check(TEXT("hud"), true, Hud->GetName());
		RunPlay(R, Hud, World);
		return;
	}
	FReport R;
	R.Check(TEXT("hud"), true, Hud->GetName());
	if (Mode == TEXT("resume")) RunResume(R, Hud);
	else R.Check(TEXT("mode"), false, TEXT("expected play, resume, tour or ui"));
	R.Write(Mode, TEXT("KcSelfTest.txt"));
	FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
}
} // namespace KcSelfTest
