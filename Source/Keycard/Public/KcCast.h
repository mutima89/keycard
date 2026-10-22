#pragma once

#include "CoreMinimal.h"

class USkeletalMesh;
class UAnimSequence;

// How the hotel's people look: each named person has the Rocketbox avatar given in Data/People.json
// (Microsoft Rocketbox, MIT licence, imported by Scripts/ImportCharacters.py); background staff and
// guests draw from the avatars no named person uses.
// Falls back to the engine mannequin when the imported avatars are missing.
namespace kc { struct PeopleCatalogue; }

namespace KcCast
{
// The cast table (UKcCampaignSubsystem calls it once the content has loaded).
void SetPeople(const kc::PeopleCatalogue& People);

struct FLook
{
	USkeletalMesh* Mesh = nullptr;
	UAnimSequence* Idle = nullptr;
	float MeshYaw = -90.0f;  // turns the mesh to face the actor's forward axis
	float Scale = 1.0f;
	bool bFallback = true;   // the mannequin stand-in (callers dress it in an outfit material)
	bool bFemale = false;    // picks the f_/m_ animation set
};

// A Rocketbox animation for one body (Name without the f_/m_ prefix, e.g. "gestic_talk_neutral_01"),
// or null when it was not imported. The mannequin stand-in has none.
UAnimSequence* Anim(bool bFemale, const TCHAR* Name);

// A named person (People.json id). Idle: 0 standing, 1 listening (interviews), 2 relaxed breathing.
FLook ForPerson(const FString& PersonId, int32 Idle = 0);
// Background staff member number Index (stable per index, varied idles).
FLook ForStaff(int32 Index);
// The outfit a named person wears (People.json "outfit"), "" if none.
FString OutfitFor(const FString& PersonId);
}
