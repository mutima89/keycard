// Owns the kc::GameState for the running session. Every change goes through here or through
// UKcCampaignSubsystem, so the UI, the self-test and the automation tests share one path.
#pragma once

#include "CoreMinimal.h"
#include "KcGameState.h"
#include "KcTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KcGameSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FKcOnStateChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FKcOnNotice, const FString&, Message);

UCLASS()
class KEYCARD_API UKcGameSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ---- Session ----
	// Background: "ex_police", "front_office", "military", "criminology".
	UFUNCTION(BlueprintCallable, Category = "Keycard")
	void NewGame(EKcDifficulty Difficulty, const FString& ProfileName = TEXT("Officer"), const FString& Background = TEXT("front_office"), int64 Seed = 0);

	// Loads the save slot through UKcSaveSubsystem.
	UFUNCTION(BlueprintCallable, Category = "Keycard")
	EKcLoadOutcome Continue(FString& OutMessage);

	UFUNCTION(BlueprintPure, Category = "Keycard")
	bool HasGame() const { return bHasGame; }

	UFUNCTION(BlueprintPure, Category = "Keycard")
	EKcDifficulty GetDifficulty() const;

	UFUNCTION(BlueprintPure, Category = "Keycard")
	int32 GetLevel() const { return State.Career.Level; }

	UFUNCTION(BlueprintPure, Category = "Keycard")
	FString GetLevelTitle() const;

	// ---- Settings (persisted in the save, applied to the engine here) ----
	UFUNCTION(BlueprintPure, Category = "Keycard")
	FKcSettingsView GetSettings() const;

	UFUNCTION(BlueprintCallable, Category = "Keycard")
	void SetSettings(const FKcSettingsView& NewSettings, bool bApplyWindowMode);

	void ApplySettings(bool bApplyWindowMode) const;

	// ---- C++ access ----
	const kc::GameState& GetState() const { return State; }
	// Edit, then call StateEdited() (UI refresh) or StateCheckpoint() (refresh and autosave).
	kc::GameState& EditState() { return State; }
	void StateEdited() { AfterAction(false); }
	void StateCheckpoint() { AfterAction(true); }
	void SetLoadedState(kc::GameState&& Loaded);

	// The promotion ceremony to play (level reached), or 0. Taking it clears it in the save, so a
	// ceremony interrupted by quitting plays again on the next load.
	int32 PeekCeremony() const { return State.bCeremonyPending ? State.CeremonyLevel : 0; }
	void CeremonyPlayed();

	UPROPERTY(BlueprintAssignable, Category = "Keycard")
	FKcOnStateChanged OnStateChanged;

	// Player-facing notices: autosaves, cards earned, save recovery.
	UPROPERTY(BlueprintAssignable, Category = "Keycard")
	FKcOnNotice OnNotice;

	void Notify(const FString& Message);

private:
	void AfterAction(bool bAutosave);

	kc::GameState State;
	bool bHasGame = false;
};
