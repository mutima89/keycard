// Owns the code-built Slate UI and the screen flow: menus, conversations with people, work at case
// points, the LP terminal (shift board, incident report, Field Manual, career), the lift, the
// debrief, the inbox and the promotion ceremony. Every step calls UKcCampaignSubsystem or
// UKcGameSubsystem, the same path the self-test and the automation tests drive. No rules here.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KcTypes.h"
#include "KcHUD.generated.h"

class AKcCasePoint;
class AKcHotel;
class AKcPersonActor;
class SKcHudOverlay;
class SKcInbox;
class SWidget;
class UKcCampaignSubsystem;
class UKcGameSubsystem;
class UKcSaveSubsystem;
namespace kc { struct ActionDef; struct QuestionDef; struct DialogueLine; enum class EGameMode; }

UCLASS()
class KEYCARD_API AKcHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	// ---- Menus ----
	void ShowMainMenu();
	void ShowDifficultyPicker();
	void ShowBackgroundPicker(EKcDifficulty Difficulty);
	// Background: "ex_police", "front_office", "military", "criminology". Starts on B1 in the LP office.
	void StartNewGame(EKcDifficulty Difficulty, const FString& Background = TEXT("front_office"));
	EKcLoadOutcome ContinueGame(FString& OutMessage);
	// Challenge modes from the main menu: survival, daily, a classroom set; and the local leaderboard.
	void ShowChallengePicker();
	void ShowClassroomPicker();
	void StartChallenge(kc::EGameMode Mode, const FString& ClassroomSetId = FString());
	void OpenLeaderboard(TFunction<void()> OnBack);
	void OpenPauseMenu();
	void OpenSettings(TFunction<void()> OnBack);
	void OpenCredits(TFunction<void()> OnBack);
	bool SaveNow(FString& OutMessage);
	void QuitToDesktop();
	// Photo mode: hides the HUD; F12 takes a screenshot (Saved/Screenshots).
	void TogglePhotoMode();
	bool IsPhotoMode() const { return bPhotoMode; }

	// ---- Notebook, Field Manual, career, inbox ----
	// The notebook (J): the case so far (findings, custody, what you said), the career and the people.
	void OpenJournal();
	void OpenFieldManual(TFunction<void()> OnBack);
	void OpenCareer(TFunction<void()> OnBack);
	TSharedPtr<SKcInbox> OpenInbox(const FString& MessageId = FString());

	// ---- Casework in the world ----
	// A person: the case's face-to-face actions and questions for them here, else a word in passing.
	void TalkTo(AKcPersonActor* Person);
	// A case point: its work (and the work with a guest behind its door); one item opens directly.
	void UseCasePoint(AKcCasePoint* Point);
	// Does one action and shows what it found: the spoken lines, then the tool view (audit trail, clips,
	// log) and the finding. OnDone runs after the last screen (default: close).
	EKcCaseResult DoAction(const kc::ActionDef& Action, TFunction<void()> OnDone = nullptr);
	// Puts an in-person question; the answer is recorded when the player commits it.
	void AskQuestion(const kc::QuestionDef& Question, const FString& Where, TFunction<void()> OnDone);
	// The LP office terminal: the shift board with no case open, the incident report during one.
	void OpenTerminal(const FString& Where);
	EKcCaseResult StartCase(const FString& MissionId);
	void OpenReport(const FString& Where);
	EKcCaseResult FileReport(const FString& Where);
	void OpenDebrief();
	// After the capstone: the ending the career earned, the performance review and the certificate.
	void OpenEnding();
	// A challenge's result once it is over (recorded on the leaderboard).
	void OpenChallengeResult();
	// The debrief's Close: the ending after the capstone, the result when a challenge is over.
	void AfterDebrief(const FString& MissionId);
	// Writes the career certificate (Markdown) to Saved/Certificates; the path, or "" on failure.
	FString SaveCertificate();
	// The lift: choose a floor; the ride fades out, chimes and arrives on the other floor.
	void OpenLift(int32 FromFloor);
	void RideLift(int32 ToFloor);

	// ---- Ceremony ----
	// Letterboxed title moment in the lobby (promotion); Reduced motion cuts instead of moving.
	void PlayCeremony(const FString& Title, const FString& Subtitle);
	bool IsCeremonyPlaying() const { return CeremonyRoot.IsValid(); }

	// ---- Screen stack (one modal screen at a time) ----
	void ShowScreen(TSharedRef<SWidget> Content, TSharedPtr<SWidget> Focus, TFunction<void()> OnBack, bool bPauseGame);
	void CloseScreen();
	bool IsScreenOpen() const { return Screen.IsValid(); }
	void Notice(const FString& Message);
	void ClearNotices();
	void OnSettingsChanged();

	// The person says the line (lip-sync, gestures; their recorded voice if there is one) and reacts to
	// the player's last choice (+1/-1). Someone not in view is heard over the radio or the phone.
	void Speak(const FString& PersonId, const FString& Line, int32 Reaction = 0);
	// Plays spoken lines one screen at a time, then OnDone.
	void PlayLines(const TArray<const kc::DialogueLine*>& Lines, const FString& Channel, TFunction<void()> OnDone);

	UKcGameSubsystem* Game() const;
	UKcSaveSubsystem* Saves() const;
	UKcCampaignSubsystem* Campaign() const;
	AKcHotel* Hotel() const;
	// The location the player is standing in ("" outside every zone).
	FString PlayerLocation() const;

	FText MenuMessage;

private:
	UFUNCTION()
	void HandleNotice(const FString& Message);

	void BuildOverlay();
	void SetGameInput();
	FText StatusText() const;
	FText ObjectiveText() const;

	TSharedPtr<SWidget> Screen;
	TSharedPtr<SWidget> OverlayRoot;
	TSharedPtr<SKcHudOverlay> Overlay;

	TWeakObjectPtr<class UKcPersonComponent> Speaker;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> RadioVoice;
	void ReleaseSpeaker();

	// The case call (radio, phone, the desk) plays once when nothing else is on screen.
	void TickCaseCall();
	// The promotion ceremony plays once the debrief is closed.
	void TickCeremony();

	TSharedPtr<SWidget> CeremonyRoot;
	double CeremonyEnds = 0.0;
	void EndCeremony();
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> CeremonyCamera;

	bool bPhotoMode = false;
	bool bMainMenuOpen = false;
	bool bRiding = false;

	// Main menu backdrop: a slow camera drift across the lobby at night (static under Reduced motion).
	void StartMenuCamera();
	void StopMenuCamera();
	UPROPERTY(Transient) TObjectPtr<class ACameraActor> MenuCamera;
	double MenuCameraTime = 0.0;
	FTransform MenuView;
};
