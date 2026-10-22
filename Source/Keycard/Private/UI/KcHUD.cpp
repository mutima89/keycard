#include "KcHUD.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformTime.h"
#include "KcAudioSubsystem.h"
#include "KcCampaignSubsystem.h"
#include "KcCharacter.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcHotel.h"
#include "KcInteractable.h"
#include "KcInteractionComponent.h"
#include "KcPerson.h"
#include "KcSaveSubsystem.h"
#include "KcScreens.h"
#include "KcSelfTest.h"
#include "KcVoice.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/App.h"
#include "SKcHudOverlay.h"
#include "SKcInbox.h"
#include "SKcScreen.h"
#include "TimerManager.h"
#include "Widgets/Layout/SBox.h"

#define LOCTEXT_NAMESPACE "Keycard"

using KcConvert::ToFString;
using KcConvert::ToStd;

UKcGameSubsystem* AKcHUD::Game() const
{
	return GetGameInstance()->GetSubsystem<UKcGameSubsystem>();
}

UKcSaveSubsystem* AKcHUD::Saves() const
{
	return GetGameInstance()->GetSubsystem<UKcSaveSubsystem>();
}

UKcCampaignSubsystem* AKcHUD::Campaign() const
{
	return GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>();
}

AKcHotel* AKcHUD::Hotel() const
{
	for (TActorIterator<AKcHotel> It(GetWorld()); It; ++It) return *It;
	return nullptr;
}

FString AKcHUD::PlayerLocation() const
{
	const APlayerController* PC = PlayerOwner.Get();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	const AKcHotel* H = Hotel();
	return Pawn && H ? H->LocationAt(Pawn->GetActorLocation()) : FString();
}

static void PlacePawn(APlayerController* PC, const FTransform& At)
{
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn) return;
	Pawn->SetActorLocation(At.GetLocation(), false, nullptr, ETeleportType::TeleportPhysics);
	PC->SetControlRotation(At.Rotator());
}

// ---------------------------------------------------------------- lifecycle

void AKcHUD::BeginPlay()
{
	Super::BeginPlay();
	SetTickableWhenPaused(true); // the menu camera keeps drifting while the menu pauses the world
	Game()->OnNotice.AddDynamic(this, &AKcHUD::HandleNotice);
	Game()->ApplySettings(false);
	BuildOverlay();
	if (!KcSelfTest::IsRequested()) ShowMainMenu();
}

void AKcHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UKcGameSubsystem* G = Game()) G->OnNotice.RemoveDynamic(this, &AKcHUD::HandleNotice);
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		if (Screen.IsValid()) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
		if (OverlayRoot.IsValid()) Viewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
		if (CeremonyRoot.IsValid()) Viewport->RemoveViewportWidgetContent(CeremonyRoot.ToSharedRef());
	}
	Screen.Reset();
	OverlayRoot.Reset();
	Overlay.Reset();
	CeremonyRoot.Reset();
	Super::EndPlay(EndPlayReason);
}

FText AKcHUD::ObjectiveText() const
{
	const UKcCampaignSubsystem* C = Campaign();
	return C ? FText::FromString(C->Objective()) : FText::GetEmpty();
}

FText AKcHUD::StatusText() const
{
	const UKcGameSubsystem* G = Game();
	const UKcCampaignSubsystem* C = Campaign();
	if (!G || !G->HasGame() || !C) return FText::GetEmpty();
	const kc::GameState& S = G->GetState();
	int32 Unread = 0, Awaiting = 0;
	for (const kc::InboxMessage& M : S.World.Inbox)
	{
		Unread += M.bRead ? 0 : 1;
		Awaiting += M.NeedsReply() ? 1 : 0;
	}
	FString Mail = FString::Printf(TEXT("[I]  Inbox %d"), Unread);
	if (Awaiting > 0) Mail += FString::Printf(TEXT(" (%d to reply)"), Awaiting);
	const FString Place = UKcCampaignSubsystem::LocationName(PlayerLocation());
	const FString Clock = C->ClockText();
	FString Line = Clock.IsEmpty() ? FString() : Clock + TEXT("      ");
	if (!Place.IsEmpty()) Line += Place + TEXT("      ");
	Line += FString::Printf(TEXT("%s      [J]  Notebook      Stress %d"), *Mail, S.World.Stress);
	return FText::FromString(Line);
}

