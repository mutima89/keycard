// The Veldane Grand Harbourfront, built from code as one multi-floor building: a stair core and two
// lifts link every floor. Phase 1 fits out B1 (LP office, CCTV room, back of house), the ground
// floor (lobby, front desk, lobby bar, bell stand, staff corridor) and floor 14 (guest corridor,
// rooms 1401-1420 with 1406 and 1412 walkable, the service closet). Floors 1-13 exist as stair
// landings with locked re-entry doors (hotel stair security).
// Every place in the building is a KcMission location; casework props are its spots (KnownSpots).
// People stand where the active case needs them. Falls back to plain shapes without imported art.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KcCast.h"
#include "KcHotel.generated.h"

class AKcCasePoint;
class AKcSign;
class USceneComponent;
enum class EKcUseStyle : uint8;
class AKcInteractable;
class AKcPersonActor;
class UInstancedStaticMeshComponent;
class ULightComponent;
class UMaterialInterface;
class UStaticMesh;
class UStaticMeshComponent;
class UPrimitiveComponent;
class UTextRenderComponent;
class UKcPersonComponent;

UCLASS()
class KEYCARD_API AKcHotel : public AActor
{
	GENERATED_BODY()

public:
	AKcHotel();

	// Builds every floor and spawns the fixed interactables. Safe to call once per world.
	void Build();

	// ---- Building facts ----
	static constexpr float FloorHeight = 400.0f;  // slab to slab
	static constexpr int32 LowestFloor = -1;      // B1
	static constexpr int32 TopFloor = 14;
	static float FloorZ(int32 Floor) { return Floor * FloorHeight; }
	static int32 FloorAt(float Z) { return FMath::FloorToInt((Z + 60.0f) / FloorHeight); }
	// Floors with work tonight (lift buttons that light): B1, G, 14.
	static bool IsFittedFloor(int32 Floor) { return Floor == -1 || Floor == 0 || Floor == 14; }
	static FString FloorLabel(int32 Floor); // "B1", "G", "14"

	// The location (KnownLocations id) at a world point, "" outside every zone.
	FString LocationAt(const FVector& WorldPoint) const;
	// Where the player starts a shift: the LP office, by the terminal.
	FTransform GetPlayerStart() const;
	// Where a case reaches the player (the opening location's arrival point).
	FTransform GetArrival(const FString& Location) const;
	// Rides the lift: same spot in the car on the other floor.
	FVector LiftDestination(const FVector& From, int32 ToFloor) const;
	// Camera positions for the screenshot and performance tour (one per place).
	void GetTourViewpoints(TArray<FString>& OutNames, TArray<FTransform>& OutViews) const;

	// Lights on the player's floor (and the stair) only: cheaper, and no light through the slabs.
	void SetActiveFloor(int32 Floor);

	struct FZone
	{
		FString Location;
		int32 Floor = 0;
		FBox2D Bounds;
	};
	static const TArray<FZone>& Zones();

