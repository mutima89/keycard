#include "KcInteractionComponent.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "KcCharacter.h"
#include "KcHUD.h"
#include "KcInteractable.h"

UKcInteractionComponent::UKcInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UKcInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller)
	{
		SetFocus(nullptr);
		return;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	Controller->GetPlayerViewPoint(ViewLocation, ViewRotation);
	const FVector End = ViewLocation + ViewRotation.Vector() * Range;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(KcInteract), false, GetOwner());
	FHitResult Hit;
	AKcInteractable* Candidate = nullptr;
	// A thin sphere sweep rather than a line: small props (binders, clipboards) are easy to target
	// without pixel-perfect aim, and walls still block it.
	if (GetWorld()->SweepSingleByChannel(Hit, ViewLocation, End, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(9.0f), Params))
	{
		Candidate = Cast<AKcInteractable>(Hit.GetActor());
		if (Candidate && !Candidate->IsUsable()) Candidate = nullptr;
	}
	SetFocus(Candidate);
}

void UKcInteractionComponent::SetFocus(AKcInteractable* NewFocus)
{
	if (Focused.Get() == NewFocus) return;
	if (AKcInteractable* Old = Focused.Get())
	{
		Old->SetFocused(false);
	}
	Focused = NewFocus;
	if (NewFocus)
	{
		NewFocus->SetFocused(true);
	}
}

FText UKcInteractionComponent::GetPromptText() const
{
	const AKcInteractable* Target = Focused.Get();
	return Target ? Target->GetPrompt() : FText::GetEmpty();
}

bool UKcInteractionComponent::Interact()
{
	AKcInteractable* Target = Focused.Get();
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	AKcHUD* Hud = PC ? Cast<AKcHUD>(PC->GetHUD()) : nullptr;
	if (!Target || !Hud) return false;
	AKcCharacter* Character = Cast<AKcCharacter>(GetOwner());
	if (!Character)
	{
		Target->Interact(Hud);
		return true;
	}
	if (Character->IsUsing()) return false;
	// Turn to it (and pick it up or lean in) first; the screen opens when the move ends.
	TWeakObjectPtr<AKcInteractable> WeakTarget(Target);
	TWeakObjectPtr<AKcHUD> WeakHud(Hud);
	Character->BeginUse(Target, [WeakTarget, WeakHud]()
	{
		if (WeakTarget.IsValid() && WeakHud.IsValid() && !WeakHud->IsScreenOpen()) WeakTarget->Interact(WeakHud.Get());
	});
	return true;
}