void AKcHUD::BuildOverlay()
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport) return;
	if (OverlayRoot.IsValid()) Viewport->RemoveViewportWidgetContent(OverlayRoot.ToSharedRef());
	TWeakObjectPtr<AKcHUD> WeakThis(this);
	TSharedRef<SKcHudOverlay> NewOverlay = SNew(SKcHudOverlay)
		.Objective_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->ObjectiveText() : FText::GetEmpty(); })
		.Prompt_Lambda([WeakThis]()
		{
			const APlayerController* PC = WeakThis.IsValid() ? WeakThis->PlayerOwner.Get() : nullptr;
			const AKcCharacter* Pawn = PC ? Cast<AKcCharacter>(PC->GetPawn()) : nullptr;
			return Pawn ? Pawn->GetInteraction()->GetPromptText() : FText::GetEmpty();
		})
		.Status_Lambda([WeakThis]() { return WeakThis.IsValid() ? WeakThis->StatusText() : FText::GetEmpty(); })
		.StressLevel_Lambda([WeakThis]()
		{
			const UKcGameSubsystem* G = WeakThis.IsValid() ? WeakThis->Game() : nullptr;
			return G && G->HasGame() ? G->GetState().World.Stress / 100.0f : 0.0f;
		})
		.ShowGameplay_Lambda([WeakThis]()
		{
			return WeakThis.IsValid() && WeakThis->Game()->HasGame() && !WeakThis->IsScreenOpen() && !WeakThis->IsCeremonyPlaying();
		});
	Overlay = NewOverlay;
	OverlayRoot = NewOverlay;
	Viewport->AddViewportWidgetContent(NewOverlay, 30);
	if (bPhotoMode) OverlayRoot->SetVisibility(EVisibility::Collapsed);
}

void AKcHUD::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (CeremonyRoot.IsValid() && FPlatformTime::Seconds() > CeremonyEnds) EndCeremony();
	if (!bMainMenuOpen && Game() && Game()->HasGame())
	{
		TickCeremony();
		TickCaseCall();
	}
	if (!bMainMenuOpen || !MenuCamera || KcUI::IsReducedMotion()) return;
	// A slow sway across the lobby, easing at both ends: 60 s per pass.
	MenuCameraTime += FApp::GetDeltaTime();
	const double Phase = FMath::Fmod(MenuCameraTime / 60.0, 2.0);
	const double T = Phase < 1.0 ? Phase : 2.0 - Phase;
	const float Eased = static_cast<float>(0.5 - 0.5 * FMath::Cos(T * PI));
	const FVector From = MenuView.GetLocation();
	const FRotator Rot = MenuView.Rotator();
	MenuCamera->SetActorLocationAndRotation(From + FVector(0.0f, FMath::Lerp(-120.0f, 120.0f, Eased), 0.0f), FRotator(Rot.Pitch, Rot.Yaw + FMath::Lerp(6.0f, -6.0f, Eased), 0.0f));
}

void AKcHUD::SetGameInput()
{
	if (APlayerController* PC = PlayerOwner.Get())
	{
		PC->SetInputMode(FInputModeGameOnly());
		PC->SetShowMouseCursor(false);
		PC->FlushPressedKeys();
		PC->SetPause(false);
	}
}

void AKcHUD::HandleNotice(const FString& Message)
{
	Notice(Message);
}

void AKcHUD::Notice(const FString& Message)
{
	if (Overlay.IsValid()) Overlay->AddNotice(Message);
	UE_LOG(LogTemp, Display, TEXT("Keycard notice: %s"), *Message);
}

void AKcHUD::ClearNotices()
{
	if (Overlay.IsValid()) Overlay->ClearNotices();
}

void AKcHUD::OnSettingsChanged()
{
	BuildOverlay();
}

// ---------------------------------------------------------------- screens

void AKcHUD::ShowScreen(TSharedRef<SWidget> Content, TSharedPtr<SWidget> Focus, TFunction<void()> OnBack, bool bPauseGame)
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (Viewport && Screen.IsValid()) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
	TSharedRef<SKcScreen> Root = SNew(SKcScreen).OnBack(MoveTemp(OnBack))[Content];
	Screen = Root;
	if (Viewport) Viewport->AddViewportWidgetContent(Root, 20);
	if (APlayerController* PC = PlayerOwner.Get())
	{
		FInputModeUIOnly Mode;
		Mode.SetWidgetToFocus(Focus.IsValid() ? Focus : TSharedPtr<SWidget>(Root));
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		PC->SetInputMode(Mode);
		PC->SetShowMouseCursor(true);
		PC->FlushPressedKeys();
		PC->SetPause(bPauseGame);
	}
}

void AKcHUD::CloseScreen()
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		if (Screen.IsValid()) Viewport->RemoveViewportWidgetContent(Screen.ToSharedRef());
	}
	Screen.Reset();
	ReleaseSpeaker();
	if (bMainMenuOpen) StopMenuCamera();
	bMainMenuOpen = false;
	SetGameInput();
}