	virtual void Tick(float DeltaSeconds) override;

private:
	// ---- primitives ----
	UMaterialInterface* Mat(const TCHAR* Name);
	UStaticMesh* Model(const TCHAR* Id);
	UStaticMeshComponent* AddMesh(UStaticMesh* Mesh, const FVector& Location, float Yaw, const FVector& Scale, UMaterialInterface* Override = nullptr, bool bCollision = true);
	bool AddInstance(UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Override = nullptr, bool bCollision = true, bool bHide = false);
	bool AddInstance(UStaticMesh* Mesh, const FVector& Location, float Yaw, const FVector& Scale, UMaterialInterface* Override = nullptr, bool bCollision = true);
	void AddBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, float Yaw = 0.0f, bool bCollision = true);
	void AddCollider(const FVector& Center, const FVector& Size, const FRotator& Rotation);
	UTextRenderComponent* AddLabel(const FString& Text, const FVector& Location, float Yaw, float Size, const FColor& Color, int32 Floor);
	void AddLight(int32 Floor, const FVector& Location, float Lumens, float Radius, const FLinearColor& Color, bool bShadows = false, bool bRect = false,
		const FVector2D& RectSize = FVector2D::ZeroVector);
	void CeilingLight(int32 Floor, const FVector& Location, float Lumens, const FLinearColor& Color, float Radius = 700.0f);
	void Sconce(int32 Floor, const FVector& WallPoint, float Yaw);

	// ---- architecture ----
	void BuildSlabs();
	void BuildCore();
	void BuildStairs();
	void BuildFacade(int32 Floor, float Height);
	// A wall along X at Y from X0 to X1 with door openings (centre, width).
	void WallX(int32 Floor, float Y, float X0, float X1, const TArray<FVector2D>& Doors, UMaterialInterface* Finish, float Height);
	void WallY(int32 Floor, float X, float Y0, float Y1, const TArray<FVector2D>& Doors, UMaterialInterface* Finish, float Height);
	void Ceiling(int32 Floor, const FBox2D& Area, float Height, UMaterialInterface* Material);
	void FloorFinish(int32 Floor, const FBox2D& Area, UMaterialInterface* Material, float Lift = 0.5f);
	// A door in an opening: frame, leaf, lever, and on guest rooms a number plate, peephole and keycard
	// reader. Center is the opening's centre on the wall line at floor level; Yaw points to the side the
	// plate faces (the corridor). bOpen swings the leaf into the room. A non-empty SignTitle makes the
	// closed leaf an AKcSign that says why it stays shut. The hinge is on the left seen from the plate side;
	// bMirror puts it on the right.
	void DoorLeaf(int32 Floor, const FVector& Center, float Yaw, float Width, const FString& Plate, bool bGuestRoom, bool bOpen,
		const FString& SignTitle = FString(), const FString& SignMessage = FString(), const FString& SignLocation = FString(), bool bMirror = false);
	// A door the casework opens and closes (1412 while the guest waits, 1406 once Mr Pike opens up, the
	// floor closet while the attendant is in it): leaf, lever and reader on a hinge pivot.
	void DynamicDoor(const FString& Id, int32 Floor, const FVector& Center, float Yaw, float Width, const FString& Plate, bool bMirror = false);
	void SetDoorOpen(const FString& Id, bool bOpen);
	struct FDynDoor
	{
		TWeakObjectPtr<USceneComponent> Pivot;
		float ClosedYaw = 0.0f;
		float OpenYaw = 0.0f;
		bool bOpen = false;
	};
	TMap<FString, FDynDoor> Doors;
	AKcSign* SpawnSign(const FString& Location, const FString& Title, const FString& Message, const FVector& At, float Yaw, const FVector& Size, UMaterialInterface* Material);
	void ExitSign(int32 Floor, const FVector& At, float Yaw);
	void SmokeDetector(int32 Floor, const FVector2D& At, float CeilingZ, bool bCovered = false);
	void CctvDome(int32 Floor, const FVector& At, const FString& CameraId);
	void FireExtinguisher(int32 Floor, const FVector& WallPoint, float Yaw);

	// ---- floors ----
	void BuildBasement();
	void BuildGround();
	void BuildGuestFloor();
	void GuestRoom(const FString& Number, const FBox2D& Area, bool bNorth, bool bWalkable);
	// Fits out a walkable room: bathroom, wardrobe and safe, bed, desk, window and balcony door.
	void FurnishRoom(const FString& Number, const FBox2D& Area, bool bNorth, int32 Seed);
	void BuildOutside();
	void BuildFireCommand();
	void BuildLpOffice();
	void BuildCctvRoom();
	void BuildFrontDesk();
	void BuildLobbyBar();
	void BuildServiceCloset();

	// ---- furniture ----
	void Desk(const FVector& Center, float Yaw, const FVector& Size, UMaterialInterface* Top);
	void Monitor(const FVector& Base, float Yaw, UMaterialInterface* Screen);
	void Chair(const FVector& Location, float Yaw, const TCHAR* ModelId = TEXT("modern_arm_chair_01"));
	void TaskChair(const FVector& Location, float Yaw);
	void Sofa(const FVector& Location, float Yaw);
	void Plant(const FVector& Location, bool bLarge);
	void Bed(const FVector& HeadCenter, float Yaw);
	void Lamp(int32 Floor, const FVector& Base, bool bFloorLamp);
	void Shelf(const FVector& Center, float Yaw, float Width, int32 Seed);
	void ArtPrint(const FVector& WallPoint, float Yaw, const TCHAR* Print, const FVector2D& Size);

	// ---- casework ----
	void PlaceFixedInteractables();
	AKcCasePoint* SpawnCasePoint(const FString& Location, const FString& Spot, const FString& Title, const FVector& At, float Yaw, UStaticMesh* Mesh,
		const FVector& Scale, UMaterialInterface* Material, EKcUseStyle Style, const FVector& Focus = FVector::ZeroVector);
	UPROPERTY(Transient) TArray<TObjectPtr<AKcCasePoint>> CasePoints;
	// People where the active case needs them; staff at their posts otherwise.
	void RefreshPeople();
	FString PeopleKey;
	struct FPersonSpot { FVector At; float Yaw; };
	FPersonSpot SpotFor(const FString& Location, const FString& PersonId, int32 Index) const;
	UPROPERTY(Transient) TArray<TObjectPtr<AKcPersonActor>> People;
	// The party in 1406 (M003): guests, music and the covered detector, while that case is open.
	void RefreshScenes();
	FString SceneKey;
	UPROPERTY(Transient) TArray<TObjectPtr<UKcPersonComponent>> Extras;
	UPROPERTY(Transient) TArray<TObjectPtr<UPrimitiveComponent>> SceneProps;
	UPROPERTY(Transient) TObjectPtr<class UAudioComponent> PartyMusic;
	UKcPersonComponent* AddExtra(const KcCast::FLook& Look, const FVector& At, float FacingYaw, int32 Seed);
	// A scene prop (removed when the scene changes).
	void SceneBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, float Yaw = 0.0f, UStaticMesh* Mesh = nullptr);
	// Rooms 1406 and 1412: the bedroom's smoke detector (the party covers 1406's).
	FVector Detector1406 = FVector::ZeroVector;
	FVector Desk1412 = FVector::ZeroVector; // desk top, under the lamp
	FVector Safe1412 = FVector::ZeroVector;

	// ---- night and life ----
	void BuildLighting();
	void BuildAmbience();
	UPROPERTY(Transient) TObjectPtr<class UMaterialInstanceDynamic> SkyMaterial;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> LobbyClock;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> CctvClock;
	float RefreshTimer = 0.0f;
	int32 ActiveFloor = INT32_MIN;

	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cube;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Cylinder;
	UPROPERTY(Transient) TObjectPtr<UStaticMesh> Sphere;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UMaterialInterface>> Materials;
	UPROPERTY(Transient) TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Pools;
	UPROPERTY(Transient) TArray<TObjectPtr<ULightComponent>> FloorLights;
	TArray<int32> FloorLightFloor; // floor of each entry in FloorLights (stair lights: INT32_MAX, always on)
	UPROPERTY(Transient) TArray<TObjectPtr<UTextRenderComponent>> Labels;
	int32 ComponentCount = 0;
	int32 InstanceCount = 0;
	int32 LightCount = 0;
	bool bBuilt = false;
	bool bHasArt = false;
};
