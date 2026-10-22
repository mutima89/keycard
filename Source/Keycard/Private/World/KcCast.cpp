#include "KcCast.h"

#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "KcConvert.h"
#include "KcPeople.h"
#include "Misc/PackageName.h"

namespace KcCast
{
UAnimSequence* Anim(bool bFemale, const TCHAR* Name)
{
	const FString Path = FString::Printf(TEXT("/Game/Keycard/Characters/Anims/A_%s_%s.A_%s_%s"), bFemale ? TEXT("f") : TEXT("m"), Name, bFemale ? TEXT("f") : TEXT("m"), Name);
	// Missing loops are normal (not every loop exists for both bodies), so look quietly.
	if (!FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) return nullptr;
	return LoadObject<UAnimSequence>(nullptr, *Path);
}

namespace
{
// Rocketbox avatars import in centimetres facing +Y, like the engine mannequin.
constexpr float AvatarYaw = -90.0f;

struct FEntry
{
	FString Avatar;
	FString Outfit;
};
TMap<FString, FEntry>& Cast()
{
	static TMap<FString, FEntry> Map;
	return Map;
}

// Background staff and guests: Rocketbox adults no named person uses (Scripts/FetchCharacters.py fetches them too).
const TCHAR* const Extras[] = {
	TEXT("Male_Adult_01"), TEXT("Female_Adult_01"), TEXT("Male_Adult_02"), TEXT("Female_Adult_03"), TEXT("Male_Adult_03"),
	TEXT("Female_Adult_05"), TEXT("Male_Adult_04"), TEXT("Business_Male_01"), TEXT("Business_Female_01"),
};

// Idles cycle so nobody in a room moves in step with their neighbour.
const TCHAR* const StaffIdles[] = {TEXT("idle_neutral_01"), TEXT("idle_breathe_01"), TEXT("idle_neutral_02"), TEXT("idle_look_around_01"),
	TEXT("cell_phone_listen_01"), TEXT("idle_neutral_02")};

FLook Avatar(const FString& Name, const TCHAR* IdleName)
{
	FLook L;
	const FString Path = FString::Printf(TEXT("/Game/Keycard/Characters/People/%s/SK_%s.SK_%s"), *Name, *Name, *Name);
	L.bFemale = Name.Contains(TEXT("Female"));
	if (FPackageName::DoesPackageExist(FPackageName::ObjectPathToPackageName(Path))) L.Mesh = LoadObject<USkeletalMesh>(nullptr, *Path);
	if (L.Mesh)
	{
		L.Idle = Anim(L.bFemale, IdleName);
		if (!L.Idle) L.Idle = Anim(L.bFemale, TEXT("idle_neutral_01")); // not every loop exists for both bodies
		L.MeshYaw = AvatarYaw;
		L.bFallback = false;
		return L;
	}
	// Stand-in: the engine mannequin (Scripts/CopyEngineContent.bat).
	L.Mesh = LoadObject<USkeletalMesh>(nullptr, L.bFemale ? TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")
		: TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	L.Idle = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/Characters/Mannequins/Anims/Unarmed/MM_Idle.MM_Idle"));
	return L;
}
} // namespace

void SetPeople(const kc::PeopleCatalogue& People)
{
	Cast().Reset();
	for (const kc::CharacterDef& C : People.People)
	{
		Cast().Add(KcConvert::ToFString(C.Id), {KcConvert::ToFString(C.Avatar), KcConvert::ToFString(C.Outfit)});
	}
}

FLook ForPerson(const FString& PersonId, int32 Idle)
{
	const TCHAR* IdleName = Idle == 1 ? TEXT("gestic_listen_neutral_01") : Idle == 2 ? TEXT("idle_breathe_01") : TEXT("idle_neutral_01");
	if (const FEntry* E = Cast().Find(PersonId))
	{
		if (!E->Avatar.IsEmpty()) return Avatar(E->Avatar, IdleName);
	}
	return Avatar(Extras[GetTypeHash(PersonId) % UE_ARRAY_COUNT(Extras)], IdleName);
}

FLook ForStaff(int32 Index)
{
	const int32 I = FMath::Abs(Index);
	return Avatar(Extras[I % UE_ARRAY_COUNT(Extras)], StaffIdles[I % UE_ARRAY_COUNT(StaffIdles)]);
}

FString OutfitFor(const FString& PersonId)
{
	const FEntry* E = Cast().Find(PersonId);
	return E ? E->Outfit : FString();
}
} // namespace KcCast
