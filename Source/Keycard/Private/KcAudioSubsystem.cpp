#include "KcAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "KcCampaignSubsystem.h"
#include "KcGameSubsystem.h"
#include "KcLog.h"
#include "KcUIStyle.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace
{
// Asset paths (object path form) under /Game/Keycard/Audio; see Scripts/ImportAssets.py.
const TCHAR* const CuePaths[] = {
	TEXT("UI/UI_Click"), TEXT("UI/UI_Confirm"), TEXT("UI/UI_Back"), TEXT("UI/UI_Notice"), TEXT("UI/UI_Message"),
	TEXT("Stingers/STG_Incident"), TEXT("Stingers/STG_Promotion"), TEXT("Stingers/STG_Pass"), TEXT("Stingers/STG_Fail"),
	TEXT("Stingers/STG_Burnout"), nullptr};
const float CueVolume[] = {0.35f, 0.5f, 0.35f, 0.4f, 0.55f, 0.7f, 0.75f, 0.6f, 0.55f, 0.6f, 0.22f};
const TCHAR* const MoodPaths[] = {nullptr, TEXT("Music/MUS_Menu"), TEXT("Music/MUS_Focus"), TEXT("Music/MUS_Tension"), TEXT("Music/MUS_Crisis")};
// Per track, so every mood plays at about -28.7 LUFS: 8 LU under the voice lines (median -20.7 LUFS at volume 1).
// Source loudness (ebur128): Menu -25.6, Focus -20.5, Tension -24.3, Crisis -12.6 LUFS (Docs/QAReport.md, audio check).
const float MoodVolume[] = {0.0f, 0.70f, 0.39f, 0.60f, 0.16f};
constexpr float RoomToneVolume = 0.28f;
constexpr float CrossfadeSeconds = 3.0f;
constexpr float MinMoodSeconds = 20.0f;
constexpr int32 FootstepCount = 5;

FString AudioPath(const FString& Rel)
{
	const FString Name = FPaths::GetBaseFilename(Rel);
	return FString::Printf(TEXT("/Game/Keycard/Audio/%s.%s"), *Rel, *Name);
}
} // namespace

void UKcAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	static_assert(UE_ARRAY_COUNT(CuePaths) == static_cast<int32>(EKcCue::Count), "one path per cue");
	static_assert(UE_ARRAY_COUNT(CueVolume) == static_cast<int32>(EKcCue::Count), "one volume per cue");
	static_assert(UE_ARRAY_COUNT(MoodPaths) == static_cast<int32>(EKcMusicMood::Count), "one track per mood");
	static_assert(UE_ARRAY_COUNT(MoodVolume) == static_cast<int32>(EKcMusicMood::Count), "one volume per mood");
	for (const TCHAR* P : CuePaths) CueSounds.Add(P ? Load(P) : nullptr);
	for (int32 i = 1; i <= FootstepCount; ++i)
	{
		if (USoundBase* S = Load(*FString::Printf(TEXT("Foley/FS_Step_%02d"), i))) FootstepSounds.Add(S);
	}
	for (const TCHAR* P : MoodPaths) MoodSounds.Add(P ? Load(P) : nullptr);
	RoomTone = Load(TEXT("Ambience/AMB_RoomTone"));
	for (const TCHAR* Name : {TEXT("AMB_Servers"), TEXT("AMB_Hvac"), TEXT("AMB_Office")})
	{
		if (USoundBase* S = Load(*FString::Printf(TEXT("Ambience/%s"), Name))) Ambiences.Add(Name, S);
	}
	KcUI::SetClickSound(CueSounds[static_cast<int32>(EKcCue::Click)]);
	Attenuation = NewObject<USoundAttenuation>(this);
	Attenuation->Attenuation.bAttenuate = true;
	Attenuation->Attenuation.bSpatialize = true;
	Attenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	Attenuation->Attenuation.FalloffDistance = 900.0f;
	VoiceAttenuation = NewObject<USoundAttenuation>(this);
	VoiceAttenuation->Attenuation.bAttenuate = true;
	VoiceAttenuation->Attenuation.bSpatialize = true;
	VoiceAttenuation->Attenuation.AttenuationShape = EAttenuationShape::Sphere;
	VoiceAttenuation->Attenuation.AttenuationShapeExtents = FVector(350.0f, 0.0f, 0.0f); // conversation distance: full volume
	VoiceAttenuation->Attenuation.FalloffDistance = 1400.0f;
	VoiceAttenuation->Attenuation.NonSpatializedRadiusStart = 250.0f; // face to face: centred, not panned hard
	VoiceAttenuation->Attenuation.NonSpatializedRadiusEnd = 120.0f;
	UE_LOG(LogKeycard, Display, TEXT("Audio: %d sounds loaded"), LoadedSoundCount());
}

