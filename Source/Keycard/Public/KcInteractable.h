// Anything the player can focus and use. Each carries a place (a location id from KcMission) and
// hands the use to the HUD; no casework rules live here.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KcCast.h"
#include "KcInteractable.generated.h"

class AKcHUD;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UKcPersonComponent;
namespace kc { struct ActionDef; }

// How using a thing looks before its screen opens: documents are picked up, screens are leaned
// into, people are turned to face.
enum class EKcUseStyle : uint8
{
	None,
	PickUp,
	Screen,
	Person
};

UCLASS(Abstract)
class KEYCARD_API AKcInteractable : public AActor
{
	GENERATED_BODY()

public:
	AKcInteractable();

	// Mesh, scale and optional material override (nullptr keeps the mesh's own materials).
	void SetAppearance(UStaticMesh* InMesh, const FVector& Scale, UMaterialInterface* Material = nullptr);

	// The location (KnownLocations id) this thing is in: casework done with it happens there.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Keycard")
	FString Location;

	virtual FText GetPrompt() const PURE_VIRTUAL(AKcInteractable::GetPrompt, return FText::GetEmpty(););
	virtual void Interact(AKcHUD* Hud) PURE_VIRTUAL(AKcInteractable::Interact, );
	// False hides the prompt and ignores E (a case point with nothing to do).
	virtual bool IsUsable() const { return true; }

	// Focus feedback: a fresnel overlay on every mesh of the actor.
	void SetFocused(bool bFocused);

	virtual EKcUseStyle GetUseStyle() const { return EKcUseStyle::None; }
	// Where the camera looks when using it (a person's eyes, a screen's centre, a document).
	virtual FVector GetFocusPoint() const;
	// The mesh lifted towards the camera for PickUp.
	virtual UStaticMesh* GetPickUpMesh(FTransform& OutFrom) const;
	// The person behind it, if any (lip-sync, gestures and reactions during conversations).
	virtual UKcPersonComponent* GetPerson() const { return nullptr; }

protected:
	UPROPERTY(VisibleAnywhere, Category = "Keycard")
	TObjectPtr<UStaticMeshComponent> Mesh;
};

// A person in the hotel (People.json id). During a case they offer the actions and questions the
// case has for them here; otherwise a word in passing.
UCLASS()
class KEYCARD_API AKcPersonActor : public AKcInteractable
{
	GENERATED_BODY()

public:
	AKcPersonActor();

	FString PersonId;
	FString DisplayName;
	FString Role;

	// Cast body and an outfit material for the stand-in (Outfit may be null). Idle 1: listening.
	void SetBody(const KcCast::FLook& Look, UMaterialInterface* Outfit);
	// No longer wanted here: hidden and still at once, destroyed a moment later.
	void Retire();
	bool IsRetired() const { return bRetired; }

	virtual FText GetPrompt() const override;
	virtual void Interact(AKcHUD* Hud) override;
	virtual EKcUseStyle GetUseStyle() const override { return EKcUseStyle::Person; }
	virtual FVector GetFocusPoint() const override;
	virtual UKcPersonComponent* GetPerson() const override { return Body; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Keycard")
	TObjectPtr<UKcPersonComponent> Body;

	bool bRetired = false;
};

// A place where work is done: a named spot (the CCTV console, the lock on 1412), or a location's own
// case point (Spot empty) for work with no prop of its own.
UCLASS()
class KEYCARD_API AKcCasePoint : public AKcInteractable
{
	GENERATED_BODY()

public:
	FString Spot;
	FString Title;         // "CCTV console"
	EKcUseStyle Style = EKcUseStyle::Screen;
	FVector Focus = FVector::ZeroVector;
	// A guest behind this closed door (the hotel sets it until they open up): their face-to-face
	// work (knock, speak through the door) is offered here too, at their own location.
	FString DoorPersonId;
	FString DoorPersonLocation;

	// The spot's work, then the work with the person behind the door.
	TArray<const kc::ActionDef*> Work() const;

	virtual FText GetPrompt() const override;
	virtual void Interact(AKcHUD* Hud) override;
	virtual bool IsUsable() const override;
	virtual EKcUseStyle GetUseStyle() const override { return Style; }
	virtual FVector GetFocusPoint() const override;
};

// The LP office terminal: the shift board (start the next case), the incident report and the
// Field Manual.
UCLASS()
class KEYCARD_API AKcReportTerminal : public AKcInteractable
{
	GENERATED_BODY()

public:
	virtual FText GetPrompt() const override;
	virtual void Interact(AKcHUD* Hud) override;
	virtual EKcUseStyle GetUseStyle() const override { return EKcUseStyle::Screen; }
};

// Lift call panel: choose a floor. The ride is a short fade with the doors and a chime.
UCLASS()
class KEYCARD_API AKcLiftPanel : public AKcInteractable
{
	GENERATED_BODY()

public:
	int32 Floor = 0;
	virtual FText GetPrompt() const override;
	virtual void Interact(AKcHUD* Hud) override;
	virtual EKcUseStyle GetUseStyle() const override { return EKcUseStyle::Screen; }
};

// A stair or service door that does not open tonight: says why.
UCLASS()
class KEYCARD_API AKcSign : public AKcInteractable
{
	GENERATED_BODY()

public:
	FString Title;
	FString Message;
	virtual FText GetPrompt() const override;
	virtual void Interact(AKcHUD* Hud) override;
};
