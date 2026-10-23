// Blueprint-facing enums and views of KeycardSim state. Copies for display only: nothing here
// scores, decides or computes an LP figure (Docs/Architecture.md).
#pragma once

#include "CoreMinimal.h"
#include "KcTypes.generated.h"

UENUM(BlueprintType)
enum class EKcDifficulty : uint8
{
	Trainee,
	Professional,
	Expert
};

UENUM(BlueprintType)
enum class EKcLoadOutcome : uint8
{
	Ok,
	RecoveredFromBackup,
	NotFound,
	Corrupt,
	UnsupportedVersion
};

// Result of a casework call (kc::EMissionError, plus the UE-only UnknownMission).
UENUM(BlueprintType)
enum class EKcCaseResult : uint8
{
	Ok,
	Unavailable,
	AnotherMissionActive,
	NotActive,
	UnknownAction,
	AlreadyDone,
	WrongPlace,
	NeedsEarlierStep,
	RequiredStepMissing,
	UnansweredQuestion,
	AnsweredInPerson,
	UnknownOption,
	SingleNeedsOneOption,
	OrderNeedsEveryOption,
	FileWhereReportsAre,
	NotSubmitted,
	UnknownMission
};

USTRUCT(BlueprintType)
struct FKcSettingsView
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") float MouseSensitivity = 1.0f;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") float TextScale = 1.0f;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") float MasterVolume = 0.8f;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bReducedMotion = false;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bHints = true;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bFullscreen = true;
	// 0 Low .. 3 Epic (engine scalability presets; Low turns Lumen off).
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") int32 GraphicsQuality = 2;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") int32 Upscaling = 0;   // 0 native .. 3 performance (TSR)
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") int32 FrameCap = 0;    // 0 unlimited, 30, 60, 120
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bVSync = true;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bInvertY = false;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") float GamepadSensitivity = 1.0f;
	UPROPERTY(BlueprintReadWrite, Category = "Keycard") bool bColourSafe = false;
};
