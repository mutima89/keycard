#include "KcGameSubsystem.h"

#include "AudioDevice.h"
#include "Engine/Engine.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcLog.h"
#include "KcSaveSubsystem.h"
#include "KcUIStyle.h"
#include "Misc/DateTime.h"

using namespace KcConvert;

namespace
{
kc::EDifficulty ToSim(EKcDifficulty D)
{
	switch (D)
	{
	case EKcDifficulty::Trainee: return kc::EDifficulty::Trainee;
	case EKcDifficulty::Expert: return kc::EDifficulty::Expert;
	default: return kc::EDifficulty::Professional;
	}
}

kc::EBackground BackgroundFromName(const FString& Name)
{
	if (Name == TEXT("ex_police")) return kc::EBackground::ExPolice;
	if (Name == TEXT("military")) return kc::EBackground::Military;
	if (Name == TEXT("criminology")) return kc::EBackground::CriminologyGraduate;
	return kc::EBackground::FrontOffice;
}
} // namespace

void UKcGameSubsystem::NewGame(EKcDifficulty Difficulty, const FString& ProfileName, const FString& Background, int64 Seed)
{
	// A fixed seed for tests and the classroom; otherwise the clock (the career is still reproducible from the save).
	const uint64 UseSeed = Seed != 0 ? static_cast<uint64>(Seed) : static_cast<uint64>(FDateTime::UtcNow().GetTicks());
	State = kc::NewGame(ToStd(ProfileName.IsEmpty() ? FString(TEXT("Officer")) : ProfileName), BackgroundFromName(Background), ToSim(Difficulty), UseSeed);
	if (const UKcCampaignSubsystem* Campaign = GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>())
	{
		Campaign->InitWorldFor(State);
	}
	bHasGame = true;
	ApplySettings(false);
	UE_LOG(LogKeycard, Display, TEXT("New game: %s, seed %llu"), *ProfileName, UseSeed);
	AfterAction(true);
}

EKcLoadOutcome UKcGameSubsystem::Continue(FString& OutMessage)
{
	UKcSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UKcSaveSubsystem>();
	return Save ? Save->Load(OutMessage) : EKcLoadOutcome::NotFound;
}

void UKcGameSubsystem::SetLoadedState(kc::GameState&& Loaded)
{
	State = MoveTemp(Loaded);
	// A save from before the cast existed (or with new people added since) gets their starting relations.
	if (const UKcCampaignSubsystem* Campaign = GetGameInstance()->GetSubsystem<UKcCampaignSubsystem>())
	{
		if (State.World.Relations.empty()) Campaign->InitWorldFor(State);
	}
	bHasGame = true;
	OnStateChanged.Broadcast();
}

EKcDifficulty UKcGameSubsystem::GetDifficulty() const
{
	switch (State.Difficulty)
	{
	case kc::EDifficulty::Trainee: return EKcDifficulty::Trainee;
	case kc::EDifficulty::Expert: return EKcDifficulty::Expert;
	default: return EKcDifficulty::Professional;
	}
}

FString UKcGameSubsystem::GetLevelTitle() const
{
	return ToFString(kc::LevelDef(State.Career.Level).Title);
}

void UKcGameSubsystem::CeremonyPlayed()
{
	if (!State.bCeremonyPending) return;
	State.bCeremonyPending = false;
	AfterAction(true);
}

void UKcGameSubsystem::AfterAction(bool bAutosave)
{
	if (bAutosave && bHasGame)
	{
		if (UKcSaveSubsystem* Save = GetGameInstance()->GetSubsystem<UKcSaveSubsystem>())
		{
			Save->Autosave();
		}
	}
	OnStateChanged.Broadcast();
}

void UKcGameSubsystem::Notify(const FString& Message)
{
	UE_LOG(LogKeycard, Display, TEXT("Keycard: %s"), *Message);
	OnNotice.Broadcast(Message);
}

