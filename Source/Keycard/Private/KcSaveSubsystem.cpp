#include "KcSaveSubsystem.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "KcConvert.h"
#include "KcGameSubsystem.h"
#include "KcLog.h"
#include "KcSave.h"

using namespace KcConvert;

void UKcSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UKcGameSubsystem>();
}

FString UKcSaveSubsystem::GetSlotPath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT(".kcsave")));
}

FString UKcSaveSubsystem::GetChallengePath() const
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT("_challenge.kcsave")));
}

static FString LeaderboardPath(const FString& SlotName)
{
	return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("SaveGames") / (SlotName + TEXT("_leaderboard.txt")));
}

std::vector<kc::LeaderboardEntry> UKcSaveSubsystem::LoadLeaderboard() const
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *LeaderboardPath(SlotName))) return {};
	return kc::DeserializeLeaderboard(ToStd(Text));
}

bool UKcSaveSubsystem::SaveLeaderboard(const std::vector<kc::LeaderboardEntry>& Entries) const
{
	const FString Path = LeaderboardPath(SlotName);
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	const bool bOk = FFileHelper::SaveStringToFile(ToFString(kc::SerializeLeaderboard(Entries)), *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogKeycard, Display, TEXT("Leaderboard to %s: %s"), *Path, bOk ? TEXT("ok") : TEXT("FAILED"));
	return bOk;
}

bool UKcSaveSubsystem::HasSave() const
{
	const FString Path = GetSlotPath();
	IFileManager& Files = IFileManager::Get();
	return Files.FileExists(*Path) || Files.FileExists(*ToFString(kc::BackupPath(ToStd(Path))));
}

bool UKcSaveSubsystem::Save(FString& OutError)
{
	const UKcGameSubsystem* Game = GetGameInstance()->GetSubsystem<UKcGameSubsystem>();
	if (!Game || !Game->HasGame())
	{
		OutError = TEXT("No game in progress.");
		return false;
	}
	const FString Path = Game->IsChallenge() ? GetChallengePath() : GetSlotPath();
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	std::string Error;
	const bool bOk = kc::SaveGameToFile(ToStd(Path), Game->GetState(), &Error);
	OutError = ToFString(Error);
	UE_LOG(LogKeycard, Display, TEXT("Save to %s: %s %s"), *Path, bOk ? TEXT("ok") : TEXT("FAILED"), *OutError);
	return bOk;
}

bool UKcSaveSubsystem::Autosave()
{
	UKcGameSubsystem* Game = GetGameInstance()->GetSubsystem<UKcGameSubsystem>();
	FString Error;
	const bool bOk = Save(Error);
	if (Game)
	{
		Game->Notify(bOk ? FString(TEXT("Progress autosaved")) : FString::Printf(TEXT("Autosave failed: %s"), *Error));
	}
	return bOk;
}

EKcLoadOutcome UKcSaveSubsystem::Load(FString& OutMessage)
{
	UKcGameSubsystem* Game = GetGameInstance()->GetSubsystem<UKcGameSubsystem>();
	const FString Path = GetSlotPath();
	kc::GameState Loaded;
	const kc::LoadResult Result = kc::LoadGameFromFile(ToStd(Path), Loaded);
	OutMessage = ToFString(Result.Message);
	UE_LOG(LogKeycard, Display, TEXT("Load %s: %s %s"), *Path, *ToFString(kc::ToString(Result.Status)), *OutMessage);

	switch (Result.Status)
	{
	case kc::ELoadStatus::Ok:
		Game->SetLoadedState(MoveTemp(Loaded));
		Game->ApplySettings(false);
		return EKcLoadOutcome::Ok;
	case kc::ELoadStatus::RecoveredFromBackup:
		Game->SetLoadedState(MoveTemp(Loaded));
		Game->ApplySettings(false);
		OutMessage = TEXT("Your latest save was damaged, so the previous save was restored from backup. ") + OutMessage;
		Game->Notify(OutMessage);
		return EKcLoadOutcome::RecoveredFromBackup;
	case kc::ELoadStatus::NotFound:
		OutMessage = TEXT("No saved game was found.");
		return EKcLoadOutcome::NotFound;
	case kc::ELoadStatus::UnsupportedVersion:
		OutMessage = TEXT("This save was made by a newer version of Keycard. ") + OutMessage;
		return EKcLoadOutcome::UnsupportedVersion;
	case kc::ELoadStatus::Corrupt:
		break;
	}
	OutMessage = TEXT("The save and its backup are both damaged and cannot be loaded. ") + OutMessage;
	return EKcLoadOutcome::Corrupt;
}