void AKcHUD::StartMenuCamera()
{
	if (!MenuCamera)
	{
		TArray<FString> Names;
		TArray<FTransform> Views;
		if (const AKcHotel* H = Hotel()) H->GetTourViewpoints(Names, Views);
		const int32 Lobby = Names.IndexOfByKey(FString(TEXT("Lobby from the street doors")));
		MenuView = Views.IsValidIndex(Lobby) ? Views[Lobby] : FTransform(FRotator(-4.0f, 20.0f, 0.0f), FVector(380.0f, -60.0f, 165.0f));
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		MenuCamera = GetWorld()->SpawnActor<ACameraActor>(MenuView.GetLocation(), MenuView.Rotator(), Params);
		if (MenuCamera)
		{
			MenuCamera->GetCameraComponent()->SetFieldOfView(70.0f);
			MenuCamera->GetCameraComponent()->bConstrainAspectRatio = false;
		}
	}
	if (APlayerController* PC = PlayerOwner.Get())
	{
		if (MenuCamera) PC->SetViewTarget(MenuCamera);
	}
}

void AKcHUD::StopMenuCamera()
{
	if (APlayerController* PC = PlayerOwner.Get())
	{
		if (APawn* Pawn = PC->GetPawn()) PC->SetViewTarget(Pawn);
	}
}

// ---------------------------------------------------------------- menus

void AKcHUD::ShowMainMenu()
{
	// The world keeps running behind the main menu so the backdrop camera can drift.
	const FKcScreenParts Parts = KcScreens::MainMenu(this);
	ShowScreen(Parts.Widget, Parts.Focus, nullptr, false);
	bMainMenuOpen = true;
	StartMenuCamera();
}

void AKcHUD::ShowDifficultyPicker()
{
	const FKcScreenParts Parts = KcScreens::DifficultyPicker(this);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { ShowMainMenu(); }, false);
	bMainMenuOpen = true;
}

void AKcHUD::ShowBackgroundPicker(EKcDifficulty Difficulty)
{
	const FKcScreenParts Parts = KcScreens::BackgroundPicker(this, Difficulty);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { ShowDifficultyPicker(); }, false);
	bMainMenuOpen = true;
}

void AKcHUD::StartNewGame(EKcDifficulty Difficulty, const FString& Background)
{
	Game()->NewGame(Difficulty, TEXT("Officer"), Background);
	CloseScreen();
	ClearNotices();
	if (const AKcHotel* H = Hotel()) PlacePawn(PlayerOwner.Get(), H->GetPlayerStart());
	Notice(TEXT("00:20, B1. Your first night shift in Loss Prevention at the Veldane Grand Harbourfront. Sign on at the LP office terminal."));
}

EKcLoadOutcome AKcHUD::ContinueGame(FString& OutMessage)
{
	const EKcLoadOutcome Outcome = Game()->Continue(OutMessage);
	if (Outcome == EKcLoadOutcome::Ok || Outcome == EKcLoadOutcome::RecoveredFromBackup)
	{
		CloseScreen();
		ClearNotices();
		BuildOverlay(); // loaded settings may change text size
		// Back where the player last worked, or at the LP office.
		if (const AKcHotel* H = Hotel())
		{
			const FString Where = Campaign() ? Campaign()->CurrentLocation() : FString();
			PlacePawn(PlayerOwner.Get(), Where.IsEmpty() ? H->GetPlayerStart() : H->GetArrival(Where));
		}
		if (Outcome == EKcLoadOutcome::Ok) Notice(TEXT("Welcome back."));
		MenuMessage = FText::GetEmpty();
	}
	else
	{
		MenuMessage = FText::FromString(OutMessage);
	}
	return Outcome;
}

void AKcHUD::OpenPauseMenu()
{
	if (IsScreenOpen() || !Game()->HasGame() || IsCeremonyPlaying()) return;
	const FKcScreenParts Parts = KcScreens::PauseMenu(this);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, true);
}

void AKcHUD::OpenSettings(TFunction<void()> OnBack)
{
	const bool bFromMenu = bMainMenuOpen;
	TFunction<void()> Back = [this, OnBack]()
	{
		OnSettingsChanged();
		OnBack();
	};
	const FKcScreenParts Parts = KcScreens::Settings(this, Back);
	ShowScreen(Parts.Widget, Parts.Focus, Back, !bFromMenu);
	bMainMenuOpen = bFromMenu;
}

void AKcHUD::OpenCredits(TFunction<void()> OnBack)
{
	const bool bFromMenu = bMainMenuOpen;
	const FKcScreenParts Parts = KcScreens::Credits(this, OnBack);
	ShowScreen(Parts.Widget, Parts.Focus, OnBack, !bFromMenu);
	bMainMenuOpen = bFromMenu; // keep the menu backdrop camera when opened from the main menu
}