FKcSettingsView UKcGameSubsystem::GetSettings() const
{
	const kc::PlayerSettings& S = State.Settings;
	FKcSettingsView V;
	V.MouseSensitivity = static_cast<float>(S.MouseSensitivity);
	V.TextScale = static_cast<float>(S.TextScale);
	V.MasterVolume = static_cast<float>(S.MasterVolume);
	V.bReducedMotion = S.bReducedMotion;
	V.bHints = S.bHints;
	V.bFullscreen = S.bFullscreen;
	V.GraphicsQuality = S.GraphicsQuality;
	V.Upscaling = S.Upscaling;
	V.FrameCap = S.FrameCap;
	V.bVSync = S.bVSync;
	V.bInvertY = S.bInvertY;
	V.GamepadSensitivity = static_cast<float>(S.GamepadSensitivity);
	V.bColourSafe = S.bColourSafe;
	return V;
}

void UKcGameSubsystem::SetSettings(const FKcSettingsView& In, bool bApplyWindowMode)
{
	kc::PlayerSettings S = State.Settings;
	S.MouseSensitivity = In.MouseSensitivity;
	S.TextScale = In.TextScale;
	S.MasterVolume = In.MasterVolume;
	S.bReducedMotion = In.bReducedMotion;
	S.bHints = In.bHints;
	S.bFullscreen = In.bFullscreen;
	S.GraphicsQuality = In.GraphicsQuality;
	S.Upscaling = In.Upscaling;
	S.FrameCap = In.FrameCap;
	S.bVSync = In.bVSync;
	S.bInvertY = In.bInvertY;
	S.GamepadSensitivity = In.GamepadSensitivity;
	S.bColourSafe = In.bColourSafe;
	State.Settings = kc::ClampSettings(S);
	ApplySettings(bApplyWindowMode);
	OnStateChanged.Broadcast();
}

void UKcGameSubsystem::ApplySettings(bool bApplyWindowMode) const
{
	const kc::PlayerSettings& S = State.Settings;
	KcUI::SetTextScale(static_cast<float>(S.TextScale));
	KcUI::SetReducedMotion(S.bReducedMotion);
	KcUI::SetColourSafe(S.bColourSafe);
	// Internal render resolution per quality preset; TSR upscales to the output resolution.
	if (IConsoleVariable* ScreenPct = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
	{
		static const float Percent[] = {50.0f, 60.0f, 70.0f, 100.0f};
		static const float Upscale[] = {100.0f, 77.0f, 67.0f, 58.0f}; // TSR quality, balanced, performance
		const float ByQuality = Percent[FMath::Clamp(S.GraphicsQuality, 0, 3)];
		ScreenPct->Set(S.Upscaling > 0 ? FMath::Min(ByQuality, Upscale[FMath::Clamp(S.Upscaling, 0, 3)]) : ByQuality, ECVF_SetByGameSetting);
	}
	if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) MaxFps->Set(S.FrameCap, ECVF_SetByGameSetting);
	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync"))) VSync->Set(S.bVSync ? 1 : 0, ECVF_SetByGameSetting);
	if (IConsoleVariable* MotionBlur = IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlur->Set(S.bReducedMotion ? 0 : 4, ECVF_SetByGameSetting);
	}
	if (!GEngine) return;
	if (FAudioDeviceHandle Audio = GEngine->GetMainAudioDevice())
	{
		Audio->SetTransientPrimaryVolume(static_cast<float>(S.MasterVolume));
	}
	// Scalability and window mode are never changed in the editor (they would change the editor's own settings).
	if (GIsEditor) return;
	if (UGameUserSettings* User = GEngine->GetGameUserSettings())
	{
		if (User->GetOverallScalabilityLevel() != S.GraphicsQuality)
		{
			User->SetOverallScalabilityLevel(S.GraphicsQuality);
			User->ApplyNonResolutionSettings();
			User->SaveSettings();
		}
		if (bApplyWindowMode)
		{
			User->SetFullscreenMode(S.bFullscreen ? EWindowMode::WindowedFullscreen : EWindowMode::Windowed);
			User->ApplyResolutionSettings(false);
			User->SaveSettings();
		}
	}
}
