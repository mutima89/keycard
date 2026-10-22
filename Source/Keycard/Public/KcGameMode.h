// Spawns the Hotel graybox, the first-person character and the HUD; starts the self-test if requested.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "KcGameMode.generated.h"

class AKcHotel;

UCLASS()
class KEYCARD_API AKcGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AKcGameMode();

	virtual AActor* FindPlayerStart_Implementation(AController* Player, const FString& IncomingName) override;
	virtual void BeginPlay() override;

	AKcHotel* EnsureHotel();

private:
	UPROPERTY(Transient)
	TObjectPtr<AKcHotel> Hotel;

	UPROPERTY(Transient)
	TObjectPtr<AActor> StartSpot;
};