bool AKcHUD::SaveNow(FString& OutMessage)
{
	FString Error;
	const bool bOk = Saves()->Save(Error);
	OutMessage = bOk ? FString(TEXT("Game saved.")) : FString::Printf(TEXT("Save failed: %s"), *Error);
	Notice(OutMessage);
	return bOk;
}

void AKcHUD::QuitToDesktop()
{
	UKismetSystemLibrary::QuitGame(GetWorld(), PlayerOwner.Get(), EQuitPreference::Quit, false);
}

void AKcHUD::TogglePhotoMode()
{
	if (IsScreenOpen() && !bPhotoMode) return;
	bPhotoMode = !bPhotoMode;
	if (OverlayRoot.IsValid()) OverlayRoot->SetVisibility(bPhotoMode ? EVisibility::Collapsed : EVisibility::HitTestInvisible);
	if (!bPhotoMode) Notice(TEXT("Photo mode off."));
}

// ---------------------------------------------------------------- notebook, manual, career, inbox

void AKcHUD::OpenJournal()
{
	if (IsScreenOpen() || !Game()->HasGame() || IsCeremonyPlaying()) return;
	const FKcScreenParts Parts = KcScreens::Journal(this);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

void AKcHUD::OpenFieldManual(TFunction<void()> OnBack)
{
	const FKcScreenParts Parts = KcScreens::FieldManual(this, OnBack);
	ShowScreen(Parts.Widget, Parts.Focus, OnBack, false);
}

void AKcHUD::OpenCareer(TFunction<void()> OnBack)
{
	const FKcScreenParts Parts = KcScreens::Career(this, OnBack);
	ShowScreen(Parts.Widget, Parts.Focus, OnBack, false);
}

TSharedPtr<SKcInbox> AKcHUD::OpenInbox(const FString& MessageId)
{
	if (!Game()->HasGame()) return nullptr;
	auto Back = [this]() { CloseScreen(); };
	TSharedRef<SKcInbox> Inbox = SNew(SKcInbox).Campaign(Campaign()).OpenMessageId(MessageId).OnClose(Back);
	ShowScreen(KcUI::MakeModalFrame(SNew(SBox).MinDesiredWidth(1100.0f).MinDesiredHeight(760.0f)[Inbox], 1200.0f), Inbox->GetInitialFocus(), Back, false);
	return Inbox;
}

// ---------------------------------------------------------------- people and voices

void AKcHUD::Speak(const FString& PersonId, const FString& Line, int32 Reaction)
{
	if (RadioVoice) RadioVoice->Stop();
	UKcPersonComponent* P = PersonId.IsEmpty() ? nullptr : UKcPersonComponent::Find(GetWorld(), PersonId);
	// Only someone within conversation distance speaks in person; anyone else is on the radio or phone.
	const APlayerController* PC = PlayerOwner.Get();
	const APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (P && Pawn && FVector::Dist(P->GetComponentLocation(), Pawn->GetActorLocation()) > 900.0f) P = nullptr;
	if (Speaker.IsValid() && Speaker.Get() != P) ReleaseSpeaker();
	if (P)
	{
		Speaker = P;
		P->SetEngaged(true);
		if (!Line.IsEmpty()) P->Say(Line);
		if (Reaction != 0) P->React(Reaction);
		return;
	}
	if (Line.IsEmpty() || PersonId.IsEmpty()) return;
	if (USoundWave* Voice = UKcAudioSubsystem::FindVoice(ToFString(kc::VoiceLineKey(ToStd(PersonId), ToStd(Line)))))
	{
		RadioVoice = UGameplayStatics::SpawnSound2D(this, Voice, 0.9f);
	}
}

void AKcHUD::ReleaseSpeaker()
{
	if (UKcPersonComponent* P = Speaker.Get())
	{
		P->StopTalking();
		P->SetEngaged(false);
	}
	Speaker.Reset();
	if (RadioVoice) RadioVoice->Stop();
	RadioVoice = nullptr;
}

void AKcHUD::PlayLines(const TArray<const kc::DialogueLine*>& Lines, const FString& Channel, TFunction<void()> OnDone)
{
	if (Lines.IsEmpty())
	{
		if (OnDone) OnDone();
		else CloseScreen();
		return;
	}
	const kc::DialogueLine* L = Lines[0];
	TArray<const kc::DialogueLine*> Rest(Lines);
	Rest.RemoveAt(0);
	const bool bPlayer = L->Speaker == "player";
	const UKcCampaignSubsystem* C = Campaign();
	FString Who = bPlayer ? FString(TEXT("You")) : (C ? C->PersonName(L->Speaker) : ToFString(L->Speaker));
	if (!bPlayer && C && !C->PersonRole(L->Speaker).IsEmpty()) Who += TEXT(", ") + C->PersonRole(L->Speaker);
	if (!Channel.IsEmpty()) Who = Channel + TEXT("  /  ") + Who;
	if (!bPlayer) Speak(ToFString(L->Speaker), ToFString(L->Text));
	const FString Text = ToFString(L->Text);
	auto Next = [this, Rest, Channel, OnDone]() { PlayLines(Rest, Channel, OnDone); };
	const FKcScreenParts Parts = KcScreens::Dialogue(FText::FromString(Who), FText::FromString(bPlayer ? Text : TEXT("“") + Text + TEXT("”")), Next);
	ShowScreen(Parts.Widget, Parts.Focus, Next, false);
}

// ---------------------------------------------------------------- casework

static FString PassingLine(const UKcCampaignSubsystem* C, const AKcPersonActor* P)
{
	// A word in passing: staff are busy with their own night, guests keep to themselves.
	const kc::CharacterDef* Def = C ? C->FindPerson(P->PersonId) : nullptr;
	const bool bStaff = !Def || Def->bStaff;
	const bool bMet = C && C->Active() && !C->ActionsAt(P->Location, P->PersonId, true).IsEmpty();
	if (bMet) return TEXT("I've told you what I know. Shout if you need me again.");
	const int32 Pick = static_cast<int32>(GetTypeHash(P->PersonId) % 3);
	if (bStaff)
	{
		static const TCHAR* const Lines[] = {TEXT("Evening. Quiet so far, touch wood."), TEXT("All good here. I'll radio if anything comes up."), TEXT("Long one tonight. Coffee's fresh in the back.")};
		return Lines[Pick];
	}
	static const TCHAR* const Guest[] = {TEXT("Evening."), TEXT("Oh, hello. Everything all right?"), TEXT("Just heading up.")};
	return Guest[Pick];
}

void AKcHUD::TalkTo(AKcPersonActor* Person)
{
	UKcCampaignSubsystem* C = Campaign();
	if (!Person || !C) return;
	const FString Where = Person->Location, Id = Person->PersonId;
	const TArray<const kc::ActionDef*> Actions = C->ActionsAt(Where, Id);
	const TArray<const kc::QuestionDef*> Questions = C->QuestionsAt(Where, Id);
	const FText Who = FText::FromString(Person->Role.IsEmpty() ? Person->DisplayName : Person->DisplayName + TEXT(", ") + Person->Role);
	if (Actions.IsEmpty() && Questions.IsEmpty())
	{
		const FString Line = PassingLine(C, Person);
		Speak(Id, Line);
		const FKcScreenParts Parts = KcScreens::Dialogue(Who, FText::FromString(TEXT("“") + Line + TEXT("”")), [this]() { CloseScreen(); });
		ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
		return;
	}
	Speak(Id, FString()); // turns to the player and listens
	TWeakObjectPtr<AKcPersonActor> WeakPerson(Person);
	// After each piece of work, back to the person while they still have something for the case.
	auto Again = [this, WeakPerson]()
	{
		AKcPersonActor* P = WeakPerson.Get();
		UKcCampaignSubsystem* Camp = Campaign();
		if (P && Camp && (!Camp->ActionsAt(P->Location, P->PersonId).IsEmpty() || !Camp->QuestionsAt(P->Location, P->PersonId).IsEmpty())) TalkTo(P);
		else CloseScreen();
	};
	TArray<FKcChoice> Items;
	for (const kc::ActionDef* A : Actions)
	{
		Items.Add({FText::FromString(ToFString(A->Label)), [this, A, Again]() { DoAction(*A, Again); }, true, true, FText::GetEmpty()});
	}
	for (const kc::QuestionDef* Q : Questions)
	{
		Items.Add({FText::FromString(ToFString(Q->Prompt)), [this, Q, Where, Again]() { AskQuestion(*Q, Where, Again); }, false, true, FText::GetEmpty()});
	}
	Items.Add({LOCTEXT("TalkLeave", "That's all for now"), [this]() { CloseScreen(); }, false, true, FText::GetEmpty()});
	const FKcScreenParts Parts = KcScreens::Choices(FText::GetEmpty(), Who, FText::GetEmpty(), Items, true);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

void AKcHUD::UseCasePoint(AKcCasePoint* Point)
{
	if (!Point) return;
	const TArray<const kc::ActionDef*> Work = Point->Work();
	if (Work.IsEmpty()) return;
	if (Work.Num() == 1)
	{
		DoAction(*Work[0]);
		return;
	}
	TWeakObjectPtr<AKcCasePoint> WeakPoint(Point);
	auto Again = [this, WeakPoint]()
	{
		if (WeakPoint.IsValid() && WeakPoint->IsUsable()) UseCasePoint(WeakPoint.Get());
		else CloseScreen();
	};
	TArray<FKcChoice> Items;
	for (const kc::ActionDef* A : Work)
	{
		Items.Add({FText::FromString(ToFString(A->Label)), [this, A, Again]() { DoAction(*A, Again); }, false, true, FText::GetEmpty()});
	}
	Items.Add({LOCTEXT("PointLeave", "Leave it for now"), [this]() { CloseScreen(); }, false, true, FText::GetEmpty()});
	const FKcScreenParts Parts = KcScreens::Choices(FText::FromString(UKcCampaignSubsystem::LocationName(Point->Location)), FText::FromString(Point->Title), FText::GetEmpty(), Items, false);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

EKcCaseResult AKcHUD::DoAction(const kc::ActionDef& Action, TFunction<void()> OnDone)
{
	UKcCampaignSubsystem* C = Campaign();
	if (!C) return EKcCaseResult::NotActive;
	const EKcCaseResult R = C->DoAction(ToFString(Action.Id), ToFString(Action.At));
	if (R != EKcCaseResult::Ok)
	{
		Notice(UKcCampaignSubsystem::ResultText(R));
		return R;
	}
	UKcAudioSubsystem::Cue(this, EKcCue::Confirm);
	const kc::ActionDef* A = &Action; // lives in the catalogue for the session
	TFunction<void()> Finish = OnDone ? OnDone : TFunction<void()>([this]() { CloseScreen(); });
	auto ShowResult = [this, A, Finish]()
	{
		const FKcScreenParts Parts = KcScreens::ActionResult(this, *A, Finish);
		ShowScreen(Parts.Widget, Parts.Focus, Finish, false);
	};
	TArray<const kc::DialogueLine*> Lines;
	for (const kc::DialogueLine& L : Action.Lines) Lines.Add(&L);
	const FString Channel = Action.Kind == kc::EActionKind::Call ? TEXT("Radio") : FString();
	PlayLines(Lines, Channel, ShowResult);
	return R;
}

void AKcHUD::AskQuestion(const kc::QuestionDef& Question, const FString& Where, TFunction<void()> OnDone)
{
	UKcCampaignSubsystem* C = Campaign();
	if (!C) return;
	const kc::QuestionDef* Q = &Question;
	const FText Who = FText::FromString(C->PersonName(Q->Person));
	auto Commit = [this, Q, Where, OnDone](const TArray<FString>& Answer)
	{
		UKcCampaignSubsystem* Camp = Campaign();
		const EKcCaseResult R = Camp ? Camp->AnswerInPerson(ToFString(Q->Id), Answer, Where) : EKcCaseResult::NotActive;
		if (R != EKcCaseResult::Ok)
		{
			Notice(UKcCampaignSubsystem::ResultText(R));
			return;
		}
		// What the player said, then what the other person says back. No reaction gives the score away.
		TArray<const kc::DialogueLine*> Lines;
		TSharedRef<TArray<kc::DialogueLine>> Said = MakeShared<TArray<kc::DialogueLine>>();
		for (const kc::OptionDef& O : Q->Options)
		{
			if (Answer.Contains(ToFString(O.Id)) && Q->Kind == kc::EQuestionKind::Single) Said->Add({"player", O.Label});
		}
		for (const kc::DialogueLine& L : *Said) Lines.Add(&L);
		for (const kc::OptionDef& O : Q->Options)
		{
			if (Answer.Contains(ToFString(O.Id)) && !O.Reply.Text.empty()) Lines.Add(&O.Reply);
		}
		// Said lives until the last line has played.
		TFunction<void()> After = [Said, OnDone]() { if (OnDone) OnDone(); };
		PlayLines(Lines, FString(), After);
	};
	const FKcScreenParts Parts = KcScreens::Question(this, *Q, Who, Commit, OnDone);
	ShowScreen(Parts.Widget, Parts.Focus, OnDone, false);
}

void AKcHUD::OpenTerminal(const FString& Where)
{
	UKcCampaignSubsystem* C = Campaign();
	if (!C || !Game()->HasGame()) return;
	if (!C->Active())
	{
		const FKcScreenParts Parts = KcScreens::ShiftBoard(this, Where);
		ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
		return;
	}
	TArray<FKcChoice> Items;
	Items.Add({FText::FromString(TEXT("Incident report: ") + ToFString(C->Active()->Title)), [this, Where]() { OpenReport(Where); }, true, true,
		LOCTEXT("TermReportNote", "Write it up when the fieldwork is done. It is scored when you file it.")});
	Items.Add({LOCTEXT("TermNotebook", "Notebook"), [this]() { CloseScreen(); OpenJournal(); }, false, true, FText::GetEmpty()});
	Items.Add({LOCTEXT("TermManual", "Field Manual"), [this, Where]() { OpenFieldManual([this, Where]() { OpenTerminal(Where); }); }, false, true, FText::GetEmpty()});
	Items.Add({LOCTEXT("TermInbox", "Inbox"), [this]() { CloseScreen(); OpenInbox(); }, false, true, FText::GetEmpty()});
	if (C->IsWhatIf())
	{
		Items.Add({LOCTEXT("TermAbandon", "End the what-if replay"), [this]()
		{
			if (UKcCampaignSubsystem* Camp = Campaign()) Camp->AbandonCase();
			CloseScreen();
		}, false, true, LOCTEXT("TermAbandonNote", "Nothing in a what-if changes your career or the hotel.")});
	}
	Items.Add({LOCTEXT("TermLogOff", "Log off"), [this]() { CloseScreen(); }, false, true, FText::GetEmpty()});
	const FKcScreenParts Parts = KcScreens::Choices(LOCTEXT("TermEyebrow", "LP office terminal"), FText::FromString(ToFString(C->Active()->Title)),
		FText::FromString(FString::Printf(TEXT("Case clock %s."), *C->ClockText())), Items, false);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

EKcCaseResult AKcHUD::StartCase(const FString& MissionId)
{
	UKcCampaignSubsystem* C = Campaign();
	const EKcCaseResult R = C ? C->StartCase(MissionId) : EKcCaseResult::NotActive;
	if (R != EKcCaseResult::Ok)
	{
		Notice(UKcCampaignSubsystem::ResultText(R));
		return R;
	}
	CloseScreen();
	ClearNotices(); // earlier case notices no longer apply
	return R;
}

void AKcHUD::OpenReport(const FString& Where)
{
	const FKcScreenParts Parts = KcScreens::Report(this, Where);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

EKcCaseResult AKcHUD::FileReport(const FString& Where)
{
	UKcCampaignSubsystem* C = Campaign();
	if (!C) return EKcCaseResult::NotActive;
	const EKcCaseResult Check = C->ValidateReport(C->Draft());
	if (Check != EKcCaseResult::Ok) return Check;
	const EKcCaseResult R = C->FileReport(C->Draft(), Where);
	if (R != EKcCaseResult::Ok) return R;
	const kc::MissionDebrief* D = C->LastDebrief();
	UKcAudioSubsystem::Cue(this, D && D->bPassed ? EKcCue::MissionPassed : EKcCue::MissionFailed);
	OpenDebrief();
	return R;
}

void AKcHUD::OpenDebrief()
{
	const kc::MissionDebrief* D = Campaign() ? Campaign()->LastDebrief() : nullptr;
	if (!D)
	{
		Notice(TEXT("No debrief yet: file a report first."));
		return;
	}
	const FKcScreenParts Parts = KcScreens::Debrief(this, *D, [this]() { CloseScreen(); });
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

void AKcHUD::OpenLift(int32 FromFloor)
{
	if (bRiding) return;
	const FKcScreenParts Parts = KcScreens::Lift(this, FromFloor);
	ShowScreen(Parts.Widget, Parts.Focus, [this]() { CloseScreen(); }, false);
}

void AKcHUD::RideLift(int32 ToFloor)
{
	CloseScreen();
	APlayerController* PC = PlayerOwner.Get();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	AKcHotel* H = Hotel();
	if (!Pawn || !H || bRiding) return;
	bRiding = true;
	// Doors close, a short ride in the dark, the chime, doors open on the other floor.
	const float FadeOut = 0.45f, Ride = 0.9f;
	if (PC->PlayerCameraManager) PC->PlayerCameraManager->StartCameraFade(0.0f, 1.0f, FadeOut, FLinearColor::Black, false, true);
	TWeakObjectPtr<AKcHUD> WeakThis(this);
	FTimerHandle Handle;
	GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(this, [WeakThis, ToFloor]()
	{
		AKcHUD* Hud = WeakThis.Get();
		APlayerController* P = Hud ? Hud->PlayerOwner.Get() : nullptr;
		APawn* Me = P ? P->GetPawn() : nullptr;
		AKcHotel* Hotel = Hud ? Hud->Hotel() : nullptr;
		if (Me && Hotel)
		{
			Me->SetActorLocation(Hotel->LiftDestination(Me->GetActorLocation(), ToFloor), false, nullptr, ETeleportType::TeleportPhysics);
			Hotel->SetActiveFloor(ToFloor);
		}
		UKcAudioSubsystem::Cue(Hud, EKcCue::Notice);
		if (P && P->PlayerCameraManager) P->PlayerCameraManager->StartCameraFade(1.0f, 0.0f, 0.5f, FLinearColor::Black, false, false);
		if (Hud)
		{
			Hud->bRiding = false;
			Hud->Notice(FString::Printf(TEXT("Floor %s."), *AKcHotel::FloorLabel(ToFloor)));
		}
	}), FadeOut + Ride, false);
}

// ---------------------------------------------------------------- the case call and the ceremony

void AKcHUD::TickCaseCall()
{
	if (IsScreenOpen() || IsCeremonyPlaying()) return;
	UKcCampaignSubsystem* C = Campaign();
	FKcCaseCall Call;
	if (!C || !C->TakeCaseCall(Call)) return;
	UKcAudioSubsystem::Cue(this, EKcCue::Incident);
	const FString Channel = Call.Channel == TEXT("FrontDesk") ? TEXT("Front desk") : (Call.Channel.IsEmpty() ? TEXT("Radio") : Call.Channel);
	FString Who = C->PersonName(ToStd(Call.FromId));
	const FString CallerRole = C->PersonRole(ToStd(Call.FromId));
	if (!CallerRole.IsEmpty()) Who += TEXT(", ") + CallerRole;
	Speak(Call.FromId, Call.Text);
	auto Done = [this]()
	{
		CloseScreen();
		if (const UKcCampaignSubsystem* Camp = Campaign()) Notice(Camp->Objective());
	};
	const FKcScreenParts Parts = KcScreens::Dialogue(FText::FromString(Channel + TEXT("  /  ") + Who), FText::FromString(TEXT("“") + Call.Text + TEXT("”")), Done,
		LOCTEXT("CallAck", "Received. On my way."));
	ShowScreen(Parts.Widget, Parts.Focus, Done, false);
}

void AKcHUD::TickCeremony()
{
	if (IsScreenOpen() || IsCeremonyPlaying()) return;
	UKcGameSubsystem* G = Game();
	const int32 Level = G ? G->PeekCeremony() : 0;
	if (Level <= 0) return;
	G->CeremonyPlayed();
	const kc::CareerLevelDef& L = kc::LevelDef(Level);
	FString By = TEXT("by the general manager and the executive committee");
	if (L.CeremonyStage == "area") By = TEXT("by the area leadership team");
	else if (L.CeremonyStage == "board") By = TEXT("by the board of Veldane Hotels");
	PlayCeremony(ToFString(L.Title), FString::Printf(TEXT("Promoted %s, on the evidence of your work"), *By));
}

void AKcHUD::PlayCeremony(const FString& Title, const FString& Subtitle)
{
	UGameViewportClient* Viewport = GetWorld()->GetGameViewport();
	if (!Viewport || CeremonyRoot.IsValid()) return;
	CeremonyRoot = KcScreens::CeremonyOverlay(Title, Subtitle);
	Viewport->AddViewportWidgetContent(CeremonyRoot.ToSharedRef(), 25);
	CeremonyEnds = FPlatformTime::Seconds() + 6.5;
	UKcAudioSubsystem::Cue(this, EKcCue::Promotion);
	// The camera eases to the front desk, under the hotel's name. Reduced motion cuts there instead.
	APlayerController* PC = PlayerOwner.Get();
	if (!PC) return;
	if (!CeremonyCamera)
	{
		TArray<FString> Names;
		TArray<FTransform> Views;
		if (const AKcHotel* H = Hotel()) H->GetTourViewpoints(Names, Views);
		const int32 Desk = Names.IndexOfByKey(FString(TEXT("Front desk")));
		const FTransform View = Views.IsValidIndex(Desk) ? Views[Desk] : FTransform(FRotator(-4.0f, 90.0f, 0.0f), FVector(1100.0f, 380.0f, 165.0f));
		CeremonyCamera = GetWorld()->SpawnActor<ACameraActor>(View.GetLocation(), View.Rotator());
		if (CeremonyCamera) CeremonyCamera->GetCameraComponent()->SetFieldOfView(62.0f);
	}
	if (!CeremonyCamera) return;
	if (KcUI::IsReducedMotion()) PC->SetViewTarget(CeremonyCamera);
	else PC->SetViewTargetWithBlend(CeremonyCamera, 1.6f, VTBlend_EaseInOut, 2.0f);
}

void AKcHUD::EndCeremony()
{
	if (UGameViewportClient* Viewport = GetWorld()->GetGameViewport())
	{
		if (CeremonyRoot.IsValid()) Viewport->RemoveViewportWidgetContent(CeremonyRoot.ToSharedRef());
	}
	CeremonyRoot.Reset();
	if (APlayerController* PC = PlayerOwner.Get())
	{
		if (PC->GetPawn() && PC->GetViewTarget() != PC->GetPawn())
		{
			if (KcUI::IsReducedMotion()) PC->SetViewTarget(PC->GetPawn());
			else PC->SetViewTargetWithBlend(PC->GetPawn(), 1.2f, VTBlend_EaseInOut, 2.0f);
		}
	}
}

#undef LOCTEXT_NAMESPACE
