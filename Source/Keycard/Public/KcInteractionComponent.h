// Traces from the player's view each frame, tracks the focused interactable and its prompt.
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "KcInteractionComponent.generated.h"

class AKcInteractable;

UCLASS(ClassGroup = (Keycard), meta = (BlueprintSpawnableComponent))
class KEYCARD_API UKcInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UKcInteractionComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	UPROPERTY(EditAnywhere, Category = "Keycard")
	float Range = 300.0f;

	AKcInteractable* GetFocused() const { return Focused.Get(); }

	// Empty when nothing is in focus.
	UFUNCTION(BlueprintPure, Category = "Keycard")
	FText GetPromptText() const;

	// Uses the focused interactable. Returns false when nothing is focused.
	UFUNCTION(BlueprintCallable, Category = "Keycard")
	bool Interact();

private:
	void SetFocus(AKcInteractable* NewFocus);

	TWeakObjectPtr<AKcInteractable> Focused;
};
