// Wraps kc::SaveGameToFile / kc::LoadGameFromFile at Saved/SaveGames/<slot>.kcsave.
// Recovery rules (backup, .corrupt) live in KeycardSim; this class only picks the path and reports.
#pragma once

#include "CoreMinimal.h"
#include "KcLearning.h"
#include "KcTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "KcSaveSubsystem.generated.h"

UCLASS()
class KEYCARD_API UKcSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// Default "slot1". Self-test and automation tests use their own slots so player saves are never touched.
	UFUNCTION(BlueprintCallable, Category = "Keycard")
	void SetSlotName(const FString& InSlotName) { SlotName = InSlotName; }

	UFUNCTION(BlueprintPure, Category = "Keycard")
	FString GetSlotPath() const;

	// True when the primary or its backup exists (Continue is worth offering).
	UFUNCTION(BlueprintPure, Category = "Keycard")
	bool HasSave() const;

	UFUNCTION(BlueprintCallable, Category = "Keycard")
	bool Save(FString& OutError);

	// Checkpoint save; reports success or failure through UKcGameSubsystem::OnNotice.
	bool Autosave();

	UFUNCTION(BlueprintCallable, Category = "Keycard")
	EKcLoadOutcome Load(FString& OutMessage);

	// A challenge in progress saves here (<slot>_challenge), so it never overwrites the career.
	FString GetChallengePath() const;
	// Local best challenge results (<slot>_leaderboard.txt); nothing leaves the machine.
	std::vector<kc::LeaderboardEntry> LoadLeaderboard() const;
	bool SaveLeaderboard(const std::vector<kc::LeaderboardEntry>& Entries) const;

private:
	FString SlotName = TEXT("slot1");
};