void UKcAudioSubsystem::Deinitialize()
{
	for (UAudioComponent* C : {MusicA.Get(), MusicB.Get(), RoomToneComponent.Get()})
	{
		if (IsValid(C)) C->Stop();
	}
	Super::Deinitialize();
}

USoundBase* UKcAudioSubsystem::Load(const TCHAR* Rel)
{
	return LoadObject<USoundBase>(nullptr, *AudioPath(Rel), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

USoundWave* UKcAudioSubsystem::FindVoice(const FString& Key)
{
	if (Key.IsEmpty()) return nullptr;
	const FString Name = TEXT("VO_") + Key;
	return LoadObject<USoundWave>(nullptr, *FString::Printf(TEXT("/Game/Keycard/Audio/Voice/%s.%s"), *Name, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UAudioComponent* UKcAudioSubsystem::PlayVoice(USoundWave* Voice, USceneComponent* AttachTo, FName Socket)
{
	if (!Voice || !AttachTo) return nullptr;
	return UGameplayStatics::SpawnSoundAttached(Voice, AttachTo, Socket, FVector::ZeroVector, EAttachLocation::KeepRelativeOffset,
		true, 1.0f, 1.0f, 0.0f, VoiceAttenuation);
}

int32 UKcAudioSubsystem::LoadedSoundCount() const
{
	int32 N = FootstepSounds.Num() + Ambiences.Num() + (RoomTone ? 1 : 0);
	for (const USoundBase* S : CueSounds) N += S ? 1 : 0;
	for (const USoundBase* S : MoodSounds) N += S ? 1 : 0;
	return N;
}

void UKcAudioSubsystem::Cue(const UObject* WorldContext, EKcCue InCue)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	if (!GI)
	{
		if (const UGameInstanceSubsystem* Sub = Cast<UGameInstanceSubsystem>(WorldContext)) GI = Sub->GetGameInstance();
	}
	if (UKcAudioSubsystem* Audio = GI ? GI->GetSubsystem<UKcAudioSubsystem>() : nullptr) Audio->PlayCue(InCue);
}

void UKcAudioSubsystem::PlayCue(EKcCue InCue)
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || World->IsNetMode(NM_DedicatedServer)) return;
	const int32 Index = static_cast<int32>(InCue);
	// The same cue twice within 80 ms is one event (several notices arriving together).
	const double Now = FPlatformTime::Seconds();
	if (Now - LastCueTime[Index] < 0.08) return;
	LastCueTime[Index] = Now;
	USoundBase* Sound = nullptr;
	if (InCue == EKcCue::Footstep)
	{
		if (FootstepSounds.IsEmpty()) return;
		int32 Pick = FMath::RandRange(0, FootstepSounds.Num() - 1);
		if (Pick == LastFootstep && FootstepSounds.Num() > 1) Pick = (Pick + 1) % FootstepSounds.Num();
		LastFootstep = Pick;
		Sound = FootstepSounds[Pick];
	}
	else
	{
		Sound = CueSounds.IsValidIndex(Index) ? CueSounds[Index].Get() : nullptr;
	}
	if (!Sound) return;
	const float Pitch = InCue == EKcCue::Footstep ? FMath::FRandRange(0.94f, 1.06f) : 1.0f;
	UGameplayStatics::PlaySound2D(World, Sound, CueVolume[Index], Pitch);
}

UAudioComponent* UKcAudioSubsystem::SpawnAmbience(UWorld* World, const FName& Name, const FVector& Location, float Radius, float Volume)
{
	const TObjectPtr<USoundBase>* Sound = Ambiences.Find(Name);
	if (!World || !Sound || !*Sound) return nullptr;
	USoundAttenuation* Att = NewObject<USoundAttenuation>(World);
	Att->Attenuation = Attenuation->Attenuation;
	Att->Attenuation.AttenuationShapeExtents = FVector(Radius * 0.35f, 0.0f, 0.0f);
	Att->Attenuation.FalloffDistance = Radius;
	return UGameplayStatics::SpawnSoundAtLocation(World, *Sound, Location, FRotator::ZeroRotator, Volume, 1.0f, 0.0f, Att, nullptr, false);
}

EKcMusicMood UKcAudioSubsystem::DesiredMood() const
{
	const UGameInstance* GI = GetGameInstance();
	const UKcGameSubsystem* Game = GI ? GI->GetSubsystem<UKcGameSubsystem>() : nullptr;
	if (!Game || !Game->HasGame()) return EKcMusicMood::Menu;
	const kc::GameState& S = Game->GetState();
	const UKcCampaignSubsystem* Campaign = GI->GetSubsystem<UKcCampaignSubsystem>();
	if (Campaign)
	{
		const kc::MissionDef* Active = Campaign->GetCatalogue().Find(S.Campaign.ActiveMissionId);
		if (Active && Active->Kind != kc::EMissionKind::Mission) return EKcMusicMood::Crisis; // a crisis or the capstone in progress
		for (const kc::MissionDef& M : Campaign->GetCatalogue().Missions)
		{
			if (M.DeadlineWeeks > 0 && kc::CheckAvailability(S, M).bAvailable && !S.Career.HasPassed(M.Id)) return EKcMusicMood::Tension;
		}
	}
	for (const kc::InboxMessage& M : S.World.Inbox)
	{
		if (!M.bRead && M.Category == kc::EMessageCategory::Incident) return EKcMusicMood::Tension;
	}
	if (S.World.Stress >= 70 || kc::AwaitingReplyCount(S) >= 3) return EKcMusicMood::Tension;
	return EKcMusicMood::Focus;
}

void UKcAudioSubsystem::SetMood(EKcMusicMood NewMood)
{
	if (NewMood == Mood) return;
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World) return;
	Mood = NewMood;
	MoodHoldTimer = MinMoodSeconds;
	UAudioComponent* Old = bMusicOnA ? MusicA.Get() : MusicB.Get();
	if (IsValid(Old)) Old->FadeOut(CrossfadeSeconds, 0.0f);
	USoundBase* Track = MoodSounds.IsValidIndex(static_cast<int32>(NewMood)) ? MoodSounds[static_cast<int32>(NewMood)].Get() : nullptr;
	if (!Track) return;
	UAudioComponent* New = UGameplayStatics::CreateSound2D(World, Track, MoodVolume[static_cast<int32>(NewMood)], 1.0f, 0.0f, nullptr, true, false);
	if (!New) return;
	New->bIsUISound = true; // keeps playing while the game is paused behind a menu
	New->FadeIn(CrossfadeSeconds, 1.0f); // the fader multiplies the component volume, so fade to 1, not to the volume again
	bMusicOnA = !bMusicOnA;
	(bMusicOnA ? MusicA : MusicB) = New;
}

void UKcAudioSubsystem::EnsureRoomTone()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World || !RoomTone || (IsValid(RoomToneComponent) && RoomToneComponent->IsPlaying())) return;
	RoomToneComponent = UGameplayStatics::CreateSound2D(World, RoomTone, RoomToneVolume, 1.0f, 0.0f, nullptr, true, false);
	if (RoomToneComponent) RoomToneComponent->FadeIn(4.0f, 1.0f);
}

void UKcAudioSubsystem::Tick(float DeltaTime)
{
	if (!GetGameInstance() || !GetGameInstance()->GetWorld()) return;
	MoodHoldTimer -= DeltaTime;
	MoodCheckTimer -= DeltaTime;
	if (MoodCheckTimer > 0.0f) return;
	MoodCheckTimer = 1.0f;
	EnsureRoomTone();
	const EKcMusicMood Want = DesiredMood();
	// Escalation is immediate; calming down waits for the hold so a single read message does not flip the score.
	if (Want != Mood && (static_cast<int32>(Want) > static_cast<int32>(Mood) || MoodHoldTimer <= 0.0f || Mood == EKcMusicMood::Menu || Want == EKcMusicMood::Menu))
	{
		SetMood(Want);
	}
}

TStatId UKcAudioSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UKcAudioSubsystem, STATGROUP_Tickables);
}
