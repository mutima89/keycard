// Owns the code-built Slate UI and the screen flow. Each flow step calls UKcGameSubsystem,
// which is the same path the self-test and automation tests drive.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "KcTypes.h"
#include "KcHUD.generated.h"

class SKcCaseFiles;
class SKcHudOverlay;
class SWidget;
class UKcGameSubsystem;
class UKcSaveSubsystem;
struct FKcAssessmentDraft;

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
	void StartNewGame(EKcDifficulty Difficulty, const FString& Background = TEXT("graduate"));
	void ShowBackgroundPicker(EKcDifficulty Difficulty);
	void ShowChallenges();
	void StartChallenge(int32 Mode); // 1 survival, 2 daily
	void ShowChallengeResults();
	void OpenGlossary(TFunction<void()> OnBack);
	// Optional AI colleagues: free-typed chat with one person (needs the player's key, off by default).
	void OpenAiChat(const FString& PersonId, const FString& Status = FString());
	void OpenReview(bool bEnding, TFunction<void()> OnBack);
	// Photo mode: hides the HUD; F12 takes a screenshot (Saved/Screenshots).
	void TogglePhotoMode();
	bool IsPhotoMode() const { return bPhotoMode; }
	void OpenSkills(TFunction<void()> OnBack);
	// A conversation with tone choices (Data/Dialogue.json); OnEnd runs when it is over.
	void PlayConversation(const struct FKcConversationView& View, TFunction<void()> OnEnd);
	// Letterboxed title moment over the boardroom (promotion, CRO); skipped by Reduced motion's camera move.
	void PlayCeremony(const FString& Title, const FString& Subtitle);
	bool IsCeremonyPlaying() const { return CeremonyRoot.IsValid(); }
	EKcLoadOutcome ContinueGame(FString& OutMessage);
	void OpenPauseMenu();
	void OpenSettings(TFunction<void()> OnBack);
	void OpenJournal();
	// Messages from colleagues, incidents, the regulator and home (I key, or the workstation).
	TSharedPtr<class SKcInbox> OpenInbox(const FString& MessageId = FString());
	void OpenCredits(TFunction<void()> OnBack);
	bool SaveNow(FString& OutMessage);
	void QuitToDesktop();

	// ---- Mission flow ----
	void TalkToManager();
	void OpenEvidence(const FString& EvidenceId, TFunction<void()> OnBack = nullptr);
	void OpenWorkstation();
	// Opens the risk tool hub on a tab (index of SKcToolHub::ETab).
	void OpenToolHub(int32 Tab = 0);
	void OpenAssessment();
	// Campaign missions after M001. Optionally opens a mission (or its debrief) directly.
	// Where: the HQ location of the terminal ("" for the player's workstation; "boardroom" for the lectern).
	TSharedPtr<SKcCaseFiles> OpenCaseFiles(const FString& MissionId = FString(), bool bDebrief = false, const FString& Where = FString());
	// In-person casework (AKcCaseMarker): evidence examined or a person interviewed at a location.
	void ExamineInPerson(const FString& MissionId, const FString& EvidenceId, const FString& Where);
	EKcSubmitResult SubmitAssessment(const FKcSubmissionInput& Input);
	void OpenDebrief();

	// ---- Screen stack (one modal screen at a time) ----
	void ShowScreen(TSharedRef<SWidget> Content, TSharedPtr<SWidget> Focus, TFunction<void()> OnBack, bool bPauseGame);
	void CloseScreen();
	bool IsScreenOpen() const { return Screen.IsValid(); }
	void Notice(const FString& Message);
	void ClearNotices();
	void OnSettingsChanged();

	// The person in a conversation speaks the line (lip-sync, gestures) and reacts to the player's last
	// choice (+1/-1); they stay attentive until the screen closes.
	void Speak(const FString& PersonId, const FString& Line, int32 Reaction = 0);
	// "Board call in 4:12" while a crisis clock runs, else empty.
	FString GetCrisisClockText() const;

	UKcGameSubsystem* Game() const;
	UKcSaveSubsystem* Saves() const;

	FText MenuMessage;

private:
	UFUNCTION()
	void HandleNotice(const FString& Message);

	void BuildOverlay();
	void SetGameInput();

	TSharedPtr<SWidget> Screen;

	TWeakObjectPtr<class UKcPersonComponent> Speaker;
	void ReleaseSpeaker();
	class AKcValtrenHQ* Office() const;

	// Colleagues come to find you: a new message from someone in the office is delivered in person.
	void TickApproach();
	void ResetApproach();
	TSet<FString> KnownMessages;
	bool bKnownMessages = false;
	double NextApproachCheck = 0.0;
	double ApproachTimeout = 0.0;
	bool bApproaching = false;

	// Crisis clock: a crisis opens with an alarm; above Analyst difficulty the board call starts after a
	// few minutes and unseen evidence is out of reach.
	void TickCrisisClock();
	FString ClockMission;
	double ClockEnds = 0.0;
	bool bClockWarned = false;
	bool bClockExpired = false;
	bool bOpenCallPending = false;
	TSharedPtr<SWidget> CeremonyRoot;
	bool bPhotoMode = false;
	bool bEndingPending = false;
	double CeremonyEnds = 0.0;
	void EndCeremony();
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> CeremonyCamera;
	TSharedPtr<SWidget> OverlayRoot;
	TSharedPtr<SKcHudOverlay> Overlay;
	TSharedPtr<FKcAssessmentDraft> Draft;
	bool bMainMenuOpen = false;

	// Main menu backdrop: a slow camera drift through the office (static under Reduced Motion).
	void StartMenuCamera();
	void StopMenuCamera();
	UPROPERTY(Transient)
	TObjectPtr<class ACameraActor> MenuCamera;
	double MenuCameraTime = 0.0;
};
