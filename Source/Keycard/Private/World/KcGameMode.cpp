#include "KcGameMode.h"

#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "KcCharacter.h"
#include "KcHUD.h"
#include "KcHotel.h"
#include "KcSelfTest.h"
#include "TimerManager.h"

AKcGameMode::AKcGameMode()
{
	DefaultPawnClass = AKcCharacter::StaticClass();
	HUDClass = AKcHUD::StaticClass();
}

AKcHotel* AKcGameMode::EnsureHotel()
{
	if (Hotel) return Hotel;
	for (TActorIterator<AKcHotel> It(GetWorld()); It; ++It)
	{
		Hotel = *It; // placed in the map by hand (L_Harbourfront has none)
	}
	if (!Hotel)
	{
		Hotel = GetWorld()->SpawnActor<AKcHotel>();
	}
	Hotel->Build();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	StartSpot = GetWorld()->SpawnActor<ATargetPoint>(ATargetPoint::StaticClass(), Hotel->GetPlayerStart(), Params);
	return Hotel;
}

AActor* AKcGameMode::FindPlayerStart_Implementation(AController* Player, const FString& IncomingName)
{
	EnsureHotel();
	return StartSpot ? StartSpot.Get() : Super::FindPlayerStart_Implementation(Player, IncomingName);
}

void AKcGameMode::BeginPlay()
{
	Super::BeginPlay();
	EnsureHotel();
	if (KcSelfTest::IsRequested())
	{
		FTimerHandle Handle;
		TWeakObjectPtr<UWorld> World(GetWorld());
		GetWorldTimerManager().SetTimer(Handle, [World]()
		{
			if (World.IsValid()) KcSelfTest::Run(World.Get());
		}, 1.0f, false);
	}
}
