#include "KcInteractable.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "KcCampaignSubsystem.h"
#include "KcHUD.h"
#include "KcPerson.h"
#include "Materials/MaterialInterface.h"

#define LOCTEXT_NAMESPACE "Keycard"

static UMaterialInterface* HighlightMaterial()
{
	static TWeakObjectPtr<UMaterialInterface> Cached;
	if (!Cached.IsValid())
	{
		Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Keycard/Materials/Instances/MI_Highlight.MI_Highlight"));
	}
	return Cached.Get();
}

AKcInteractable::AKcInteractable()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("BlockAll"));
	RootComponent = Mesh;
}

void AKcInteractable::SetAppearance(UStaticMesh* InMesh, const FVector& Scale, UMaterialInterface* Material)
{
	Mesh->SetStaticMesh(InMesh);
	Mesh->SetWorldScale3D(Scale);
	if (Material)
	{
		for (int32 i = 0; i < Mesh->GetNumMaterials(); ++i) Mesh->SetMaterial(i, Material);
	}
}

void AKcInteractable::SetFocused(bool bFocused)
{
	UMaterialInterface* Overlay = bFocused ? HighlightMaterial() : nullptr;
	TArray<UMeshComponent*> Meshes;
	GetComponents<UMeshComponent>(Meshes);
	for (UMeshComponent* M : Meshes)
	{
		if (M->IsVisible()) M->SetOverlayMaterial(Overlay);
	}
}

FVector AKcInteractable::GetFocusPoint() const
{
	return Mesh ? Mesh->Bounds.Origin : GetActorLocation();
}

UStaticMesh* AKcInteractable::GetPickUpMesh(FTransform& OutFrom) const
{
	UStaticMesh* M = Mesh ? Mesh->GetStaticMesh() : nullptr;
	if (!M || Mesh->Bounds.BoxExtent.GetMax() > 40.0f) return nullptr; // only small things are lifted
	OutFrom = Mesh->GetComponentTransform();
	return M;
}

static const UKcCampaignSubsystem* CampaignOf(const AActor* A)
{
	const UGameInstance* GI = A ? A->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UKcCampaignSubsystem>() : nullptr;
}

// ---------------------------------------------------------------- people

AKcPersonActor::AKcPersonActor()
{
	Body = CreateDefaultSubobject<UKcPersonComponent>(TEXT("Body"));
	Body->SetupAttachment(Mesh);
	Body->SetAbsolute(false, false, true);
}

void AKcPersonActor::SetBody(const KcCast::FLook& Look, UMaterialInterface* Outfit)
{
	if (!Look.Mesh) return;
	Mesh->SetVisibility(false); // the capsule stays for collision and the interaction trace
	// The capsule is centred on the actor; the body's origin is at its feet.
	Body->SetRelativeLocation(FVector(0.0f, 0.0f, -90.0f / FMath::Max(GetActorScale3D().Z, 0.01f)));
	Body->Setup(Look, PersonId, GetActorRotation().Yaw, GetTypeHash(PersonId) ^ GetTypeHash(GetActorLocation()));
	if (Outfit && Look.bFallback)
	{
		for (int32 i = 0; i < Body->GetNumMaterials(); ++i) Body->SetMaterial(i, Outfit);
	}
}

FVector AKcPersonActor::GetFocusPoint() const
{
	return Body && Body->GetSkinnedAsset() ? Body->GetEyeLocation() : GetActorLocation() + FVector(0.0f, 0.0f, 70.0f);
}

FText AKcPersonActor::GetPrompt() const
{
	const UKcCampaignSubsystem* C = CampaignOf(this);
	const bool bCasework = C && (!C->ActionsAt(Location, PersonId).IsEmpty() || !C->QuestionsAt(Location, PersonId).IsEmpty());
	if (bCasework) return FText::Format(LOCTEXT("TalkCase", "Talk to {0}, {1}"), FText::FromString(DisplayName), FText::FromString(Role));
	return FText::Format(LOCTEXT("TalkPrompt", "Talk to {0}"), FText::FromString(DisplayName));
}

void AKcPersonActor::Interact(AKcHUD* Hud)
{
	Hud->TalkTo(this);
}

// ---------------------------------------------------------------- case points

TArray<const kc::ActionDef*> AKcCasePoint::Work() const
{
	TArray<const kc::ActionDef*> Out;
	const UKcCampaignSubsystem* C = CampaignOf(this);
	if (!C) return Out;
	Out = C->ActionsAtSpot(Location, Spot);
	if (!DoorPersonId.IsEmpty()) Out.Append(C->ActionsAt(DoorPersonLocation, DoorPersonId));
	return Out;
}

bool AKcCasePoint::IsUsable() const
{
	return !Work().IsEmpty();
}

FText AKcCasePoint::GetPrompt() const
{
	return FText::Format(LOCTEXT("UsePoint", "Use: {0}"), FText::FromString(Title));
}

void AKcCasePoint::Interact(AKcHUD* Hud)
{
	Hud->UseCasePoint(this);
}

FVector AKcCasePoint::GetFocusPoint() const
{
	return Focus.IsZero() ? AKcInteractable::GetFocusPoint() : Focus;
}

// ---------------------------------------------------------------- terminal, lift, signs

FText AKcReportTerminal::GetPrompt() const
{
	const UKcCampaignSubsystem* C = CampaignOf(this);
	if (C && C->Active()) return LOCTEXT("TerminalReport", "Use terminal: write and file the incident report");
	return LOCTEXT("TerminalShift", "Use terminal: shift board, Field Manual, career");
}

void AKcReportTerminal::Interact(AKcHUD* Hud)
{
	Hud->OpenTerminal(Location);
}

FText AKcLiftPanel::GetPrompt() const
{
	return LOCTEXT("LiftPrompt", "Call the lift");
}

void AKcLiftPanel::Interact(AKcHUD* Hud)
{
	Hud->OpenLift(Floor);
}

FText AKcSign::GetPrompt() const
{
	return FText::FromString(Title);
}

void AKcSign::Interact(AKcHUD* Hud)
{
	Hud->Notice(Message);
}

#undef LOCTEXT_NAMESPACE
