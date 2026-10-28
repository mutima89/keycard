#include "KcHotel.h"

#include "Components/AudioComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/RectLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureCube.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "KcAudioSubsystem.h"
#include "KcCampaignSubsystem.h"
#include "KcConvert.h"
#include "KcCast.h"
#include "KcGameSubsystem.h"
#include "KcInteractable.h"
#include "KcLog.h"
#include "KcPerson.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// Plan (cm, X east, Y north, Z up; floor F's slab top at F * 400):
//   footprint X 0..4800, Y -1000..1000; the harbour is north, the street west.
//   core X 2100..2700 on every floor: lifts on the north side of the core corridor (Y -160..160),
//   Stair A (dog-leg, B1 to 14) on its south side, door at X 2185.
//   B1: back-of-house corridor; LP office and CCTV room north of it, east of the core.
//   G:  lobby west of the core (front desk north, lobby bar south, street doors west),
//       staff corridor east (fire command centre, kitchen, staff entrance).
//   14: guest corridor; rooms 1401-1410 north, 1411-1420 south, floor closet at the west end.
// Docs/WorldData.md has the same plan with the casework spots.

namespace
{
constexpr float MaxX = 4800.0f;
constexpr float HalfY = 1000.0f;
constexpr float CorrY = 160.0f;          // corridor half-width (all fitted floors)
constexpr float Wall = 16.0f;            // partition thickness
constexpr float DoorH = 215.0f;
constexpr float GuestDoorW = 95.0f;
constexpr float DeskH = 74.0f;
constexpr float CoreX0 = 2100.0f;
constexpr float CoreX1 = 2700.0f;
constexpr float LiftDoorX[2] = {2250.0f, 2550.0f};
constexpr float LiftPanelX = 2400.0f;
constexpr float StairDoorX = 2185.0f;
constexpr float DockDoorX = 4250.0f;     // B1 dock door in the south wall
constexpr float LuggageY0 = 760.0f;      // luggage room behind the bell stand: Y 760..1000, X 0..480
constexpr float LuggageX1 = 480.0f;
constexpr float LuggageDoorX = 240.0f;
constexpr float StairY0 = -460.0f;       // stair inner faces
constexpr float StairY1 = -168.0f;
constexpr float StairSpineY = -310.0f;
constexpr float StairLandX1 = 2250.0f;   // floor landing X 2120..2250
constexpr float StairMidX0 = 2558.0f;    // mid landing X 2558..2680
constexpr float StairIn0 = 2120.0f;
constexpr float StairIn1 = 2680.0f;
constexpr float CeilB1 = 280.0f;
constexpr float CeilLobby = 380.0f;
constexpr float CeilBack = 280.0f;
constexpr float Ceil14 = 270.0f;
constexpr float CeilBallroom = 360.0f;
constexpr float CeilExec = 270.0f;
// Floors that are built inside (B1, G, the ballroom floor, the executive floor, 14). Keep in step with
// AKcHotel::IsFittedFloor.
constexpr int32 FittedFloors[] = {-1, 0, 1, 4, 14};
constexpr float RoomDepth = 840.0f;      // corridor wall to facade
// Poly Haven models face +Y in UE after import; this turns "facing yaw" into mesh yaw.
constexpr float ModelYawOffset = -90.0f;
constexpr int32 StairLightBase = 1000;   // stair lights are stored as 1000 + floor

// Night: exposure held in a band so lamps and dark windows do not pump.
constexpr float ExposureMinEV100 = 3.6f;
constexpr float ExposureMaxEV100 = 5.4f;
constexpr float DownLumens = 850.0f;

const FLinearColor Warm(1.0f, 0.78f, 0.55f);
const FLinearColor Neutral(1.0f, 0.93f, 0.84f);
const FLinearColor Cool(0.86f, 0.92f, 1.0f);
const FColor Ink(28, 30, 34);
const FColor Cream(238, 232, 220);
const FColor BrassText(214, 178, 112);

float Zf(int32 Floor, float Height) { return AKcHotel::FloorZ(Floor) + Height; }

// Coordinates inside a guest room: U along the corridor from the room's west wall, V from the
// corridor wall towards the facade. North rooms run +Y, south rooms -Y (mirrored).
struct FRoomFrame
{
	float X0 = 0.0f;
	float W = 0.0f;
	bool bNorth = true;
	float Zb = 0.0f;
	FVector P(float U, float V, float Z) const { return FVector(X0 + U, bNorth ? CorrY + V : -CorrY - V, Zb + Z); }
	// Local yaw: 0 along +U, 90 deeper into the room (+V).
	float Yaw(float LocalYaw) const { return bNorth ? LocalYaw : -LocalYaw; }
	FVector2D P2(float U, float V) const { return FVector2D(X0 + U, bNorth ? CorrY + V : -CorrY - V); }
};

// The casework people's places (Z is the floor; the capsule is lifted by 90).
struct FPost
{
	const TCHAR* PersonId;
	const TCHAR* Location;
	int32 Floor;
	FVector2D At;
	float Yaw;
};
const FPost Posts[] = {
	{TEXT("night_auditor"), TEXT("front_desk"), 0, FVector2D(860.0f, 800.0f), -90.0f},
	{TEXT("desk_agent"), TEXT("front_desk"), 0, FVector2D(1240.0f, 800.0f), -90.0f},
	{TEXT("night_manager"), TEXT("front_desk"), 0, FVector2D(1760.0f, 600.0f), -150.0f},
	{TEXT("visitor_haskett"), TEXT("lobby"), 0, FVector2D(1290.0f, 260.0f), 120.0f},
	{TEXT("server_kerr"), TEXT("lobby_bar"), 0, FVector2D(1915.0f, -740.0f), 180.0f},
	{TEXT("guest_whitlock"), TEXT("room_1412"), 14, FVector2D(1000.0f, -560.0f), 90.0f},
	{TEXT("attendant_duarte"), TEXT("f14_service"), 14, FVector2D(205.0f, -470.0f), 110.0f},
	{TEXT("guest_morrow"), TEXT("f14_corridor"), 14, FVector2D(3195.0f, 95.0f), -130.0f},
	{TEXT("guest_achterberg"), TEXT("f14_corridor"), 14, FVector2D(3825.0f, -95.0f), 60.0f},
	{TEXT("guest_pike"), TEXT("guest_room"), 14, FVector2D(2775.0f, 205.0f), -90.0f},
	{TEXT("partner"), TEXT("cctv_room"), -1, FVector2D(4520.0f, 560.0f), 160.0f},
	{TEXT("bell_captain"), TEXT("bell_stand"), 0, FVector2D(270.0f, 560.0f), -45.0f},
	{TEXT("guest_lindgren"), TEXT("bell_stand"), 0, FVector2D(440.0f, 420.0f), 140.0f},
	{TEXT("guest_halasz"), TEXT("lobby"), 0, FVector2D(760.0f, -170.0f), 25.0f},
	{TEXT("paramedic_sethna"), TEXT("lobby"), 0, FVector2D(420.0f, -60.0f), 0.0f},
	{TEXT("guest_nyholt"), TEXT("lobby_bar"), 0, FVector2D(1470.0f, -820.0f), 10.0f},
	{TEXT("receiving_clerk"), TEXT("loading_dock"), -1, FVector2D(3220.0f, -560.0f), -150.0f},
	{TEXT("partner"), TEXT("f14_corridor"), 14, FVector2D(2470.0f, -70.0f), 160.0f},
	{TEXT("guest_ashcombe"), TEXT("lobby"), 0, FVector2D(540.0f, -120.0f), 160.0f},
	{TEXT("contractor_fenner"), TEXT("kitchen"), 0, FVector2D(3620.0f, -720.0f), -100.0f},
	{TEXT("banquet_manager"), TEXT("ballroom"), 1, FVector2D(4060.0f, -830.0f), 0.0f},
	{TEXT("hr_director"), TEXT("exec_offices"), 4, FVector2D(1150.0f, 760.0f), -150.0f},
	{TEXT("finance_director"), TEXT("exec_offices"), 4, FVector2D(350.0f, 880.0f), -90.0f},
	{TEXT("gm"), TEXT("gm_office"), 4, FVector2D(400.0f, -860.0f), 90.0f},
};

// A guest behind a closed door: their face-to-face work is offered at the door's spot until the
// action that opens it is done.
struct FBehindDoor
{
	const TCHAR* Spot;
	const TCHAR* PersonId;
	const TCHAR* PersonLocation;
	const TCHAR* OpensWith;
};
const FBehindDoor BehindDoors[] = {
	{TEXT("door_1406"), TEXT("guest_pike"), TEXT("guest_room"), TEXT("knock_1406")},
};

// Guest room bands on 14: first number, west edge, width, count, north side.
struct FRoomBand { int32 First; float X0; float W; int32 Count; bool bNorth; };
const FRoomBand Bands[] = {
	{1401, 0.0f, 420.0f, 5, true},
	{1406, 2700.0f, 420.0f, 5, true},
	{1411, 300.0f, 450.0f, 4, false},
	{1415, 2700.0f, 350.0f, 6, false},
};
// A guest room's door: 75 cm in from its west wall.
float RoomDoorX(float X0) { return X0 + 75.0f; }
} // namespace

// ---------------------------------------------------------------------------- facts

const TArray<AKcHotel::FZone>& AKcHotel::Zones()
{
	// First match wins: the specific places come before the areas around them.
	static const TArray<FZone> Z = {
		{TEXT("lp_office"), -1, FBox2D(FVector2D(2700, 160), FVector2D(3700, 1000))},
		{TEXT("cctv_room"), -1, FBox2D(FVector2D(3700, 160), FVector2D(4800, 1000))},
		{TEXT("housekeeping"), -1, FBox2D(FVector2D(0, -1000), FVector2D(2100, -160))},
		{TEXT("loading_dock"), -1, FBox2D(FVector2D(2700, -1000), FVector2D(4800, -160))},
		{TEXT("back_of_house"), -1, FBox2D(FVector2D(0, -460), FVector2D(4800, 160))},
		{TEXT("front_desk"), 0, FBox2D(FVector2D(500, 450), FVector2D(1800, 1000))},
		{TEXT("bell_stand"), 0, FBox2D(FVector2D(0, 300), FVector2D(500, 1000))},
		{TEXT("lobby_bar"), 0, FBox2D(FVector2D(0, -1000), FVector2D(2100, -450))},
		{TEXT("fire_command"), 0, FBox2D(FVector2D(2700, 160), FVector2D(3500, 1000))},
		{TEXT("kitchen"), 0, FBox2D(FVector2D(2700, -1000), FVector2D(4800, -160))},
		{TEXT("lobby"), 0, FBox2D(FVector2D(0, -1000), FVector2D(2700, 1000))},
		{TEXT("ballroom"), 1, FBox2D(FVector2D(0, -1000), FVector2D(4800, 1000))},
		{TEXT("stair_a"), 2, FBox2D(FVector2D(StairIn0, StairY0), FVector2D(StairIn1, StairY1))},
		{TEXT("gm_office"), 4, FBox2D(FVector2D(0, -1000), FVector2D(800, -160))},
		{TEXT("boardroom"), 4, FBox2D(FVector2D(800, -1000), FVector2D(2100, -160))},
		{TEXT("exec_offices"), 4, FBox2D(FVector2D(0, -1000), FVector2D(4800, 1000))},
		{TEXT("room_1412"), 14, FBox2D(FVector2D(750, -1000), FVector2D(1200, -160))},
		{TEXT("guest_room"), 14, FBox2D(FVector2D(2700, 160), FVector2D(3120, 1000))},
		{TEXT("f14_service"), 14, FBox2D(FVector2D(0, -1000), FVector2D(300, -160))},
		{TEXT("f14_corridor"), 14, FBox2D(FVector2D(0, -460), FVector2D(4800, 160))},
	};
	return Z;
}

FString AKcHotel::FloorLabel(int32 Floor)
{
	if (Floor < 0) return FString::Printf(TEXT("B%d"), -Floor);
	if (Floor == 0) return TEXT("G");
	return FString::FromInt(Floor);
}

TArray<FString> AKcHotel::MissingWorkPoints(const kc::MissionCatalogue& Catalogue) const
{
	TArray<FString> Out;
	auto HasPoint = [this](const FString& Location, const FString& Spot)
	{
		for (const AKcCasePoint* P : CasePoints)
		{
			if (P && P->Location == Location && P->Spot == Spot) return true;
		}
		return false;
	};
	auto HasZone = [](const FString& Location)
	{
		for (const FZone& Zn : Zones()) if (Zn.Location == Location) return true;
		return false;
	};
	// A step with a person and no spot is done by talking to them: RefreshScenes stands them at their post
	// or by the place's arrival point (SpotFor). A place with no arrival would put them in the LP office.
	auto CanStand = [this](const FString& Location, const FString& PersonId)
	{
		for (const FPost& P : Posts) if (P.PersonId == PersonId && P.Location == Location) return true;
		return Location == TEXT("lp_office") || !GetArrival(Location).Equals(GetPlayerStart());
	};
	for (const kc::MissionDef& M : Catalogue.Missions)
	{
		for (const kc::ActionDef& A : M.Actions)
		{
			const FString Where = KcConvert::ToFString(A.At), Spot = KcConvert::ToFString(A.Spot), Person = KcConvert::ToFString(A.Person);
			const FString Name = KcConvert::ToFString(M.Id) + TEXT(" ") + KcConvert::ToFString(A.Id);
			if (!HasZone(Where)) Out.Add(Name + TEXT(": no zone for ") + Where);
			else if (!Person.IsEmpty() && Spot.IsEmpty())
			{
				if (!CanStand(Where, Person)) Out.Add(Name + TEXT(": nowhere for ") + Person + TEXT(" to stand at ") + Where);
			}
			else if (!HasPoint(Where, Spot)) Out.Add(Name + TEXT(": no case point ") + (Spot.IsEmpty() ? Where : Where + TEXT("/") + Spot));
		}
	}
	return Out;
}

FString AKcHotel::LocationAt(const FVector& WorldPoint) const
{
	const int32 Floor = FloorAt(WorldPoint.Z);
	const FVector2D P(WorldPoint.X, WorldPoint.Y);
	for (const FZone& Zn : Zones())
	{
		if (Zn.Floor == Floor && Zn.Bounds.IsInside(P)) return Zn.Location;
	}
	return FString();
}

AKcHotel::AKcHotel()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.0f;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

FTransform AKcHotel::GetPlayerStart() const
{
	// The LP office, between the desks and the door, facing your desk and the report terminal.
	return FTransform(FRotator(0.0f, 70.0f, 0.0f), FVector(2930.0f, 560.0f, Zf(-1, 100.0f)));
}

FTransform AKcHotel::GetArrival(const FString& Location) const
{
	struct FArrive { const TCHAR* Id; int32 Floor; FVector2D At; float Yaw; };
	static const FArrive A[] = {
		{TEXT("lp_office"), -1, FVector2D(2930.0f, 560.0f), 70.0f},
		{TEXT("cctv_room"), -1, FVector2D(4250.0f, 330.0f), 90.0f},
		{TEXT("back_of_house"), -1, FVector2D(2400.0f, -40.0f), 0.0f},
		{TEXT("housekeeping"), -1, FVector2D(1000.0f, -260.0f), -90.0f},
		{TEXT("loading_dock"), -1, FVector2D(3500.0f, -260.0f), -90.0f},
		{TEXT("kitchen"), 0, FVector2D(3300.0f, -280.0f), -90.0f},
		{TEXT("front_desk"), 0, FVector2D(1100.0f, 470.0f), 90.0f},
		{TEXT("lobby"), 0, FVector2D(2350.0f, -40.0f), 180.0f},
		{TEXT("lobby_bar"), 0, FVector2D(1050.0f, -480.0f), -90.0f},
		{TEXT("bell_stand"), 0, FVector2D(420.0f, 380.0f), 135.0f},
		{TEXT("fire_command"), 0, FVector2D(3100.0f, 300.0f), 90.0f},
		{TEXT("ballroom"), 1, FVector2D(3200.0f, 0.0f), 0.0f},
		{TEXT("stair_a"), 2, FVector2D(2190.0f, -330.0f), 90.0f},
		{TEXT("exec_offices"), 4, FVector2D(1000.0f, 245.0f), 90.0f},
		{TEXT("gm_office"), 4, FVector2D(400.0f, -300.0f), -90.0f},
		{TEXT("boardroom"), 4, FVector2D(1450.0f, -260.0f), -90.0f},
		{TEXT("f14_corridor"), 14, FVector2D(2400.0f, -40.0f), 180.0f},
		{TEXT("room_1412"), 14, FVector2D(825.0f, -120.0f), -90.0f},
		{TEXT("guest_room"), 14, FVector2D(2775.0f, 60.0f), 90.0f},
		{TEXT("f14_service"), 14, FVector2D(150.0f, -110.0f), -90.0f},
	};
	for (const FArrive& X : A)
	{
		if (Location == X.Id) return FTransform(FRotator(0.0f, X.Yaw, 0.0f), FVector(X.At.X, X.At.Y, Zf(X.Floor, 100.0f)));
	}
	return GetPlayerStart();
}

FVector AKcHotel::LiftDestination(const FVector& From, int32 ToFloor) const
{
	// Off the car on the other floor, in the same spot of the lift lobby.
	const int32 Here = FloorAt(From.Z);
	return FVector(From.X, From.Y, From.Z - FloorZ(Here) + FloorZ(FMath::Clamp(ToFloor, LowestFloor, TopFloor)));
}

void AKcHotel::GetTourViewpoints(TArray<FString>& OutNames, TArray<FTransform>& OutViews) const
{
	const float Eye = 165.0f;
	auto Add = [&](const FString& Name, int32 Floor, const FVector2D& From, const FVector& To)
	{
		const FVector F(From.X, From.Y, Zf(Floor, Eye));
		const FVector T(To.X, To.Y, Zf(Floor, To.Z));
		OutNames.Add(Name);
		OutViews.Add(FTransform((T - F).Rotation(), F));
	};
	Add(TEXT("LP office"), -1, FVector2D(3600.0f, 260.0f), FVector(2900.0f, 900.0f, 90.0f));
	Add(TEXT("LP office (evidence locker)"), -1, FVector2D(3000.0f, 300.0f), FVector(3690.0f, 600.0f, 110.0f));
	Add(TEXT("CCTV control room"), -1, FVector2D(4250.0f, 260.0f), FVector(4250.0f, 1000.0f, 210.0f));
	Add(TEXT("Back-of-house corridor"), -1, FVector2D(4700.0f, 0.0f), FVector(0.0f, 0.0f, 140.0f));
	Add(TEXT("Stair A"), -1, FVector2D(2185.0f, -220.0f), FVector(2600.0f, -240.0f, 260.0f));
	Add(TEXT("Housekeeping and laundry"), -1, FVector2D(1000.0f, -230.0f), FVector(400.0f, -900.0f, 90.0f));
	Add(TEXT("Housekeeping key cabinet"), -1, FVector2D(1250.0f, -420.0f), FVector(1250.0f, -170.0f, 150.0f));
	Add(TEXT("Loading dock"), -1, FVector2D(3500.0f, -230.0f), FVector(4300.0f, -980.0f, 110.0f));
	Add(TEXT("Receiving office"), -1, FVector2D(3300.0f, -600.0f), FVector(2800.0f, -950.0f, 100.0f));
	Add(TEXT("Lobby from the street doors"), 0, FVector2D(380.0f, -60.0f), FVector(1600.0f, 500.0f, 130.0f));
	Add(TEXT("Front desk"), 0, FVector2D(1100.0f, 380.0f), FVector(1100.0f, 900.0f, 150.0f));
	Add(TEXT("Bell stand and luggage room"), 0, FVector2D(560.0f, 380.0f), FVector(240.0f, 800.0f, 110.0f));
	Add(TEXT("Lobby bar"), 0, FVector2D(1050.0f, -470.0f), FVector(900.0f, -980.0f, 140.0f));
	Add(TEXT("Lift lobby, ground floor"), 0, FVector2D(2400.0f, -130.0f), FVector(2400.0f, 160.0f, 160.0f));
	Add(TEXT("Fire command centre"), 0, FVector2D(3100.0f, 260.0f), FVector(3100.0f, 1000.0f, 150.0f));
	Add(TEXT("Main kitchen"), 0, FVector2D(2850.0f, -260.0f), FVector(4000.0f, -900.0f, 150.0f));
	Add(TEXT("Main kitchen (cook line and canopy)"), 0, FVector2D(3600.0f, -500.0f), FVector(3700.0f, -950.0f, 200.0f));
	Add(TEXT("Floor 14 corridor"), 14, FVector2D(2420.0f, 0.0f), FVector(0.0f, 0.0f, 150.0f));
	Add(TEXT("Floor 14 corridor (east)"), 14, FVector2D(2600.0f, 60.0f), FVector(4800.0f, 0.0f, 150.0f));
	Add(TEXT("Room 1412"), 14, FVector2D(830.0f, -330.0f), FVector(1100.0f, -900.0f, 80.0f));
	Add(TEXT("Room 1412 (desk and safe)"), 14, FVector2D(1150.0f, -800.0f), FVector(790.0f, -480.0f, 90.0f));
	Add(TEXT("Room 1406 from the door"), 14, FVector2D(2775.0f, 40.0f), FVector(2850.0f, 800.0f, 120.0f));
	Add(TEXT("Floor 14 closet"), 14, FVector2D(150.0f, -190.0f), FVector(150.0f, -900.0f, 80.0f));
	Add(TEXT("Floor 1 foyer"), 1, FVector2D(300.0f, 0.0f), FVector(2400.0f, 0.0f, 150.0f));
	Add(TEXT("Harbour Ballroom"), 1, FVector2D(2800.0f, 60.0f), FVector(4700.0f, -100.0f, 120.0f));
	Add(TEXT("Banquet service corridor"), 1, FVector2D(2760.0f, -870.0f), FVector(3900.0f, -870.0f, 140.0f));
	Add(TEXT("Banquet office and bar store"), 1, FVector2D(3960.0f, -820.0f), FVector(4700.0f, -930.0f, 110.0f));
	Add(TEXT("Executive offices"), 4, FVector2D(1750.0f, 200.0f), FVector(1500.0f, 900.0f, 110.0f));
	Add(TEXT("HR interview room"), 4, FVector2D(1000.0f, 380.0f), FVector(1000.0f, 850.0f, 90.0f));
	Add(TEXT("Finance office"), 4, FVector2D(350.0f, 390.0f), FVector(350.0f, 900.0f, 100.0f));
	Add(TEXT("General Manager's office"), 4, FVector2D(400.0f, -230.0f), FVector(400.0f, -900.0f, 100.0f));
	Add(TEXT("Boardroom"), 4, FVector2D(850.0f, -250.0f), FVector(2000.0f, -700.0f, 100.0f));
	Add(TEXT("Stair A, floor 14 (sprinkler valve)"), 14, FVector2D(2220.0f, -200.0f), FVector(2134.0f, -400.0f, 120.0f));
	Add(TEXT("Stair A, floor 2"), 2, FVector2D(2185.0f, -220.0f), FVector(2120.0f, -320.0f, 190.0f));
	Add(TEXT("Stair A, floor 7"), 7, FVector2D(2185.0f, -220.0f), FVector(2120.0f, -320.0f, 190.0f));
}

// ---------------------------------------------------------------------------- primitives

UMaterialInterface* AKcHotel::Mat(const TCHAR* Name)
{
	if (TObjectPtr<UMaterialInterface>* Found = Materials.Find(Name)) return *Found;
	const FString Path = FString::Printf(TEXT("/Game/Keycard/Materials/Instances/%s.%s"), Name, Name);
	UMaterialInterface* M = LoadObject<UMaterialInterface>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
	Materials.Add(Name, M);
	return M;
}

UStaticMesh* AKcHotel::Model(const TCHAR* Id)
{
	const FString Path = FString::Printf(TEXT("/Game/Keycard/Models/%s/SM_%s.SM_%s"), Id, Id, Id);
	return LoadObject<UStaticMesh>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

UStaticMeshComponent* AKcHotel::AddMesh(UStaticMesh* Mesh, const FVector& Location, float Yaw, const FVector& Scale, UMaterialInterface* Override, bool bCollision)
{
	if (!Mesh) return nullptr;
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None);
	C->SetMobility(EComponentMobility::Movable);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(Location);
	C->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	C->SetRelativeScale3D(Scale);
	if (!bCollision) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	if (Override)
	{
		for (int32 i = 0; i < C->GetNumMaterials(); ++i) C->SetMaterial(i, Override);
	}
	C->RegisterComponent();
	++ComponentCount;
	return C;
}

bool AKcHotel::AddInstance(UStaticMesh* Mesh, const FTransform& T, UMaterialInterface* Override, bool bCollision, bool bHide)
{
	// One instanced component per (mesh, material, collision, visibility): a few hundred draws for the building.
	if (!Mesh) return false;
	const FString Key = Mesh->GetPathName() + TEXT("|") + (Override ? Override->GetPathName() : FString()) + (bCollision ? TEXT("|c") : TEXT("|n")) +
		(bHide ? TEXT("|h") : TEXT(""));
	TObjectPtr<UInstancedStaticMeshComponent>& Pool = Pools.FindOrAdd(Key);
	if (!Pool)
	{
		Pool = NewObject<UInstancedStaticMeshComponent>(this, NAME_None);
		Pool->SetMobility(EComponentMobility::Movable);
		Pool->SetStaticMesh(Mesh);
		Pool->SetupAttachment(RootComponent);
		if (!bCollision) Pool->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		if (bHide)
		{
			Pool->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			Pool->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
			Pool->SetHiddenInGame(true);
			Pool->SetVisibility(false);
			Pool->SetCastShadow(false);
		}
		if (Override)
		{
			for (int32 i = 0; i < Pool->GetNumMaterials(); ++i) Pool->SetMaterial(i, Override);
		}
		Pool->RegisterComponent();
		++ComponentCount;
	}
	Pool->AddInstance(T);
	++InstanceCount;
	return true;
}

bool AKcHotel::AddInstance(UStaticMesh* Mesh, const FVector& Location, float Yaw, const FVector& Scale, UMaterialInterface* Override, bool bCollision)
{
	return AddInstance(Mesh, FTransform(FRotator(0.0f, Yaw, 0.0f), Location, Scale), Override, bCollision, false);
}

void AKcHotel::AddBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, float Yaw, bool bCollision)
{
	AddInstance(Cube, Center, Yaw, Size / 100.0f, Material, bCollision); // the basic cube is 100 cm
}

void AKcHotel::AddCollider(const FVector& Center, const FVector& Size, const FRotator& Rotation)
{
	AddInstance(Cube, FTransform(Rotation, Center, Size / 100.0f), nullptr, true, true);
}

UTextRenderComponent* AKcHotel::AddLabel(const FString& Text, const FVector& Location, float Yaw, float Size, const FColor& Color, int32 Floor)
{
	// The text reads from the side its local +X points to.
	UTextRenderComponent* Label = NewObject<UTextRenderComponent>(this, NAME_None);
	Label->SetupAttachment(RootComponent);
	Label->SetRelativeLocation(Location);
	Label->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	Label->SetText(FText::FromString(Text));
	Label->SetWorldSize(Size);
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetVerticalAlignment(EVRTA_TextCenter);
	Label->SetTextRenderColor(Color);
	Label->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Label->SetCastShadow(false);
	Label->RegisterComponent();
	Labels.Add(Label);
	++ComponentCount;
	(void)Floor;
	return Label;
}

void AKcHotel::AddLight(int32 Floor, const FVector& Location, float Lumens, float Radius, const FLinearColor& Color, bool bShadows, bool bRect, const FVector2D& RectSize)
{
	ULocalLightComponent* L = nullptr;
	if (bRect)
	{
		URectLightComponent* R = NewObject<URectLightComponent>(this, NAME_None);
		R->SetSourceWidth(RectSize.X);
		R->SetSourceHeight(RectSize.Y);
		R->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
		L = R;
	}
	else
	{
		UPointLightComponent* P = NewObject<UPointLightComponent>(this, NAME_None);
		P->SetSourceRadius(4.0f);
		L = P;
	}
	L->SetMobility(EComponentMobility::Movable);
	L->SetupAttachment(RootComponent);
	L->SetRelativeLocation(Location);
	L->SetIntensityUnits(ELightUnits::Lumens);
	L->SetIntensity(Lumens);
	L->SetAttenuationRadius(Radius);
	L->SetLightColor(Color);
	L->SetCastShadows(bShadows);
	L->RegisterComponent();
	FloorLights.Add(L);
	FloorLightFloor.Add(Floor);
	++ComponentCount;
	++LightCount;
}

void AKcHotel::AddDownlight(int32 Floor, const FVector& Location, float Lumens, float Radius, const FLinearColor& Color, float OuterCone)
{
	// The engine spreads a spot's lumens over its cone, a point light's over the whole sphere: scale them so the light
	// straight down matches the point light it replaces, plus a quarter for the bounce the unlit ceiling no longer gives.
	USpotLightComponent* S = NewObject<USpotLightComponent>(this, NAME_None);
	S->SetMobility(EComponentMobility::Movable);
	S->SetupAttachment(RootComponent);
	S->SetRelativeLocation(Location);
	S->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	S->SetOuterConeAngle(OuterCone);
	S->SetInnerConeAngle(OuterCone * 0.65f);
	S->SetSourceRadius(4.0f);
	S->SetIntensityUnits(ELightUnits::Lumens);
	S->SetIntensity(Lumens * 1.25f * (1.0f - FMath::Cos(FMath::DegreesToRadians(OuterCone))) * 0.5f);
	S->SetAttenuationRadius(Radius);
	S->SetLightColor(Color);
	S->SetCastShadows(false);
	S->RegisterComponent();
	FloorLights.Add(S);
	FloorLightFloor.Add(Floor);
	++ComponentCount;
	++LightCount;
}

void AKcHotel::CeilingLight(int32 Floor, const FVector& Location, float Lumens, const FLinearColor& Color, float Radius, float OuterCone)
{
	// Recessed downlight flush with the ceiling at Location.Z: a chrome trim, a dark baffle and a small glowing lens, and a
	// spot aimed down from just under it. (A point light 12 cm down lit its own disc and the ceiling round it white, walk 17.)
	AddInstance(Cylinder, FVector(Location.X, Location.Y, Location.Z - 0.6f), 0.0f, FVector(0.16f, 0.16f, 0.012f), Mat(TEXT("MI_Chrome")), false);
	AddInstance(Cylinder, FVector(Location.X, Location.Y, Location.Z - 0.8f), 0.0f, FVector(0.125f, 0.125f, 0.012f), Mat(TEXT("MI_Graphite")), false);
	AddInstance(Cylinder, FVector(Location.X, Location.Y, Location.Z - 1.0f), 0.0f, FVector(0.07f, 0.07f, 0.012f), Mat(TEXT("MI_LampWarm")), false);
	AddDownlight(Floor, Location - FVector(0.0f, 0.0f, 6.0f), Lumens, Radius, Color, OuterCone);
}

void AKcHotel::Sconce(int32 Floor, const FVector& WallPoint, float Yaw)
{
	// Brass backplate and a linen drum shade, face along local +X, a diffuser in its open bottom; a soft warm wash on the
	// wall. The light is inside the shade, where it cannot light the shade's own faces.
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Shade = WallPoint + Out * 13.0f - FVector(0.0f, 0.0f, 8.0f);
	AddBox(WallPoint + Out * 0.8f, FVector(1.6f, 10.0f, 22.0f), Mat(TEXT("MI_Brass")), Yaw, false);
	AddBox(WallPoint + Out * 7.0f, FVector(9.0f, 2.0f, 2.0f), Mat(TEXT("MI_Brass")), Yaw, false);
	AddInstance(Cylinder, Shade, 0.0f, FVector(0.2f, 0.2f, 0.22f), Mat(TEXT("MI_LampShade")), false);
	AddInstance(Cylinder, Shade - FVector(0.0f, 0.0f, 11.3f), 0.0f, FVector(0.09f, 0.09f, 0.004f), Mat(TEXT("MI_LampWarm")), false);
	AddLight(Floor, WallPoint + Out * 16.0f, 220.0f, 380.0f, Warm); // 320 scalloped the wall by 1406 close to white (walk 18)
}

// ---------------------------------------------------------------------------- build

void AKcHotel::Build()
{
	if (bBuilt) return;
	bBuilt = true;
	Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!Cube || !Cylinder || !Sphere)
	{
		UE_LOG(LogKeycard, Error, TEXT("Hotel: engine basic shapes missing; is /Engine/BasicShapes cooked?"));
		return;
	}
	bHasArt = Mat(TEXT("MI_WallPlaster")) != nullptr;
	if (!bHasArt)
	{
		UE_LOG(LogKeycard, Warning, TEXT("Hotel: imported art missing (run Scripts/ImportAssets.bat); building plain shapes"));
	}
	BuildSlabs();
	BuildCore();
	BuildStairs();
	for (int32 F = LowestFloor; F <= TopFloor; ++F) BuildFacade(F, FloorHeight);
	BuildOutside();
	BuildBasement();
	BuildGround();
	BuildBallroomFloor();
	BuildExecutiveFloor();
	BuildGuestFloor();
	BuildLighting();
	PlaceFixedInteractables();
	BuildAmbience();
	SetActiveFloor(-1);
	RefreshScenes();
	RefreshPeople();
	UE_LOG(LogKeycard, Display, TEXT("Hotel built: %d components, %d instances, %d lights, %d case points, art %s"), ComponentCount, InstanceCount, LightCount,
		CasePoints.Num(), bHasArt ? TEXT("yes") : TEXT("missing"));
}

void AKcHotel::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt) return;
	// Lights follow the player's floor (the stairs included) at once; casework is refreshed twice a second.
	if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		const int32 Floor = FloorAt(Pawn->GetActorLocation().Z - 90.0f);
		if (Floor != ActiveFloor) SetActiveFloor(Floor);
	}
	RefreshTimer -= DeltaSeconds;
	if (RefreshTimer > 0.0f) return;
	RefreshTimer = 0.5f;
	RefreshScenes();
	RefreshPeople();
	const UGameInstance* GI = GetGameInstance();
	const UKcCampaignSubsystem* C = GI ? GI->GetSubsystem<UKcCampaignSubsystem>() : nullptr;
	const UKcGameSubsystem* G = GI ? GI->GetSubsystem<UKcGameSubsystem>() : nullptr;
	const FString Clock = (C && G && G->HasGame()) ? C->ClockText() : FString(TEXT("00:30"));
	if (LobbyClock) LobbyClock->SetText(FText::FromString(Clock));
	if (CctvClock) CctvClock->SetText(FText::FromString(Clock));
}

void AKcHotel::SetActiveFloor(int32 Floor)
{
	ActiveFloor = Floor;
	for (int32 i = 0; i < FloorLights.Num(); ++i)
	{
		ULightComponent* L = FloorLights[i];
		if (!L) continue;
		const int32 F = FloorLightFloor[i];
		// The stair shows its own lights near the player; floors show only their own.
		const bool bOn = F >= StairLightBase - 100 ? FMath::Abs(F - StairLightBase - Floor) <= 2 : F == Floor;
		L->SetVisibility(bOn);
	}
}

void AKcHotel::SetBlackout(bool bOn)
{
	if (bOn == bBlackout) return;
	bBlackout = bOn;
	if (FloorLightIntensity.Num() != FloorLights.Num())
	{
		FloorLightIntensity.Reset();
		for (const ULightComponent* L : FloorLights) FloorLightIntensity.Add(L ? L->Intensity : 0.0f);
	}
	// The emergency fittings: about a tenth of the normal light, enough to find the stairs.
	for (int32 i = 0; i < FloorLights.Num(); ++i)
	{
		if (ULightComponent* L = FloorLights[i]) L->SetIntensity(FloorLightIntensity[i] * (bOn ? 0.12f : 1.0f));
	}
}

// ---------------------------------------------------------------------------- architecture

void AKcHotel::BuildSlabs()
{
	UMaterialInterface* Concrete = Mat(TEXT("MI_Concrete"));
	auto Slab = [&](int32 Floor, float X0, float X1, float Y0, float Y1)
	{
		AddBox(FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, Zf(Floor, -10.0f)), FVector(X1 - X0, Y1 - Y0, 20.0f), Concrete);
	};
	for (int32 Floor : FittedFloors)
	{
		if (Floor == LowestFloor)
		{
			Slab(Floor, -20.0f, MaxX + 20.0f, -HalfY - 20.0f, HalfY + 20.0f);
			continue;
		}
		// Open over the stair flights; the floor landing (X 2120..2250) is part of the slab.
		Slab(Floor, -20.0f, StairLandX1, -HalfY - 20.0f, HalfY + 20.0f);
		Slab(Floor, StairIn1, MaxX + 20.0f, -HalfY - 20.0f, HalfY + 20.0f);
		Slab(Floor, StairLandX1, StairIn1, -HalfY - 20.0f, StairY0);
		Slab(Floor, StairLandX1, StairIn1, StairY1, HalfY + 20.0f);
	}
	// Roof over 14, with a parapet and the plant screen.
	Slab(TopFloor + 1, -20.0f, MaxX + 20.0f, -HalfY - 20.0f, HalfY + 20.0f);
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(FVector(MaxX * 0.5f, Side * (HalfY + 5.0f), Zf(TopFloor + 1, 55.0f)), FVector(MaxX + 40.0f, 30.0f, 110.0f), Mat(TEXT("MI_Facade")));
	}
	AddBox(FVector(CoreX0 + 300.0f, 200.0f, Zf(TopFloor + 1, 180.0f)), FVector(600.0f, 500.0f, 360.0f), Mat(TEXT("MI_Facade")));
	// Floors 1-13: only the stair landings exist inside the shaft.
	for (int32 Floor = 1; Floor < TopFloor; ++Floor)
	{
		if (IsFittedFloor(Floor)) continue;
		Slab(Floor, StairIn0, StairLandX1, StairY0, StairY1);
	}
	// Mid landings, half a storey up from every floor below the top.
	for (int32 Floor = LowestFloor; Floor < TopFloor; ++Floor)
	{
		AddBox(FVector((StairMidX0 + StairIn1) * 0.5f, (StairY0 + StairY1) * 0.5f, Zf(Floor, 191.0f)), FVector(StairIn1 - StairMidX0, StairY1 - StairY0, 18.0f), Concrete);
	}
}

void AKcHotel::WallX(int32 Floor, float Y, float X0, float X1, const TArray<FVector2D>& InDoors, UMaterialInterface* Finish, float Height)
{
	TArray<FVector2D> Openings = InDoors;
	Openings.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
	UMaterialInterface* Skirt = Mat(TEXT("MI_Skirting"));
	auto Segment = [&](float A, float B)
	{
		if (B - A < 1.0f) return;
		const float Mid = (A + B) * 0.5f;
		AddBox(FVector(Mid, Y, Zf(Floor, Height * 0.5f)), FVector(B - A, Wall, Height), Finish);
		for (float Side : {-1.0f, 1.0f})
		{
			AddBox(FVector(Mid, Y + Side * (Wall * 0.5f + 0.6f), Zf(Floor, 6.0f)), FVector(B - A, 1.2f, 12.0f), Skirt, 0.0f, false);
		}
	};
	float Start = X0;
	for (const FVector2D& D : Openings)
	{
		const float Half = D.Y * 0.5f;
		Segment(Start, D.X - Half);
		if (Height > DoorH) AddBox(FVector(D.X, Y, Zf(Floor, (DoorH + Height) * 0.5f)), FVector(D.Y, Wall, Height - DoorH), Finish);
		// Architrave on both faces, and the jamb lining.
		for (float Face : {-1.0f, 1.0f})
		{
			const float FY = Y + Face * (Wall * 0.5f + 0.8f);
			for (float Side : {-1.0f, 1.0f}) AddBox(FVector(D.X + Side * (Half + 3.0f), FY, Zf(Floor, DoorH * 0.5f + 3.0f)), FVector(6.0f, 1.6f, DoorH + 6.0f), Skirt, 0.0f, false);
			AddBox(FVector(D.X, FY, Zf(Floor, DoorH + 3.0f)), FVector(D.Y + 12.0f, 1.6f, 6.0f), Skirt, 0.0f, false);
		}
		for (float Side : {-1.0f, 1.0f}) AddBox(FVector(D.X + Side * (Half - 0.5f), Y, Zf(Floor, DoorH * 0.5f)), FVector(1.0f, Wall, DoorH), Skirt, 0.0f, false);
		Start = D.X + Half;
	}
	Segment(Start, X1);
}

void AKcHotel::WallY(int32 Floor, float X, float Y0, float Y1, const TArray<FVector2D>& InDoors, UMaterialInterface* Finish, float Height)
{
	TArray<FVector2D> Openings = InDoors;
	Openings.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
	UMaterialInterface* Skirt = Mat(TEXT("MI_Skirting"));
	auto Segment = [&](float A, float B)
	{
		if (B - A < 1.0f) return;
		const float Mid = (A + B) * 0.5f;
		AddBox(FVector(X, Mid, Zf(Floor, Height * 0.5f)), FVector(Wall, B - A, Height), Finish);
		for (float Side : {-1.0f, 1.0f})
		{
			AddBox(FVector(X + Side * (Wall * 0.5f + 0.6f), Mid, Zf(Floor, 6.0f)), FVector(1.2f, B - A, 12.0f), Skirt, 0.0f, false);
		}
	};
	float Start = Y0;
	for (const FVector2D& D : Openings)
	{
		const float Half = D.Y * 0.5f;
		Segment(Start, D.X - Half);
		if (Height > DoorH) AddBox(FVector(X, D.X, Zf(Floor, (DoorH + Height) * 0.5f)), FVector(Wall, D.Y, Height - DoorH), Finish);
		for (float Face : {-1.0f, 1.0f})
		{
			const float FX = X + Face * (Wall * 0.5f + 0.8f);
			for (float Side : {-1.0f, 1.0f}) AddBox(FVector(FX, D.X + Side * (Half + 3.0f), Zf(Floor, DoorH * 0.5f + 3.0f)), FVector(1.6f, 6.0f, DoorH + 6.0f), Skirt, 0.0f, false);
			AddBox(FVector(FX, D.X, Zf(Floor, DoorH + 3.0f)), FVector(1.6f, D.Y + 12.0f, 6.0f), Skirt, 0.0f, false);
		}
		for (float Side : {-1.0f, 1.0f}) AddBox(FVector(X, D.X + Side * (Half - 0.5f), Zf(Floor, DoorH * 0.5f)), FVector(Wall, 1.0f, DoorH), Skirt, 0.0f, false);
		Start = D.X + Half;
	}
	Segment(Start, Y1);
}

void AKcHotel::Ceiling(int32 Floor, const FBox2D& Area, float Height, UMaterialInterface* Material)
{
	const FVector2D C = Area.GetCenter();
	const FVector2D S = Area.GetSize();
	AddBox(FVector(C.X, C.Y, Zf(Floor, Height + 4.0f)), FVector(S.X, S.Y, 8.0f), Material, 0.0f, false);
}

void AKcHotel::FloorFinish(int32 Floor, const FBox2D& Area, UMaterialInterface* Material, float Lift)
{
	const FVector2D C = Area.GetCenter();
	const FVector2D S = Area.GetSize();
	AddBox(FVector(C.X, C.Y, Zf(Floor, Lift * 0.5f)), FVector(S.X, S.Y, Lift), Material, 0.0f, false);
}

AKcSign* AKcHotel::SpawnSign(const FString& Location, const FString& Title, const FString& Message, const FVector& At, float Yaw, const FVector& Size, UMaterialInterface* Material)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKcSign* S = GetWorld()->SpawnActor<AKcSign>(At, FRotator(0.0f, Yaw, 0.0f), Params);
	if (!S) return nullptr;
	S->Location = Location;
	S->Title = Title;
	S->Message = Message;
	S->SetAppearance(Cube, Size / 100.0f, Material);
	return S;
}

void AKcHotel::DoorLeaf(int32 Floor, const FVector& Center, float Yaw, float Width, const FString& Plate, bool bGuestRoom, bool bOpen,
	const FString& SignTitle, const FString& SignMessage, const FString& SignLocation, bool bMirror, bool bPlateAtHinge)
{
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();                                 // the plate side
	const FVector Right = FRotator(0.0f, Yaw + (bMirror ? -90.0f : 90.0f), 0.0f).Vector();  // hinge to latch
	const float LW = Width - 6.0f;
	const float LH = DoorH - 3.0f;
	UMaterialInterface* LeafMat = Mat(bGuestRoom ? TEXT("MI_WoodWalnut") : TEXT("MI_DoorPaint"));
	UMaterialInterface* Lever = Mat(bGuestRoom ? TEXT("MI_Brass") : TEXT("MI_Steel"));
	const FVector Base(Center.X, Center.Y, Zf(Floor, 0.0f));
	if (bOpen)
	{
		// Swung 90 degrees into the room about the hinge, held on its closer.
		const FVector Hinge = Base - Right * (Width * 0.5f - 3.0f);
		AddBox(Hinge - Out * (LW * 0.5f + 2.0f) + Right * 2.5f + FVector(0, 0, LH * 0.5f + 1.0f), FVector(4.5f, LW, LH), LeafMat, Yaw + 90.0f);
		AddBox(Hinge - Out * (LW - 8.0f) + Right * 8.0f + FVector(0, 0, 105.0f), FVector(2.0f, 2.0f, 14.0f), Lever, Yaw, false);
	}
	else
	{
		const FVector LeafC = Base + FVector(0, 0, LH * 0.5f + 1.0f);
		if (!SignTitle.IsEmpty())
		{
			SpawnSign(SignLocation, SignTitle, SignMessage, LeafC, Yaw, FVector(4.5f, LW, LH), LeafMat);
		}
		else
		{
			AddBox(LeafC, FVector(4.5f, LW, LH), LeafMat, Yaw);
		}
		// Levers on both faces at the latch side.
		for (float Face : {-1.0f, 1.0f})
		{
			const FVector L = Base + Right * (LW * 0.5f - 9.0f) + Out * Face * 4.2f + FVector(0, 0, 104.0f);
			AddBox(L, FVector(2.0f, 3.0f, 3.0f), Lever, Yaw, false);
			AddBox(L + Out * Face * 1.5f - Right * 6.0f, FVector(1.6f, 14.0f, 2.0f), Lever, Yaw, false);
		}
		// No kick plate: in dark steel at the foot of the leaf it read as the skirting running across the doorway (owner's
		// playthrough, B1), and bright steel as a slot of light under the door (walk 12).
		if (bGuestRoom)
		{
			// Keycard reader above the lever (red LED: locked), viewer at eye height, door closer arm.
			const FVector Reader = Base + Right * (LW * 0.5f - 9.0f) + Out * 3.2f + FVector(0, 0, 128.0f);
			AddBox(Reader, FVector(1.6f, 7.0f, 13.0f), Mat(TEXT("MI_DeviceBlack")), Yaw, false);
			AddBox(Reader + Out * 0.9f + FVector(0, 0, 4.5f), FVector(0.4f, 1.2f, 0.6f), Mat(TEXT("MI_LedRed")), Yaw, false);
			AddInstance(Cylinder, FTransform(FRotator(90.0f, Yaw, 0.0f), Base + Out * 2.6f + FVector(0, 0, 150.0f), FVector(0.022f, 0.022f, 0.012f)), Mat(TEXT("MI_Brass")), false, false);
			AddBox(Base - Out * 4.0f + FVector(0, 0, LH - 6.0f), FVector(4.0f, 30.0f, 4.0f), Mat(TEXT("MI_Steel")), Yaw, false);
		}
	}
	if (!Plate.IsEmpty())
	{
		// Number plate on the wall beside the latch side (the hinge side for a pair), at eye height.
		const FVector P = Base + Right * (bPlateAtHinge ? -1.0f : 1.0f) * (Width * 0.5f + 24.0f) + Out * (Wall * 0.5f + 0.7f) + FVector(0, 0, 152.0f);
		const bool bLong = Plate.Len() > 5;
		AddBox(P, FVector(1.2f, bLong ? 36.0f : 17.0f, bLong ? 9.0f : 11.0f), Mat(bGuestRoom ? TEXT("MI_Brass") : TEXT("MI_Paper")), Yaw, false);
		AddLabel(Plate, P + Out * 0.8f, Yaw, bLong ? 3.2f : 6.5f, Ink, Floor);
	}
}

void AKcHotel::DynamicDoor(const FString& Id, int32 Floor, const FVector& Center, float Yaw, float Width, const FString& Plate, bool bMirror)
{
	// A leaf on a pivot at the hinge; its parts are children so it swings as one.
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Right = FRotator(0.0f, Yaw + (bMirror ? -90.0f : 90.0f), 0.0f).Vector();
	const float Hand = bMirror ? -1.0f : 1.0f; // the pivot's local Y runs hinge to latch
	const float LW = Width - 6.0f;
	const float LH = DoorH - 3.0f;
	const FVector Base(Center.X, Center.Y, Zf(Floor, 0.0f));
	USceneComponent* Pivot = NewObject<USceneComponent>(this, NAME_None);
	Pivot->SetMobility(EComponentMobility::Movable);
	Pivot->SetupAttachment(RootComponent);
	Pivot->SetRelativeLocation(Base - Right * (Width * 0.5f - 3.0f));
	Pivot->SetRelativeRotation(FRotator(0.0f, Yaw, 0.0f));
	Pivot->RegisterComponent();
	const bool bGuest = Plate.IsNumeric();
	auto Part = [&](const FVector& Local, const FVector& Size, UMaterialInterface* M, bool bCollide, UStaticMesh* Mesh = nullptr, const FRotator& Rot = FRotator::ZeroRotator)
	{
		// Local: X out of the plate face, Y from the hinge to the latch (mirrored by Hand), Z up.
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this, NAME_None);
		C->SetMobility(EComponentMobility::Movable);
		C->SetStaticMesh(Mesh ? Mesh : Cube.Get());
		C->SetupAttachment(Pivot);
		C->SetRelativeLocation(FVector(Local.X, Local.Y * Hand, Local.Z));
		C->SetRelativeRotation(Rot);
		C->SetRelativeScale3D(Size / 100.0f);
		if (M) C->SetMaterial(0, M);
		if (!bCollide) C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->RegisterComponent();
		++ComponentCount;
	};
	Part(FVector(0, LW * 0.5f, LH * 0.5f + 1.0f), FVector(4.5f, LW, LH), Mat(bGuest ? TEXT("MI_WoodWalnut") : TEXT("MI_DoorPaint")), true);
	for (float Face : {-1.0f, 1.0f})
	{
		Part(FVector(Face * 4.2f, LW - 9.0f, 104.0f), FVector(2.0f, 3.0f, 3.0f), Mat(bGuest ? TEXT("MI_Brass") : TEXT("MI_Steel")), false);
		Part(FVector(Face * 5.7f, LW - 15.0f, 104.0f), FVector(1.6f, 14.0f, 2.0f), Mat(bGuest ? TEXT("MI_Brass") : TEXT("MI_Steel")), false);
	}
	if (!bGuest) Part(FVector(2.5f, LW * 0.5f, 13.0f), FVector(0.4f, LW - 8.0f, 24.0f), Mat(TEXT("MI_SteelDark")), false);
	if (bGuest)
	{
		Part(FVector(3.2f, LW - 9.0f, 128.0f), FVector(1.6f, 7.0f, 13.0f), Mat(TEXT("MI_DeviceBlack")), false);
		Part(FVector(4.1f, LW - 9.0f, 132.5f), FVector(0.4f, 1.2f, 0.6f), Mat(TEXT("MI_LedRed")), false);
		Part(FVector(2.4f, LW * 0.5f, 150.0f), FVector(2.4f, 2.4f, 0.8f), Mat(TEXT("MI_Brass")), false, Cylinder, FRotator(90.0f, 0.0f, 0.0f)); // the viewer: a round rim, not a cube
		Part(FVector(2.9f, LW * 0.5f, 150.0f), FVector(1.2f, 1.2f, 0.2f), Mat(TEXT("MI_GlassDark")), false, Cylinder, FRotator(90.0f, 0.0f, 0.0f));
	}
	FDynDoor D;
	D.Pivot = Pivot;
	D.ClosedYaw = Yaw;
	D.OpenYaw = Yaw + 90.0f * Hand; // into the room, away from the plate side
	Doors.Add(Id, D);
	if (!Plate.IsEmpty())
	{
		const FVector P = Base + Right * (Width * 0.5f + 24.0f) + Out * (Wall * 0.5f + 0.7f) + FVector(0, 0, 152.0f);
		const bool bLong = Plate.Len() > 5;
		AddBox(P, FVector(1.2f, bLong ? 36.0f : 17.0f, bLong ? 9.0f : 11.0f), Mat(bGuest ? TEXT("MI_Brass") : TEXT("MI_Paper")), Yaw, false);
		AddLabel(Plate, P + Out * 0.8f, Yaw, bLong ? 3.2f : 6.5f, Ink, Floor);
	}
}

void AKcHotel::SetDoorOpen(const FString& Id, bool bOpen)
{
	FDynDoor* D = Doors.Find(Id);
	if (!D || !D->Pivot.IsValid() || D->bOpen == bOpen) return;
	D->bOpen = bOpen;
	// Open, the leaf lies along the room side of the wall from its hinge.
	D->Pivot->SetRelativeRotation(FRotator(0.0f, bOpen ? D->OpenYaw : D->ClosedYaw, 0.0f));
}

void AKcHotel::ExitSign(int32 Floor, const FVector& At, float Yaw)
{
	// Internally lit running-figure sign; double-sided when hung from the ceiling (the caller places both).
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
	AddBox(At, FVector(3.0f, 38.0f, 15.0f), Mat(TEXT("MI_PlasticGrey")), Yaw, false);
	UMaterialInterface* Face = Mat(TEXT("MI_ExitSign"));
	AddBox(At + Out * 1.7f, FVector(0.4f, 35.0f, 12.5f), Face ? Face : Mat(TEXT("MI_LedGreen")), Yaw, false);
	if (!Face) AddLabel(TEXT("EXIT"), At + Out * 2.1f, Yaw, 7.0f, FColor::White, Floor);
}

void AKcHotel::SmokeDetector(int32 Floor, const FVector2D& At, float CeilingZ, bool bCovered)
{
	AddInstance(Cylinder, FVector(At.X, At.Y, Zf(Floor, CeilingZ - 2.0f)), 0.0f, FVector(0.12f, 0.12f, 0.035f), Mat(TEXT("MI_Paper")), false);
	AddInstance(Cylinder, FVector(At.X, At.Y, Zf(Floor, CeilingZ - 4.0f)), 0.0f, FVector(0.07f, 0.07f, 0.01f), Mat(TEXT("MI_Ceramic")), false);
	AddBox(FVector(At.X + 3.0f, At.Y, Zf(Floor, CeilingZ - 4.3f)), FVector(0.6f, 0.6f, 0.4f), Mat(bCovered ? TEXT("MI_DeviceBlack") : TEXT("MI_LedRed")), 0.0f, false);
}

void AKcHotel::CctvDome(int32 Floor, const FVector& At, const FString& CameraId)
{
	// Smoked dome on a white ring, flush with the ceiling at At.Z. The id is on the ring's tag.
	AddInstance(Cylinder, At - FVector(0, 0, 1.0f), 0.0f, FVector(0.17f, 0.17f, 0.02f), Mat(TEXT("MI_Paper")), false);
	AddInstance(Sphere, At - FVector(0, 0, 2.0f), 0.0f, FVector(0.22f, 0.22f, 0.2f), Mat(TEXT("MI_GlassDark")), false);
	AddBox(At + FVector(9.0f, 0, -2.6f), FVector(4.0f, 2.0f, 0.4f), Mat(TEXT("MI_Paper")), 0.0f, false);
	(void)CameraId;
	(void)Floor;
}

void AKcHotel::FireExtinguisher(int32 Floor, const FVector& WallPoint, float Yaw)
{
	// A red cabinet on the wall with a glazed door and a pale lining, the extinguisher standing in it (body, valve, handle,
	// hose, instruction label), the sign above, and the manual call point beside it under its own sign. (A 2 cm cabinet
	// had the cylinder through its glass, and the call point's white FIRE sat on its white element: a bare red square,
	// walk 17.)
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
	const FVector Right = FRotator(0.0f, Yaw + 90.0f, 0.0f).Vector();
	const FVector Base(WallPoint.X, WallPoint.Y, Zf(Floor, 0.0f));
	UMaterialInterface* Red = Mat(TEXT("MI_FireRed"));
	UMaterialInterface* Black = Mat(TEXT("MI_DeviceBlack"));
	const FVector Cab = Base + FVector(0, 0, 105.0f);
	AddBox(Cab + Out * 0.75f, FVector(1.5f, 46.0f, 84.0f), Red, Yaw, false);
	AddBox(Cab + Out * 1.6f, FVector(0.2f, 42.0f, 80.0f), Mat(TEXT("MI_Paper")), Yaw, false);
	for (float S : {-1.0f, 1.0f})
	{
		AddBox(Cab + Out * 7.5f + Right * (S * 22.25f), FVector(12.0f, 1.5f, 84.0f), Red, Yaw, false);
		AddBox(Cab + Out * 7.5f + FVector(0, 0, S * 41.25f), FVector(12.0f, 43.0f, 1.5f), Red, Yaw, false);
	}
	AddBox(Cab + Out * 13.7f, FVector(0.4f, 43.0f, 81.0f), Mat(TEXT("MI_Glass")), Yaw, false);
	const FVector Ext = Base + Out * 7.6f;
	AddInstance(Cylinder, Ext + FVector(0, 0, 92.0f), 0.0f, FVector(0.11f, 0.11f, 0.46f), Red, false);
	AddInstance(Sphere, Ext + FVector(0, 0, 115.0f), 0.0f, FVector(0.11f, 0.11f, 0.06f), Red, false);
	AddInstance(Cylinder, Ext + FVector(0, 0, 120.0f), 0.0f, FVector(0.035f, 0.035f, 0.06f), Black, false);
	AddBox(Ext + FVector(0, 0, 124.5f), FVector(4.0f, 12.0f, 1.5f), Black, Yaw, false);
	AddBox(Ext - Right * 7.0f + FVector(0, 0, 104.0f), FVector(1.6f, 1.6f, 30.0f), Black, Yaw, false);
	AddBox(Ext + Out * 5.65f + FVector(0, 0, 94.0f), FVector(0.3f, 8.0f, 14.0f), Mat(TEXT("MI_Paper")), Yaw, false);
	AddBox(Base + Out * 0.6f + FVector(0, 0, 160.0f), FVector(1.2f, 46.0f, 12.0f), Red, Yaw, false);
	AddLabel(TEXT("FIRE EXTINGUISHER"), Base + Out * 1.4f + FVector(0, 0, 160.0f), Yaw, 3.4f, FColor::White, Floor);
	// Manual call point at 1.4 m: red housing, the white element low in its face, FIRE on the red band above it.
	const FVector Call = Base + Right * 48.0f + FVector(0, 0, 140.0f);
	AddBox(Call + Out * 2.5f, FVector(5.0f, 9.0f, 9.0f), Red, Yaw, false);
	AddBox(Call + Out * 5.15f - FVector(0, 0, 0.9f), FVector(0.3f, 5.6f, 5.6f), Mat(TEXT("MI_Ceramic")), Yaw, false);
	AddLabel(TEXT("FIRE"), Call + Out * 5.2f + FVector(0, 0, 3.3f), Yaw, 1.4f, FColor::White, Floor);
	AddLabel(TEXT("PUSH HERE"), Call + Out * 5.4f - FVector(0, 0, 0.9f), Yaw, 0.9f, Ink, Floor);
	AddBox(Call + Out * 0.6f + FVector(0, 0, 11.0f), FVector(1.2f, 20.0f, 6.0f), Red, Yaw, false);
	AddLabel(TEXT("FIRE ALARM"), Call + Out * 1.4f + FVector(0, 0, 11.0f), Yaw, 2.2f, FColor::White, Floor);
}

void AKcHotel::BuildCore()
{
	for (int32 Floor : FittedFloors)
	{
		const float Ceil = Floor == 14 ? Ceil14 : Floor == 4 ? CeilExec : (Floor == 0 || Floor == 1) ? 300.0f : CeilB1;
		const float BlockH = Floor == 0 ? CeilLobby : Floor == 1 ? CeilBallroom : Ceil;
		UMaterialInterface* Finish = Mat((Floor == 14 || Floor == 4) ? TEXT("MI_Wallpaper") : (Floor == 0 || Floor == 1) ? TEXT("MI_WallPlaster") : TEXT("MI_WallPaint"));
		UMaterialInterface* Concrete = Mat(TEXT("MI_Concrete"));
		// Lift wall with two landing doors; the shafts and risers are solid behind it.
		WallX(Floor, CorrY, CoreX0, CoreX1, {FVector2D(LiftDoorX[0], 110.0f), FVector2D(LiftDoorX[1], 110.0f)}, Finish, Ceil);
		AddBox(FVector((CoreX0 + CoreX1) * 0.5f, (CorrY + Wall * 0.5f + HalfY) * 0.5f, Zf(Floor, BlockH * 0.5f)),
			FVector(CoreX1 - CoreX0, HalfY - CorrY - Wall * 0.5f, BlockH), Concrete);
		AddBox(FVector((CoreX0 + CoreX1) * 0.5f, (-HalfY - 480.0f) * 0.5f, Zf(Floor, BlockH * 0.5f)), FVector(CoreX1 - CoreX0, HalfY - 480.0f, BlockH), Concrete);
		// The corridor face of the stair wall (the stair side is painted concrete).
		for (const FVector2D& Seg : {FVector2D(CoreX0, StairDoorX - 50.0f), FVector2D(StairDoorX + 50.0f, CoreX1)})
		{
			AddBox(FVector((Seg.X + Seg.Y) * 0.5f, -CorrY + Wall * 0.5f + 0.5f, Zf(Floor, Ceil * 0.5f)), FVector(Seg.Y - Seg.X, 1.0f, Ceil), Finish, 0.0f, false);
			AddBox(FVector((Seg.X + Seg.Y) * 0.5f, -CorrY + Wall * 0.5f + 1.6f, Zf(Floor, 6.0f)), FVector(Seg.Y - Seg.X, 1.2f, 12.0f), Mat(TEXT("MI_Skirting")), 0.0f, false);
		}
		AddBox(FVector(StairDoorX, -CorrY + Wall * 0.5f + 0.5f, Zf(Floor, (DoorH + Ceil) * 0.5f)), FVector(100.0f, 1.0f, Ceil - DoorH), Finish, 0.0f, false);
		Ceiling(Floor, FBox2D(FVector2D(CoreX0, -CorrY), FVector2D(CoreX1, CorrY)), Ceil, Mat(TEXT("MI_CeilingWhite")));
		for (float X : LiftDoorX) CeilingLight(Floor, FVector(X, 0.0f, Zf(Floor, Ceil)), DownLumens, Neutral);

		// Landing doors: brushed steel, centre-opening, a position indicator above.
		const float FaceY = CorrY - Wall * 0.5f;
		for (float X : LiftDoorX)
		{
			for (float Side : {-1.0f, 1.0f})
			{
				AddBox(FVector(X + Side * 27.5f, CorrY - 2.0f, Zf(Floor, 105.0f)), FVector(3.0f, 54.0f, 210.0f), Mat(TEXT("MI_Steel")), 90.0f);
			}
			AddBox(FVector(X, FaceY - 0.6f, Zf(Floor, 230.0f)), FVector(1.2f, 36.0f, 13.0f), Mat(TEXT("MI_DeviceBlack")), 90.0f, false);
			AddLabel(FloorLabel(Floor), FVector(X - 6.0f, FaceY - 1.4f, Zf(Floor, 230.0f)), -90.0f, 8.0f, FColor(255, 170, 60), Floor);
			AddBox(FVector(X + 10.0f, FaceY - 1.3f, Zf(Floor, 230.0f)), FVector(0.3f, 4.0f, 6.0f), Mat(TEXT("MI_LedAmber")), 90.0f, false);
		}
		// Call panel between the doors: up/down buttons, and the fire notice below it.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AKcLiftPanel* Panel = GetWorld()->SpawnActor<AKcLiftPanel>(FVector(LiftPanelX, FaceY - 0.8f, Zf(Floor, 115.0f)), FRotator(0.0f, -90.0f, 0.0f), Params))
		{
			Panel->Floor = Floor;
			Panel->Location = Floor == -1 ? TEXT("back_of_house") : Floor == 0 ? TEXT("lobby") : Floor == 1 ? TEXT("ballroom") : Floor == 4 ? TEXT("exec_offices") : TEXT("f14_corridor");
			Panel->SetAppearance(Cube, FVector(0.016f, 0.13f, 0.3f), Mat(TEXT("MI_Steel")));
		}
		for (float Dz : {8.0f, -8.0f})
		{
			AddInstance(Cylinder, FTransform(FRotator(90.0f, -90.0f, 0.0f), FVector(LiftPanelX, FaceY - 2.0f, Zf(Floor, 115.0f + Dz)), FVector(0.04f, 0.04f, 0.01f)), Mat(TEXT("MI_LedAmber")), false, false);
		}
		if (Floor == 0)
		{
			// Guest floors need a room key in the lift after 23:00 (M002: the card was tried here twice).
			AddBox(FVector(LiftPanelX, FaceY - 1.0f, Zf(Floor, 145.0f)), FVector(2.0f, 9.0f, 12.0f), Mat(TEXT("MI_DeviceBlack")), 90.0f, false);
			AddBox(FVector(LiftPanelX, FaceY - 2.1f, Zf(Floor, 149.0f)), FVector(0.3f, 1.2f, 0.6f), Mat(TEXT("MI_LedRed")), 90.0f, false);
			AddLabel(TEXT("Guest floors: tap your room key"), FVector(LiftPanelX, FaceY - 1.2f, Zf(Floor, 158.0f)), -90.0f, 2.2f, Ink, Floor);
		}
		AddBox(FVector(LiftPanelX, FaceY - 0.5f, Zf(Floor, 82.0f)), FVector(1.0f, 28.0f, 12.0f), Mat(TEXT("MI_Paper")), 90.0f, false);
		AddLabel(TEXT("In a fire, do not use the lifts."), FVector(LiftPanelX, FaceY - 1.3f, Zf(Floor, 85.0f)), -90.0f, 1.7f, FColor(170, 30, 30), Floor);
		AddLabel(TEXT("Use the stairs."), FVector(LiftPanelX, FaceY - 1.3f, Zf(Floor, 80.0f)), -90.0f, 1.7f, FColor(170, 30, 30), Floor);

		// Stair A, on the corridor side: the exit sign above the door; the name plate and the fire door notice on the wall
		// beside it. (The notice hung in the open doorway, and a name 1 mm off its plate was lost in the plate, walk 17.) On
		// these floors the stair door is held open on its magnet, so the notice is the one for an automatic door.
		const float SouthFace = -CorrY + Wall * 0.5f + 1.0f;
		ExitSign(Floor, FVector(StairDoorX, SouthFace + 1.5f, Zf(Floor, DoorH + 22.0f)), 90.0f);
		AddBox(FVector(StairDoorX + 80.0f, SouthFace + 0.6f, Zf(Floor, 152.0f)), FVector(1.2f, 26.0f, 10.0f), Mat(TEXT("MI_Paper")), 90.0f, false);
		AddLabel(TEXT("STAIR A"), FVector(StairDoorX + 80.0f, SouthFace + 1.7f, Zf(Floor, 152.0f)), 90.0f, 4.2f, Ink, Floor);
		AddBox(FVector(StairDoorX + 80.0f, SouthFace + 0.6f, Zf(Floor, 132.0f)), FVector(1.2f, 30.0f, 11.0f), Mat(TEXT("MI_SuitNavy")), 90.0f, false);
		AddLabel(TEXT("AUTOMATIC FIRE DOOR"), FVector(StairDoorX + 80.0f, SouthFace + 1.7f, Zf(Floor, 134.6f)), 90.0f, 2.1f, FColor::White, Floor);
		AddLabel(TEXT("KEEP CLEAR"), FVector(StairDoorX + 80.0f, SouthFace + 1.7f, Zf(Floor, 129.4f)), 90.0f, 2.1f, FColor::White, Floor);
		// Floor directory and lobby furniture opposite the lifts.
		if (Floor == 14)
		{
			const FVector D(2470.0f, SouthFace + 0.8f, Zf(Floor, 158.0f));
			AddBox(D, FVector(1.6f, 96.0f, 54.0f), Mat(TEXT("MI_WoodWalnut")), 90.0f, false);
			AddLabel(TEXT("14"), D + FVector(0, 1.2f, 17.0f), 90.0f, 13.0f, Cream, Floor);
			AddLabel(TEXT("< 1401-1405   1411-1414"), D + FVector(0, 1.2f, -2.0f), 90.0f, 3.4f, Cream, Floor);
			AddLabel(TEXT("1406-1410   1415-1420 >"), D + FVector(0, 1.2f, -12.0f), 90.0f, 3.4f, Cream, Floor);
			AddLabel(TEXT("Ice and vending: floor 2"), D + FVector(0, 1.2f, -21.0f), 90.0f, 2.2f, Cream, Floor);
			Desk(FVector(2470.0f, SouthFace + 20.0f, Zf(Floor, 0.0f)), 0.0f, FVector(110.0f, 36.0f, 78.0f), Mat(TEXT("MI_WoodWalnut")));
			Lamp(Floor, FVector(2510.0f, SouthFace + 20.0f, Zf(Floor, 78.0f)), false);
			FireExtinguisher(Floor, FVector(2640.0f, SouthFace - 0.5f, 0.0f), 90.0f);
			CctvDome(Floor, FVector(LiftPanelX, 40.0f, Zf(Floor, Ceil)), TEXT("14-01"));
		}
		else if (Floor == 0)
		{
			ArtPrint(FVector(2470.0f, SouthFace, Zf(Floor, 160.0f)), 90.0f, TEXT("MI_ArtHarbourB"), FVector2D(90.0f, 60.0f));
			Plant(FVector(2640.0f, -110.0f, Zf(Floor, 0.0f)), true);
			CctvDome(Floor, FVector(LiftPanelX, 40.0f, Zf(Floor, Ceil)), TEXT("L-04"));
		}
		else if (Floor == 1 || Floor == 4)
		{
			const FVector D(2470.0f, SouthFace + 0.8f, Zf(Floor, 158.0f));
			AddBox(D, FVector(1.6f, 96.0f, 54.0f), Mat(TEXT("MI_WoodWalnut")), 90.0f, false);
			AddLabel(FloorLabel(Floor), D + FVector(0, 1.2f, 17.0f), 90.0f, 13.0f, Cream, Floor);
			AddLabel(Floor == 1 ? TEXT("Harbour Ballroom  >") : TEXT("< Executive offices   Boardroom"), D + FVector(0, 1.2f, -2.0f), 90.0f, 3.4f, Cream, Floor);
			AddLabel(Floor == 1 ? TEXT("< Quay Suite   Harbour Rooms") : TEXT("< General Manager   Sales  >"), D + FVector(0, 1.2f, -12.0f), 90.0f, 3.4f, Cream, Floor);
			FireExtinguisher(Floor, FVector(2640.0f, SouthFace - 0.5f, 0.0f), 90.0f);
		}
		else
		{
			const FVector D(2470.0f, SouthFace + 0.8f, Zf(Floor, 160.0f));
			AddBox(D, FVector(1.6f, 90.0f, 34.0f), Mat(TEXT("MI_Paper")), 90.0f, false);
			AddLabel(TEXT("B1  BACK OF HOUSE  STAFF ONLY"), D + FVector(0, 1.2f, 9.0f), 90.0f, 3.0f, Ink, Floor);
			AddLabel(TEXT("LP office  CCTV  Receiving  >"), D + FVector(0, 1.2f, -1.0f), 90.0f, 3.0f, Ink, Floor);
			AddLabel(TEXT("< Lockers  Housekeeping  Stair B"), D + FVector(0, 1.2f, -10.0f), 90.0f, 3.0f, Ink, Floor);
			FireExtinguisher(Floor, FVector(2640.0f, SouthFace - 0.5f, 0.0f), 90.0f);
		}
	}
}

void AKcHotel::BuildStairs()
{
	UMaterialInterface* Paint = Mat(TEXT("MI_StairPaint"));
	UMaterialInterface* Concrete = Mat(TEXT("MI_Concrete"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));
	UMaterialInterface* Nosing = Mat(TEXT("MI_SafetyYellow"));
	constexpr int32 Risers = 12;
	const float Run = StairMidX0 - StairLandX1;      // 308
	const float Rise = FloorHeight * 0.5f / Risers;  // 16.7
	const float Going = Run / Risers;                // 25.7
	const float Pitch = FMath::RadiansToDegrees(FMath::Atan2(FloorHeight * 0.5f, Run));
	const float Len = FMath::Sqrt(Run * Run + FMath::Square(FloorHeight * 0.5f));
	const float FlightA0 = StairSpineY + 5.0f, FlightA1 = StairY1;   // north half
	const float FlightB0 = StairY0, FlightB1 = StairSpineY - 5.0f;   // south half
	for (int32 Floor = LowestFloor; Floor <= TopFloor; ++Floor)
	{
		const float H = FloorHeight;
		// Enclosure: two-hour walls on four sides, the door to the lift lobby at X 2185.
		AddBox(FVector(2110.0f, -320.0f, Zf(Floor, H * 0.5f)), FVector(20.0f, 320.0f, H), Paint);
		AddBox(FVector(2690.0f, -320.0f, Zf(Floor, H * 0.5f)), FVector(20.0f, 320.0f, H), Paint);
		AddBox(FVector(2400.0f, -470.0f, Zf(Floor, H * 0.5f)), FVector(600.0f, 20.0f, H), Paint);
		WallX(Floor, -CorrY, CoreX0, CoreX1, {FVector2D(StairDoorX, 100.0f)}, Paint, H);

		const bool bFitted = IsFittedFloor(Floor);
		if (bFitted)
		{
			// Held open on a magnetic release (it closes when the fire alarm sounds).
			// Hinged on the west so the open leaf rests against the landing wall, clear of the flight.
			DoorLeaf(Floor, FVector(StairDoorX, -CorrY, 0.0f), 90.0f, 100.0f, FString(), false, true, FString(), FString(), FString(), true);
			AddBox(FVector(StairIn0 + 8.0f, -250.0f, Zf(Floor, 180.0f)), FVector(16.0f, 6.0f, 6.0f), Steel, 0.0f, false);
		}
		else
		{
			const FString N = FloorLabel(Floor);
			DoorLeaf(Floor, FVector(StairDoorX, -CorrY, 0.0f), -90.0f, 100.0f, TEXT("FLOOR ") + N, false, false, TEXT("Stair door, floor ") + N,
				FString::Printf(TEXT("Floor %s. The stair door is locked from this side: re-entry is by keycard only, and nothing on %s needs you tonight. Keep going, or ride the lift."), *N, *N),
				FString());
		}
		// Stair identification sign on the landing wall.
		// A board in a dark frame, clear of the open door leaf (at 110 wide it touched the leaf, walk 12).
		const FVector S(StairIn0 + 1.0f, -314.0f, Zf(Floor, 170.0f));
		AddBox(S, FVector(1.2f, 96.0f, 120.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
		for (float Side : {-1.0f, 1.0f})
		{
			AddBox(S + FVector(0.4f, Side * 49.0f, 0), FVector(2.0f, 2.5f, 124.5f), Mat(TEXT("MI_Graphite")), 0.0f, false);
			AddBox(S + FVector(0.4f, 0, Side * 61.0f), FVector(2.0f, 100.5f, 2.5f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		}
		// The sprinkler riser runs up every landing in the corner, clipped to the wall, with this floor's
		// control valve and its sign (floor 14's valve is the M013 case point, built with the floor).
		{
			const float Vx = StairIn0 + 8.0f, Vy = -420.0f;
			AddInstance(Cylinder, FVector(Vx, Vy, Zf(Floor, H * 0.5f)), 0.0f, FVector(0.1f, 0.1f, H / 100.0f), Mat(TEXT("MI_FireRed")), false);
			for (float Z : {40.0f, 240.0f, 380.0f})
			{
				AddBox(FVector(Vx, Vy, Zf(Floor, Z)), FVector(12.0f, 12.0f, 3.0f), Steel, 0.0f, false);
				AddBox(FVector(StairIn0 + 1.5f, Vy, Zf(Floor, Z)), FVector(3.0f, 8.0f, 8.0f), Steel, 0.0f, false);
			}
			if (Floor != TopFloor)
			{
				AddBox(FVector(Vx, Vy + 16.0f, Zf(Floor, 120.0f)), FVector(12.0f, 14.0f, 16.0f), Mat(TEXT("MI_FireRed")), 0.0f, false);
				AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 27.0f, Zf(Floor, 120.0f)), FVector(0.03f, 0.03f, 0.1f)), Steel, false, false);
				AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 32.0f, Zf(Floor, 120.0f)), FVector(0.24f, 0.24f, 0.02f)), Mat(TEXT("MI_FireRed")), false, false);
				// A dark disc inside the rim leaves a red ring, with three red spokes and a hub across it: steel
				// spokes over the whole disc read as an asterisk (walk 14).
				AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 33.3f, Zf(Floor, 120.0f)), FVector(0.18f, 0.18f, 0.005f)), Mat(TEXT("MI_Skirting")), false, false);
				for (int32 k = 0; k < 3; ++k)
				{
					AddInstance(Cube, FTransform(FRotator(k * 60.0f, 0.0f, 0.0f), FVector(Vx, Vy + 33.8f, Zf(Floor, 120.0f)), FVector(0.022f, 0.01f, 0.19f)), Mat(TEXT("MI_FireRed")), false, false);
				}
				AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 34.2f, Zf(Floor, 120.0f)), FVector(0.05f, 0.05f, 0.01f)), Steel, false, false);
			}
			// The label over the valve, clear of the floor sign's frame (beside it, the frame cut its edge, walk 13).
			AddBox(FVector(StairIn0 + 1.0f, Vy + 16.0f, Zf(Floor, 205.0f)), FVector(1.0f, 40.0f, 14.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
			AddLabel(TEXT("SPRINKLER VALVE  FLOOR ") + FloorLabel(Floor), FVector(StairIn0 + 1.6f, Vy + 16.0f, Zf(Floor, 207.0f)), 0.0f, 1.8f, FColor(170, 30, 30), Floor);
			AddLabel(TEXT("Keep open and locked"), FVector(StairIn0 + 1.6f, Vy + 16.0f, Zf(Floor, 202.0f)), 0.0f, 1.3f, Ink, Floor);
		}
		AddLabel(TEXT("STAIR A"), S + FVector(1.0f, 0, 46.0f), 0.0f, 8.0f, Ink, Floor);
		AddLabel(FloorLabel(Floor), S + FVector(1.0f, 0, 12.0f), 0.0f, 48.0f, Ink, Floor);
		AddLabel(TEXT("B1 - 14   NO ROOF ACCESS"), S + FVector(1.0f, 0, -30.0f), 0.0f, 3.6f, Ink, Floor);
		AddLabel(Floor == 0 ? TEXT("EXIT HERE TO THE STREET") : TEXT("EXIT DISCHARGE AT G"), S + FVector(1.0f, 0, -42.0f), 0.0f, 3.6f, Ink, Floor);
		// Bulkhead light over the landing.
		AddBox(FVector(StairIn0 + 4.0f, -314.0f, Zf(Floor, 300.0f)), FVector(8.0f, 30.0f, 12.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(StairLightBase + Floor, FVector(StairIn0 + 30.0f, -314.0f, Zf(Floor, 300.0f)), 520.0f, 650.0f, Neutral);

		if (Floor == TopFloor)
		{
			// The top landing: a balustrade along the drop to the flights below.
			AddBox(FVector((StairLandX1 + StairMidX0) * 0.5f, StairSpineY, Zf(Floor, 55.0f)), FVector(Run, 10.0f, 110.0f), Paint);
			AddBox(FVector(StairLandX1 + 3.0f, (FlightA0 + FlightA1) * 0.5f, Zf(Floor, 55.0f)), FVector(6.0f, FlightA1 - FlightA0, 110.0f), Paint);
			continue;
		}
		// Spine wall between the two flights of this storey.
		AddBox(FVector((StairLandX1 + StairMidX0) * 0.5f, StairSpineY, Zf(Floor, H * 0.5f)), FVector(Run, 10.0f, H), Paint);
		// Two flights of twelve risers: A up the north half to the mid landing, B back up the south half.
		auto Flight = [&](float Y0, float Y1, float StartX, float Dir, float Z0)
		{
			const float W = Y1 - Y0;
			const float Yc = (Y0 + Y1) * 0.5f;
			for (int32 i = 0; i < Risers; ++i)
			{
				const float Front = StartX + Dir * i * Going;    // nosing edge of tread i
				const float Top = Z0 + (i + 1) * Rise;
				AddBox(FVector(Front + Dir * Going * 0.5f, Yc, Top - (Rise + 14.0f) * 0.5f), FVector(Going, W, Rise + 14.0f), Concrete, 0.0f, false);
				AddBox(FVector(Front + Dir * 2.0f, Yc, Top + 0.3f), FVector(4.0f, W - 4.0f, 0.6f), Nosing, 0.0f, false);
			}
			const FVector Mid(StartX + Dir * Run * 0.5f, Yc, Z0 + FloorHeight * 0.25f);
			const float Yaw = Dir > 0.0f ? 0.0f : 180.0f;
			// Soffit under the flight, and the walkable ramp along the nosings.
			AddInstance(Cube, FTransform(FRotator(Pitch, Yaw, 0.0f), Mid - FVector(0, 0, 22.0f), FVector(Len / 100.0f, W / 100.0f, 0.14f)), Concrete, false, false);
			AddCollider(Mid - FVector(0, 0, 1.5f), FVector(Len, W, 3.0f), FRotator(Pitch, Yaw, 0.0f));
			// Handrails both sides, 90 cm above the nosings.
			for (float Ry : {Y0 + 5.0f, Y1 - 5.0f})
			{
				AddInstance(Cube, FTransform(FRotator(Pitch, Yaw, 0.0f), FVector(Mid.X, Ry, Mid.Z + 90.0f), FVector(Len / 100.0f, 0.045f, 0.045f)), Steel, false, false);
			}
		};
		Flight(FlightA0, FlightA1, StairLandX1, 1.0f, Zf(Floor, 0.0f));
		Flight(FlightB0, FlightB1, StairMidX0, -1.0f, Zf(Floor, FloorHeight * 0.5f));
	}
}

void AKcHotel::BuildFacade(int32 Floor, float Height)
{
	// Spandrel panels and glazing on the long sides, solid ends; B1 is below ground (concrete).
	UMaterialInterface* Panel = Mat(TEXT("MI_Facade"));
	const bool bFitted = IsFittedFloor(Floor);
	UMaterialInterface* Glass = Mat(bFitted ? TEXT("MI_Glass") : TEXT("MI_GlassDark"));
	// Inside the fitted floors the facade's inner face is plastered: bare facade concrete showed in the GM
	// office, round the windows and over the lobby bar (walk 12). A thin skin 15-15.4 cm in, behind any room's own
	// lining (rooms put theirs at 15-16).
	const bool bLined = bFitted && Floor != LowestFloor;
	auto Lining = [&](bool bAlongX, float Fixed, float A, float B, float Z0, float Z1)
	{
		if (!bLined || Z1 - Z0 < 1.0f) return;
		const float In = bAlongX ? (Fixed > 0.0f ? -1.0f : 1.0f) : (Fixed > MaxX * 0.5f ? -1.0f : 1.0f);
		const float Mid = (A + B) * 0.5f, Face = Fixed + In * 15.2f, Zc = Zf(Floor, (Z0 + Z1) * 0.5f);
		AddBox(bAlongX ? FVector(Mid, Face, Zc) : FVector(Face, Mid, Zc), bAlongX ? FVector(B - A, 0.4f, Z1 - Z0) : FVector(0.4f, B - A, Z1 - Z0), Mat(TEXT("MI_WallPlaster")), 0.0f, false);
	};
	auto Solid = [&](bool bAlongX, float Fixed, float A, float B)
	{
		const float Mid = (A + B) * 0.5f;
		const FVector C = bAlongX ? FVector(Mid, Fixed, Zf(Floor, Height * 0.5f)) : FVector(Fixed, Mid, Zf(Floor, Height * 0.5f));
		AddBox(C, bAlongX ? FVector(B - A, 30.0f, Height) : FVector(30.0f, B - A, Height), Floor == LowestFloor ? Mat(TEXT("MI_Concrete")) : Panel);
		Lining(bAlongX, Fixed, A, B, 0.0f, Height);
	};
	auto Glazed = [&](bool bAlongX, float Fixed, float A, float B, float Sill, float Head)
	{
		const float Mid = (A + B) * 0.5f;
		auto At = [&](float Z) { return bAlongX ? FVector(Mid, Fixed, Zf(Floor, Z)) : FVector(Fixed, Mid, Zf(Floor, Z)); };
		auto Size = [&](float T, float H) { return bAlongX ? FVector(B - A, T, H) : FVector(T, B - A, H); };
		if (Sill > 0.0f) AddBox(At(Sill * 0.5f), Size(30.0f, Sill), Panel);
		AddBox(At((Sill + Head) * 0.5f), Size(3.0f, Head - Sill), Glass);
		AddBox(At((Head + Height) * 0.5f), Size(30.0f, Height - Head), Panel);
		Lining(bAlongX, Fixed, A, B, 0.0f, Sill);
		Lining(bAlongX, Fixed, A, B, Head, Height);
		const int32 Bays = FMath::Max(1, FMath::RoundToInt((B - A) / 150.0f));
		for (int32 i = 0; i <= Bays; ++i)
		{
			const float P = A + (B - A) * i / Bays;
			AddBox(bAlongX ? FVector(P, Fixed, Zf(Floor, (Sill + Head) * 0.5f)) : FVector(Fixed, P, Zf(Floor, (Sill + Head) * 0.5f)),
				bAlongX ? FVector(6.0f, 16.0f, Head - Sill) : FVector(16.0f, 6.0f, Head - Sill), Mat(TEXT("MI_Graphite")), 0.0f, false);
		}
	};
	if (Floor == LowestFloor)
	{
		Solid(true, HalfY, 0.0f, MaxX);
		// South side: the dock door (X 4200..4300) opens onto the covered dock apron under the plaza.
		Solid(true, -HalfY, 0.0f, DockDoorX - 50.0f);
		Solid(true, -HalfY, DockDoorX + 50.0f, MaxX);
		AddBox(FVector(DockDoorX, -HalfY, Zf(Floor, (DoorH + Height) * 0.5f)), FVector(100.0f, 30.0f, Height - DoorH), Mat(TEXT("MI_Concrete")));
		Solid(false, 0.0f, -HalfY, HalfY);
		Solid(false, MaxX, -HalfY, HalfY);
		return;
	}
	if (Floor == 0)
	{
		// Harbour side: glass either side of the stone wall behind the front desk.
		Glazed(true, HalfY, 0.0f, 500.0f, 0.0f, 330.0f);
		Solid(true, HalfY, 500.0f, 1800.0f);
		Glazed(true, HalfY, 1800.0f, CoreX0, 0.0f, 330.0f);
		Solid(true, HalfY, CoreX0, MaxX);
		// Street side: full-height glass with the entrance (Y -130..130, built with the lobby).
		Glazed(false, 0.0f, -HalfY, -130.0f, 0.0f, 330.0f);
		Glazed(false, 0.0f, 130.0f, HalfY, 0.0f, 330.0f);
		// A glass transom up to the lobby ceiling, so the lit canopy shows over the doors (a blank panel, walk 14).
		AddBox(FVector(0.0f, 0.0f, Zf(Floor, (330.0f + CeilLobby) * 0.5f)), FVector(3.0f, 260.0f, CeilLobby - 330.0f), Glass);
		AddBox(FVector(0.0f, 0.0f, Zf(Floor, (CeilLobby + Height) * 0.5f)), FVector(30.0f, 260.0f, Height - CeilLobby), Panel);
		// City side: the lobby bar's windows.
		Glazed(true, -HalfY, 0.0f, CoreX0, 40.0f, 330.0f);
		Solid(true, -HalfY, CoreX0, MaxX);
		Solid(false, MaxX, -HalfY, HalfY);
		return;
	}
	if (Floor == TopFloor)
	{
		// Each room has its window; the closet and the core are solid; 1412's balcony door is a gap here.
		Glazed(true, HalfY, 0.0f, CoreX0, 60.0f, 250.0f);
		Glazed(true, HalfY, CoreX1, MaxX, 60.0f, 250.0f);
		Solid(true, HalfY, CoreX0, CoreX1);
		Solid(true, -HalfY, 0.0f, 300.0f);
		Glazed(true, -HalfY, 300.0f, 1040.0f, 60.0f, 250.0f);
		AddBox(FVector(1085.0f, -HalfY, Zf(Floor, (DoorH + Height) * 0.5f)), FVector(90.0f, 30.0f, Height - DoorH), Panel);
		Lining(true, -HalfY, 1040.0f, 1130.0f, DoorH, Height); // bare concrete showed over the balcony door (walk 14)
		Glazed(true, -HalfY, 1130.0f, CoreX0, 60.0f, 250.0f);
		Solid(true, -HalfY, CoreX0, CoreX1);
		Glazed(true, -HalfY, CoreX1, MaxX, 60.0f, 250.0f);
		Solid(false, 0.0f, -HalfY, HalfY);
		Solid(false, MaxX, -HalfY, -CorrY);
		Solid(false, MaxX, CorrY, HalfY);
		Glazed(false, MaxX, -CorrY, CorrY, 40.0f, 250.0f);
		return;
	}
	// Floors 1-13: the same rhythm, dark glass (nothing behind it is built).
	Glazed(true, HalfY, 0.0f, CoreX0, 60.0f, 250.0f);
	Glazed(true, HalfY, CoreX1, MaxX, 60.0f, 250.0f);
	Solid(true, HalfY, CoreX0, CoreX1);
	Glazed(true, -HalfY, 0.0f, CoreX0, 60.0f, 250.0f);
	Glazed(true, -HalfY, CoreX1, MaxX, 60.0f, 250.0f);
	Solid(true, -HalfY, CoreX0, CoreX1);
	Solid(false, 0.0f, -HalfY, HalfY);
	Solid(false, MaxX, -HalfY, HalfY);
}

void AKcHotel::BuildOutside()
{
	// The quay to the north, the street to the west, the plaza and the entrance canopy.
	// Paving around the footprint only: one slab across it closed the stair between B1 and G at street level.
	{
		const float PX0 = -2400.0f, PX1 = 7200.0f, PY0 = -3500.0f, PY1 = 2500.0f;
		const float FX0 = -20.0f, FX1 = MaxX + 20.0f, FY0 = -HalfY - 20.0f, FY1 = HalfY + 20.0f;
		auto Paving = [this](float X0, float X1, float Y0, float Y1)
		{
			AddBox(FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, -3.0f), FVector(X1 - X0, Y1 - Y0, 6.0f), Mat(TEXT("MI_Paving")));
		};
		Paving(PX0, PX1, PY0, FY0);
		Paving(PX0, PX1, FY1, PY1);
		Paving(PX0, FX0, FY0, FY1);
		Paving(FX1, PX1, FY0, FY1);
		// The city's ground out to the skylines on the three street sides (the harbour is north): without it the
		// lobby looked out over a void and the far towers stood on nothing (walk 12).
		auto Ground = [this](float X0, float X1, float Y0, float Y1)
		{
			AddBox(FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, -10.0f), FVector(X1 - X0, Y1 - Y0, 6.0f), Mat(TEXT("MI_Asphalt")), 0.0f, false);
		};
		Ground(-40000.0f, 45000.0f, -40000.0f, PY0);
		Ground(-40000.0f, PX0, PY0, PY1);
		Ground(PX1, 45000.0f, PY0, PY1);
	}
	// Across the street on the west, south and east (stopping short of the quay on the north): a kerb, a pavement and a
	// terrace of three or four storeys with lit shopfronts (GenerateTextures.py FRONTAGES), so the lobby looks at a street,
	// not at towers. 16 m tall: at 28 m it filled the floor 4 windows with one flat wall (walk 13).
	{
		struct FFrontage { FVector2D At; float Yaw; float Width; const TCHAR* Material; FVector2D Pave; FVector2D PaveSize; };
		for (const FFrontage& K : {FFrontage{FVector2D(-3300.0f, -1800.0f), 0.0f, 8400.0f, TEXT("MI_StreetW"), FVector2D(-2900.0f, -1800.0f), FVector2D(800.0f, 8400.0f)},
				 FFrontage{FVector2D(2400.0f, -4300.0f), 90.0f, 14000.0f, TEXT("MI_StreetS"), FVector2D(2400.0f, -4200.0f), FVector2D(14000.0f, 200.0f)},
				 FFrontage{FVector2D(8000.0f, -2050.0f), 180.0f, 8900.0f, TEXT("MI_StreetE"), FVector2D(7900.0f, -2050.0f), FVector2D(200.0f, 8900.0f)}})
		{
			AddBox(FVector(K.Pave.X, K.Pave.Y, -3.0f), FVector(K.PaveSize.X, K.PaveSize.Y, 6.0f), Mat(TEXT("MI_Paving")), 0.0f, false);
			UMaterialInterface* M = Mat(K.Material);
			if (!M) continue;
			if (UStaticMeshComponent* C = AddMesh(Cube, FVector(K.At.X, K.At.Y, 800.0f), K.Yaw, FVector(0.1f, K.Width / 100.0f, 16.0f), M, false))
			{
				C->SetCastShadow(false);
				C->bAffectDistanceFieldLighting = false;
				C->bVisibleInRayTracing = false;
			}
			// Lamp heads along the far kerb (lit by the texture's shop glow; no light of their own).
			const FVector Along = FRotator(0.0f, K.Yaw + 90.0f, 0.0f).Vector();
			const FVector Out = FRotator(0.0f, K.Yaw, 0.0f).Vector();
			for (float T = -K.Width * 0.5f + 750.0f; T < K.Width * 0.5f; T += 1500.0f)
			{
				const FVector P = FVector(K.At.X, K.At.Y, 0.0f) + Out * 120.0f + Along * T;
				AddInstance(Cylinder, P + FVector(0, 0, 300.0f), 0.0f, FVector(0.1f, 0.1f, 6.0f), Mat(TEXT("MI_Graphite")), false);
				AddBox(P + Out * 30.0f + FVector(0, 0, 598.0f), FVector(70.0f, 20.0f, 8.0f), Mat(TEXT("MI_Graphite")), K.Yaw, false);
				AddBox(P + Out * 30.0f + FVector(0, 0, 593.0f), FVector(44.0f, 14.0f, 1.0f), Mat(TEXT("MI_LampWarm")), K.Yaw, false);
			}
		}
		AddBox(FVector(2400.0f, -3800.0f, -8.0f), FVector(14000.0f, 600.0f, 6.0f), Mat(TEXT("MI_Asphalt")), 0.0f, false);
		// The far kerb on the street the lobby faces, and light on that pavement: through the doors the street had no
		// edge and no ground (walk 14). The shopfronts are self-lit; these light the paving in front of them.
		AddBox(FVector(-2500.0f, -1800.0f, -3.0f), FVector(14.0f, 8400.0f, 12.0f), Mat(TEXT("MI_Concrete")), 0.0f, false);
		for (float Y : {-1500.0f, 0.0f, 1500.0f}) AddLight(0, FVector(-2700.0f, Y, 500.0f), 2500.0f, 1300.0f, Warm);
		AddBox(FVector(7500.0f, -2050.0f, -8.0f), FVector(600.0f, 8900.0f, 6.0f), Mat(TEXT("MI_Asphalt")), 0.0f, false);
	}
	AddBox(FVector(-1700.0f, 0.0f, -8.0f), FVector(1600.0f, 9000.0f, 6.0f), Mat(TEXT("MI_Asphalt")));
	AddBox(FVector(-905.0f, 0.0f, -3.0f), FVector(14.0f, 9000.0f, 12.0f), Mat(TEXT("MI_Concrete")));
	AddBox(FVector(2400.0f, 2560.0f, -60.0f), FVector(9600.0f, 120.0f, 120.0f), Mat(TEXT("MI_Concrete")));
	AddBox(FVector(2400.0f, 51300.0f, -160.0f), FVector(200000.0f, 97400.0f, 10.0f), Mat(TEXT("MI_Water")), 0.0f, false); // to the far shore
	for (float X = -600.0f; X < MaxX + 600.0f; X += 600.0f)
	{
		AddInstance(Cylinder, FVector(X, 2420.0f, 45.0f), 0.0f, FVector(0.22f, 0.22f, 0.9f), Mat(TEXT("MI_Graphite")), true);
	}
	for (float Y = -3000.0f; Y <= 3000.0f; Y += 1500.0f)
	{
		AddInstance(Cylinder, FVector(-760.0f, Y, 300.0f), 0.0f, FVector(0.12f, 0.12f, 6.0f), Mat(TEXT("MI_Graphite")), true);
		AddBox(FVector(-800.0f, Y, 598.0f), FVector(90.0f, 24.0f, 10.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(-820.0f, Y, 592.0f), FVector(50.0f, 18.0f, 1.0f), Mat(TEXT("MI_LampWarm")), 0.0f, false);
		AddLight(0, FVector(-820.0f, Y, 580.0f), 3500.0f, 1400.0f, Warm);
	}
	// Canopy with the name over the street doors.
	AddBox(FVector(-200.0f, 0.0f, 395.0f), FVector(400.0f, 640.0f, 30.0f), Mat(TEXT("MI_Graphite")));
	AddLabel(TEXT("VELDANE GRAND"), FVector(-401.0f, 0.0f, 400.0f), 180.0f, 18.0f, BrassText, 0);
	AddLabel(TEXT("HARBOURFRONT"), FVector(-401.0f, 0.0f, 383.0f), 180.0f, 7.0f, BrassText, 0);
	for (float Y : {-200.0f, 0.0f, 200.0f}) CeilingLight(0, FVector(-200.0f, Y, 380.0f), 900.0f, Warm, 500.0f);
	for (float Y : {-280.0f, 280.0f}) Plant(FVector(-60.0f, Y, 0.0f), true);
	// Bollards at the kerb: dark steel with a domed cap and a reflective band (bare steel posts read as grey stubs, walk 17).
	for (float Y : {-480.0f, -380.0f, 380.0f, 480.0f})
	{
		AddInstance(Cylinder, FVector(-450.0f, Y, 45.0f), 0.0f, FVector(0.2f, 0.2f, 0.9f), Mat(TEXT("MI_SteelDark")), true);
		AddInstance(Sphere, FVector(-450.0f, Y, 90.0f), 0.0f, FVector(0.2f, 0.2f, 0.08f), Mat(TEXT("MI_SteelDark")), false);
		AddInstance(Cylinder, FVector(-450.0f, Y, 78.0f), 0.0f, FVector(0.205f, 0.205f, 0.05f), Mat(TEXT("MI_SafetyYellow")), false);
	}
}

// ---------------------------------------------------------------------------- B1: back of house

void AKcHotel::BuildBasement()
{
	const int32 F = -1;
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	FloorFinish(F, FBox2D(FVector2D(0.0f, -CorrY), FVector2D(MaxX, CorrY)), Mat(TEXT("MI_FloorConcrete")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -CorrY), FVector2D(CoreX0, CorrY)), CeilB1, Mat(TEXT("MI_CeilingMid"))); // Soft glared white over the battens, Dim read black
	Ceiling(F, FBox2D(FVector2D(CoreX1, -CorrY), FVector2D(MaxX, CorrY)), CeilB1, Mat(TEXT("MI_CeilingMid")));
	// Corridor walls and doors.
	WallX(F, CorrY, 15.0f, CoreX0, {FVector2D(600.0f, 100.0f), FVector2D(1400.0f, 100.0f)}, Paint, CeilB1);
	WallX(F, CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3200.0f, 100.0f), FVector2D(4250.0f, 100.0f)}, Paint, CeilB1);
	WallX(F, -CorrY, 15.0f, CoreX0, {FVector2D(1000.0f, 160.0f)}, Paint, CeilB1);
	WallX(F, -CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3500.0f, 160.0f)}, Paint, CeilB1);
	DoorLeaf(F, FVector(600.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("LOCKERS M"), false, false, TEXT("Staff lockers (men)"),
		TEXT("Staff locker room. LP opens a locker only with the employee and HR present, under the locker policy."), TEXT("back_of_house"));
	DoorLeaf(F, FVector(1400.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("LOCKERS F"), false, false, TEXT("Staff lockers (women)"),
		TEXT("Staff locker room. LP opens a locker only with the employee and HR present, under the locker policy."), TEXT("back_of_house"));
	for (float Side : {-1.0f, 1.0f})
	{
		// Double doors held open on their closers: housekeeping and receiving work through the night.
		DoorLeaf(F, FVector(1000.0f + Side * 40.0f, -CorrY, 0.0f), 90.0f, 80.0f, Side < 0.0f ? TEXT("HOUSEKEEPING") : FString(), false, true, FString(), FString(), FString(), Side < 0.0f, true);
		DoorLeaf(F, FVector(3500.0f + Side * 40.0f, -CorrY, 0.0f), 90.0f, 80.0f, Side < 0.0f ? TEXT("RECEIVING") : FString(), false, true, FString(), FString(), FString(), Side < 0.0f, true);
	}
	// Stair B at the west end: an alarmed exit, not tonight's route.
	DoorLeaf(F, FVector(17.0f, 0.0f, 0.0f), 0.0f, 100.0f, TEXT("STAIR B"), false, false, TEXT("Stair B"),
		TEXT("Stair B: alarmed exit door. In an emergency, use it; tonight, use Stair A by the lifts."), TEXT("back_of_house"));
	ExitSign(F, FVector(19.0f, 0.0f, Zf(F, DoorH + 22.0f)), 0.0f);
	// LP office and CCTV room doors, open, with access readers.
	DoorLeaf(F, FVector(3200.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("LOSS PREVENTION"), false, true);
	DoorLeaf(F, FVector(4250.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("CCTV CONTROL"), false, true);
	for (float X : {3200.0f, 4250.0f})
	{
		AddBox(FVector(X - 78.0f, CorrY - Wall * 0.5f - 1.0f, Zf(F, 120.0f)), FVector(2.0f, 8.0f, 12.0f), Mat(TEXT("MI_DeviceBlack")), 90.0f, false);
		AddBox(FVector(X - 78.0f, CorrY - Wall * 0.5f - 2.1f, Zf(F, 124.0f)), FVector(0.3f, 1.2f, 0.6f), Mat(TEXT("MI_LedGreen")), 90.0f, false);
	}
	AddLabel(TEXT("Authorised staff only"), FVector(4250.0f + 74.0f, CorrY - Wall * 0.5f - 1.6f, Zf(F, 140.0f)), -90.0f, 2.2f, Ink, F);
	// Exposed services: cable tray, the red sprinkler main with its heads, fluorescent battens.
	AddBox(FVector(MaxX * 0.5f, 105.0f, Zf(F, CeilB1 - 20.0f)), FVector(MaxX - 40.0f, 30.0f, 6.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
	AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(MaxX * 0.5f, -105.0f, Zf(F, CeilB1 - 14.0f)), FVector(0.1f, 0.1f, (MaxX - 40.0f) / 100.0f)),
		Mat(TEXT("MI_FireRed")), false, false);
	for (float X = 300.0f; X < MaxX; X += 300.0f)
	{
		AddInstance(Cylinder, FVector(X, -105.0f, Zf(F, CeilB1 - 22.0f)), 0.0f, FVector(0.02f, 0.02f, 0.12f), Mat(TEXT("MI_Brass")), false);
		if (FMath::Fmod(X, 600.0f) < 1.0f && (X < CoreX0 || X > CoreX1))
		{
			AddBox(FVector(X, 0.0f, Zf(F, CeilB1 - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
			AddLight(F, FVector(X, 0.0f, Zf(F, CeilB1 - 30.0f)), 700.0f, 650.0f, Cool);
		}
	}
	// The two dead ends, 600 cm past the last batten, read black: a batten over each.
	for (float X : {180.0f, MaxX - 180.0f})
	{
		AddBox(FVector(X, 0.0f, Zf(F, CeilB1 - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, 0.0f, Zf(F, CeilB1 - 30.0f)), 600.0f, 600.0f, Cool);
	}
	for (float X : {450.0f, 1350.0f, 3050.0f, 3950.0f}) SmokeDetector(F, FVector2D(X, 30.0f), CeilB1);
	CctvDome(F, FVector(3700.0f, 0.0f, Zf(F, CeilB1)), TEXT("B1-02"));
	CctvDome(F, FVector(800.0f, 0.0f, Zf(F, CeilB1)), TEXT("B1-01"));
	// Wall protection rail for trolleys, and a linen cage left by housekeeping.
	for (float Y : {CorrY - Wall * 0.5f - 2.0f, -CorrY + Wall * 0.5f + 2.0f})
	{
		AddBox(FVector(MaxX * 0.5f + 300.0f, Y, Zf(F, 30.0f)), FVector(MaxX - 1000.0f, 4.0f, 8.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	}
	AddBox(FVector(1250.0f, -105.0f, Zf(F, 80.0f)), FVector(100.0f, 70.0f, 150.0f), Mat(TEXT("MI_Steel")), 0.0f, true);
	AddBox(FVector(1250.0f, -105.0f, Zf(F, 75.0f)), FVector(92.0f, 62.0f, 120.0f), Mat(TEXT("MI_Linen")), 0.0f, false);
	FireExtinguisher(F, FVector(4600.0f, -CorrY + Wall * 0.5f, 0.0f), 90.0f);
	BuildLpOffice();
	BuildCctvRoom();
	BuildHousekeeping();
	BuildLoadingDock();
}

void AKcHotel::BuildLpOffice()
{
	const int32 F = -1;
	const float X0 = CoreX1, X1 = 3700.0f, Y0 = CorrY + Wall * 0.5f, Y1 = HalfY - 15.0f;
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_Carpet")));
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilB1, Mat(TEXT("MI_CeilingLow"))); // Low, not Dim: the downlights are spots now and no longer light it (round 18)
	WallY(F, X1, Y0, Y1, {}, Mat(TEXT("MI_WallPaint")), CeilB1);
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	AddBox(FVector((X0 + X1) * 0.5f, Y1 - 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(X1 - X0, 1.0f, CeilB1), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	// Ceiling LED panels.
	for (const FVector2D& P : {FVector2D(2950.0f, 420.0f), FVector2D(3450.0f, 420.0f), FVector2D(2950.0f, 800.0f), FVector2D(3450.0f, 800.0f)})
	{
		AddBox(FVector(P.X, P.Y, Zf(F, CeilB1 - 0.5f)), FVector(120.0f, 60.0f, 1.0f), Mat(TEXT("MI_LightPanel")), 0.0f, false);
		AddLight(F, FVector(P.X, P.Y, Zf(F, CeilB1 - 6.0f)), 900.0f, 750.0f, Neutral, P.X < 3000.0f && P.Y > 700.0f, true, FVector2D(120.0f, 60.0f));
	}
	// The panels light only below them: two soft fills keep the upper walls from going black.
	for (float X : {2950.0f, 3450.0f}) AddLight(F, FVector(X, 610.0f, Zf(F, CeilB1 - 45.0f)), 220.0f, 520.0f, Neutral);
	SmokeDetector(F, FVector2D(3200.0f, 600.0f), CeilB1);

	// Your desk, against the north wall: two screens (shift board and report system), in-tray, radio.
	UMaterialInterface* Oak = Mat(TEXT("MI_WoodOak"));
	Desk(FVector(2930.0f, Y1 - 42.0f, Zf(F, 0.0f)), 0.0f, FVector(170.0f, 80.0f, DeskH), Oak);
	TaskChair(FVector(2930.0f, Y1 - 125.0f, Zf(F, 0.0f)), 90.0f);
	Monitor(FVector(2885.0f, Y1 - 30.0f, Zf(F, DeskH)), -90.0f, nullptr);
	Monitor(FVector(2975.0f, Y1 - 30.0f, Zf(F, DeskH)), -90.0f, nullptr);
	AddBox(FVector(2930.0f, Y1 - 62.0f, Zf(F, DeskH + 1.0f)), FVector(44.0f, 15.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Screen(2975.0f, Y1 - 31.6f, Zf(F, DeskH + 38.5f));
		if (AKcReportTerminal* T = GetWorld()->SpawnActor<AKcReportTerminal>(Screen, FRotator(0.0f, -90.0f, 0.0f), Params))
		{
			T->Location = TEXT("lp_office");
			T->SetAppearance(Cube, FVector(0.003f, 0.59f, 0.33f), Mat(TEXT("MI_ScreenShiftBoard")));
		}
		SpawnCasePoint(TEXT("lp_office"), TEXT("report_terminal"), FString(), FVector(2885.0f, Y1 - 31.6f, Zf(F, DeskH + 38.5f)), -90.0f, Cube, FVector(0.003f, 0.59f, 0.33f),
			Mat(TEXT("MI_ScreenReport")), EKcUseStyle::Screen);
	}
	SpawnCasePoint(TEXT("lp_office"), TEXT("lp_desk"), FString(), FVector(3000.0f, Y1 - 70.0f, Zf(F, DeskH + 4.0f)), 8.0f, Cube, FVector(0.32f, 0.25f, 0.08f),
		Mat(TEXT("MI_Graphite")), EKcUseStyle::PickUp);
	// The in-tray's papers: a few sheets out of line, a form on top, a clip.
	for (int32 k = 0; k < 4; ++k)
	{
		AddBox(FVector(3000.0f + k * 0.6f, Y1 - 70.0f - k * 0.4f, Zf(F, DeskH + 8.4f + k * 0.4f)), FVector(30.0f, 21.0f, 0.3f), Mat(TEXT("MI_Paper")), 8.0f - k * 3.0f, false);
	}
	AddBox(FVector(2988.0f, Y1 - 63.0f, Zf(F, DeskH + 10.2f)), FVector(3.0f, 1.0f, 0.6f), Mat(TEXT("MI_Chrome")), 8.0f, false);
	AddInstance(Cylinder, FVector(2860.0f, Y1 - 70.0f, Zf(F, DeskH + 5.0f)), 0.0f, FVector(0.08f, 0.08f, 0.1f), Mat(TEXT("MI_Ceramic")), false);
	// Ines's desk: her jacket on the chair, a live view on the screen.
	Desk(FVector(3300.0f, Y1 - 42.0f, Zf(F, 0.0f)), 0.0f, FVector(170.0f, 80.0f, DeskH), Oak);
	TaskChair(FVector(3290.0f, Y1 - 130.0f, Zf(F, 0.0f)), 70.0f);
	Monitor(FVector(3300.0f, Y1 - 30.0f, Zf(F, DeskH)), -90.0f, Mat(TEXT("MI_ScreenCctv2")));
	AddBox(FVector(3283.0f, Y1 - 150.0f, Zf(F, 75.0f)), FVector(6.0f, 46.0f, 50.0f), Mat(TEXT("MI_UniformBlack")), 70.0f, false);
	AddBox(FVector(3360.0f, Y1 - 60.0f, Zf(F, DeskH + 8.0f)), FVector(2.0f, 14.0f, 16.0f), Mat(TEXT("MI_Graphite")), -100.0f, false);
	// Printer and shredder (reports are printed for the morning).
	// The copier: a grey cabinet with two paper drawers, the body, the scanner lid, a control panel and the output tray.
	{
		const FVector C(3520.0f, Y1 - 35.0f, Zf(F, 0.0f));
		AddBox(C + FVector(0, 0, 32.0f), FVector(60.0f, 55.0f, 64.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, true);
		for (float Z : {18.0f, 46.0f})
		{
			AddBox(C + FVector(0, -27.8f, Z), FVector(56.0f, 0.6f, 24.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
			AddBox(C + FVector(0, -28.4f, Z + 7.0f), FVector(18.0f, 0.8f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		}
		AddBox(C + FVector(0, 0, 82.0f), FVector(56.0f, 50.0f, 36.0f), Mat(TEXT("MI_Paper")), 0.0f, true);
		AddBox(C + FVector(0, 2.0f, 101.5f), FVector(56.0f, 46.0f, 3.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(C + FVector(18.0f, -24.0f, 98.0f), FVector(22.0f, 10.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddBox(C + FVector(18.0f, -28.5f, 98.5f), FVector(14.0f, 0.4f, 1.2f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
		AddBox(C + FVector(-34.0f, 0, 78.0f), FVector(10.0f, 36.0f, 1.0f), Mat(TEXT("MI_Graphite")), -8.0f, false);
		AddBox(C + FVector(-36.0f, 0, 79.4f), FVector(9.0f, 21.0f, 1.0f), Mat(TEXT("MI_Paper")), -8.0f, false);
		AddBox(C + FVector(0, -24.0f, 70.0f), FVector(52.0f, 1.0f, 2.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	}
	AddBox(FVector(3610.0f, Y1 - 30.0f, Zf(F, 35.0f)), FVector(36.0f, 28.0f, 70.0f), Mat(TEXT("MI_Graphite")), 0.0f, true);

	// Radio base on the west counter, with the handheld charger rack and the radio log.
	AddBox(FVector(X0 + 32.0f, 500.0f, Zf(F, 45.0f)), FVector(60.0f, 300.0f, 90.0f), Mat(TEXT("MI_WoodOak")), 0.0f, true);
	SpawnCasePoint(TEXT("lp_office"), TEXT("lp_radio"), FString(), FVector(X0 + 34.0f, 570.0f, Zf(F, 97.0f)), 0.0f, Cube, FVector(0.28f, 0.34f, 0.13f),
		Mat(TEXT("MI_DeviceBlack")), EKcUseStyle::Screen, FVector(X0 + 34.0f, 570.0f, Zf(F, 110.0f)));
	AddBox(FVector(X0 + 48.5f, 570.0f, Zf(F, 98.0f)), FVector(0.4f, 14.0f, 4.0f), Mat(TEXT("MI_LedAmber")), 0.0f, false);
	AddInstance(Cylinder, FVector(X0 + 30.0f, 610.0f, Zf(F, 112.0f)), 0.0f, FVector(0.01f, 0.01f, 0.3f), Mat(TEXT("MI_Graphite")), false);
	AddInstance(Sphere, FVector(X0 + 30.0f, 610.0f, Zf(F, 128.0f)), 0.0f, FVector(0.05f), Mat(TEXT("MI_DeviceBlack")), false);
	AddBox(FVector(X0 + 30.0f, 430.0f, Zf(F, 94.0f)), FVector(30.0f, 70.0f, 8.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	for (int32 i = 0; i < 6; ++i)
	{
		const float Y = 400.0f + i * 12.0f;
		AddBox(FVector(X0 + 30.0f, Y, Zf(F, 108.0f)), FVector(5.0f, 6.0f, 20.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddBox(FVector(X0 + 45.5f, Y, Zf(F, 99.0f)), FVector(0.3f, 1.0f, 0.6f), Mat(i == 2 ? TEXT("MI_LedGreen") : TEXT("MI_LedRed")), 0.0f, false);
	}
	AddBox(FVector(X0 + 34.0f, 490.0f, Zf(F, 90.8f)), FVector(23.0f, 32.0f, 1.6f), Mat(TEXT("MI_Paper")), 4.0f, false);
	// Policy board over the counter.
	AddBox(FVector(X0 + 1.5f, 830.0f, Zf(F, 165.0f)), FVector(3.0f, 170.0f, 95.0f), Mat(TEXT("MI_Cork")), 0.0f, false);
	AddLabel(TEXT("OBSERVE   REPORT   DE-ESCALATE"), FVector(X0 + 3.4f, 830.0f, Zf(F, 196.0f)), 0.0f, 5.0f, Ink, F);
	AddBox(FVector(X0 + 3.2f, 790.0f, Zf(F, 160.0f)), FVector(0.4f, 50.0f, 40.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("Searches of people, detention and force:"), FVector(X0 + 3.7f, 790.0f, Zf(F, 172.0f)), 0.0f, 1.5f, Ink, F);
	AddLabel(TEXT("policy and local law only. Ask first."), FVector(X0 + 3.7f, 790.0f, Zf(F, 168.0f)), 0.0f, 1.5f, Ink, F);
	AddBox(FVector(X0 + 3.2f, 870.0f, Zf(F, 155.0f)), FVector(0.4f, 44.0f, 58.0f), Mat(TEXT("MI_FloorPlan")), 0.0f, false);

	// Evidence locker: six numbered compartments, each with a keypad.
	SpawnCasePoint(TEXT("lp_office"), TEXT("evidence_locker"), FString(), FVector(X1 - Wall * 0.5f - 26.0f, 560.0f, Zf(F, 95.0f)), 0.0f, Cube,
		FVector(0.5f, 1.8f, 1.9f), Mat(TEXT("MI_Steel")), EKcUseStyle::Screen, FVector(X1 - 60.0f, 560.0f, Zf(F, 130.0f)));
	for (int32 i = 0; i < 6; ++i)
	{
		const float Y = 560.0f + ((i % 2) ? 44.0f : -44.0f);
		const float Z = 160.0f - (i / 2) * 62.0f;
		const FVector Face(X1 - Wall * 0.5f - 51.5f, Y, Zf(F, Z));
		AddBox(Face, FVector(1.0f, 84.0f, 58.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
		AddBox(Face - FVector(0.8f, -28.0f, -14.0f), FVector(0.6f, 10.0f, 13.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddLabel(FString::FromInt(i + 1), Face - FVector(1.2f, 20.0f, -16.0f), 180.0f, 6.0f, Ink, F);
	}
	AddBox(FVector(X1 - Wall * 0.5f - 1.5f, 560.0f, Zf(F, 215.0f)), FVector(1.2f, 120.0f, 22.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("EVIDENCE"), FVector(X1 - Wall * 0.5f - 2.4f, 560.0f, Zf(F, 219.0f)), 180.0f, 6.0f, Ink, F);
	AddLabel(TEXT("Log every item. Sign every seal."), FVector(X1 - Wall * 0.5f - 2.4f, 560.0f, Zf(F, 209.0f)), 180.0f, 2.4f, Ink, F);
	// Coffee corner, AED and first aid on the south wall, key control cabinet by the door.
	AddBox(FVector(X1 - 40.0f, 250.0f, Zf(F, 45.0f)), FVector(60.0f, 120.0f, 90.0f), Mat(TEXT("MI_WoodOak")), 0.0f, true);
	AddBox(FVector(X1 - 45.0f, 280.0f, Zf(F, 112.0f)), FVector(36.0f, 28.0f, 44.0f), Mat(TEXT("MI_DeviceBlack")), 180.0f, true);
	AddInstance(Cylinder, FVector(X1 - 40.0f, 225.0f, Zf(F, 101.0f)), 0.0f, FVector(0.16f, 0.16f, 0.22f), Mat(TEXT("MI_Steel")), false);
	for (int32 i = 0; i < 3; ++i) AddInstance(Cylinder, FVector(X1 - 70.0f, 205.0f + i * 12.0f, Zf(F, 95.0f)), 0.0f, FVector(0.08f, 0.08f, 0.1f), Mat(TEXT("MI_Ceramic")), false);
	const float SouthFace = Y0 + 1.0f;
	// AED and first-aid cabinets: white boxes with a green sign panel (all-green emissive boxes glared, walk 12).
	AddBox(FVector(3560.0f, SouthFace + 6.0f, Zf(F, 135.0f)), FVector(42.0f, 12.0f, 48.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddBox(FVector(3560.0f, SouthFace + 12.2f, Zf(F, 150.0f)), FVector(34.0f, 0.4f, 12.0f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
	AddLabel(TEXT("AED"), FVector(3560.0f, SouthFace + 12.6f, Zf(F, 150.0f)), 90.0f, 5.0f, FColor::White, F);
	AddBox(FVector(3560.0f, SouthFace + 12.2f, Zf(F, 128.0f)), FVector(30.0f, 0.4f, 24.0f), Mat(TEXT("MI_GlassDark")), 0.0f, false);
	AddBox(FVector(3490.0f, SouthFace + 6.0f, Zf(F, 135.0f)), FVector(32.0f, 12.0f, 26.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddBox(FVector(3490.0f, SouthFace + 12.2f, Zf(F, 135.0f)), FVector(14.0f, 0.4f, 14.0f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
	AddLabel(TEXT("+"), FVector(3490.0f, SouthFace + 12.6f, Zf(F, 135.0f)), 90.0f, 14.0f, FColor::White, F);
	AddBox(FVector(2830.0f, SouthFace + 5.0f, Zf(F, 150.0f)), FVector(70.0f, 10.0f, 80.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
	AddBox(FVector(2830.0f, SouthFace + 10.3f, Zf(F, 150.0f)), FVector(64.0f, 0.4f, 74.0f), Mat(TEXT("MI_Glass")), 0.0f, false);
	for (int32 r = 0; r < 3; ++r)
	{
		for (int32 c = 0; c < 5; ++c)
		{
			AddBox(FVector(2806.0f + c * 12.0f, SouthFace + 8.0f, Zf(F, 172.0f - r * 22.0f)), FVector(2.0f, 1.0f, 6.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
		}
	}
	AddLabel(TEXT("KEY CONTROL: sign every key out and in"), FVector(2830.0f, SouthFace + 1.2f, Zf(F, 200.0f)), 90.0f, 2.2f, Ink, F);
	// Whiteboard with tonight's shift notes.
	AddBox(FVector(3410.0f, SouthFace + 1.5f, Zf(F, 160.0f)), FVector(210.0f, 3.0f, 110.0f), Mat(TEXT("MI_Whiteboard")), 0.0f, false);
	// Briefing table, and the shift log on it (the office's own case point).
	Desk(FVector(3200.0f, 560.0f, Zf(F, 0.0f)), 0.0f, FVector(160.0f, 90.0f, DeskH), Mat(TEXT("MI_WoodWalnut")));
	for (const FVector2D& C : {FVector2D(3150.0f, 490.0f), FVector2D(3250.0f, 490.0f), FVector2D(3150.0f, 630.0f), FVector2D(3250.0f, 630.0f)})
	{
		Chair(FVector(C.X, C.Y, Zf(F, 0.0f)), C.Y < 560.0f ? 90.0f : -90.0f, TEXT("dining_chair_02"));
	}
	SpawnCasePoint(TEXT("lp_office"), FString(), TEXT("Shift log"), FVector(3220.0f, 565.0f, Zf(F, DeskH + 1.5f)), 12.0f, Cube, FVector(0.3f, 0.22f, 0.03f),
		Mat(TEXT("MI_SuitNavy")), EKcUseStyle::PickUp);
	Plant(FVector(X1 - 45.0f, Y1 - 45.0f, Zf(F, 0.0f)), true);
}

void AKcHotel::BuildCctvRoom()
{
	const int32 F = -1;
	const float X0 = 3700.0f + Wall * 0.5f, X1 = MaxX - 15.0f, Y0 = CorrY + Wall * 0.5f, Y1 = HalfY - 15.0f;
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FabricDark")));
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilB1, Mat(TEXT("MI_CeilingDim")));
	// Painted walls: the acoustic fabric (wool texture) rendered black in this dim room at any tint, and the
	// back wall behind the screens showed the basement's bare concrete.
	AddBox(FVector((X0 + X1) * 0.5f, Y1 - 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(X1 - X0, 1.0f, CeilB1), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	AddBox(FVector(X1 - 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	// Video wall: four by two monitors, each a different camera page.
	AddBox(FVector(4250.0f, Y1 - 4.0f, Zf(F, 182.0f)), FVector(470.0f, 8.0f, 150.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	static const TCHAR* const Pages[] = {TEXT("MI_ScreenCctv1"), TEXT("MI_ScreenCctv2"), TEXT("MI_ScreenCctv3"), TEXT("MI_ScreenCctv4")};
	for (int32 r = 0; r < 2; ++r)
	{
		for (int32 c = 0; c < 4; ++c)
		{
			AddBox(FVector(4250.0f - 171.0f + c * 114.0f, Y1 - 8.3f, Zf(F, 146.0f + r * 72.0f)), FVector(110.0f, 0.4f, 64.0f), Mat(Pages[(r * 4 + c) % 4]), 0.0f, false);
		}
	}
	AddBox(FVector(4250.0f, Y1 - 4.3f, Zf(F, 266.0f)), FVector(44.0f, 8.6f, 14.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false); // the clock's housing: the digits hung in the air
	CctvClock = AddLabel(TEXT("00:30"), FVector(4250.0f, Y1 - 9.0f, Zf(F, 266.0f)), -90.0f, 9.0f, FColor(255, 170, 60), F);
	// Console: three screens facing the operator, the keyboard and the PTZ joystick.
	Desk(FVector(4250.0f, 650.0f, Zf(F, 0.0f)), 0.0f, FVector(380.0f, 85.0f, DeskH), Mat(TEXT("MI_Graphite")));
	Monitor(FVector(4130.0f, 668.0f, Zf(F, DeskH)), -80.0f, Mat(TEXT("MI_ScreenCctv3")));
	Monitor(FVector(4250.0f, 670.0f, Zf(F, DeskH)), -90.0f, nullptr);
	Monitor(FVector(4370.0f, 668.0f, Zf(F, DeskH)), -100.0f, Mat(TEXT("MI_ScreenCctv4")));
	SpawnCasePoint(TEXT("cctv_room"), TEXT("cctv_console"), FString(), FVector(4250.0f, 668.4f, Zf(F, DeskH + 38.5f)), -90.0f, Cube, FVector(0.003f, 0.59f, 0.33f),
		Mat(TEXT("MI_ScreenCctv1")), EKcUseStyle::Screen);
	AddBox(FVector(4250.0f, 625.0f, Zf(F, DeskH + 1.0f)), FVector(46.0f, 16.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	AddBox(FVector(4330.0f, 625.0f, Zf(F, DeskH + 2.0f)), FVector(18.0f, 16.0f, 4.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	AddInstance(Cylinder, FVector(4330.0f, 625.0f, Zf(F, DeskH + 9.0f)), 0.0f, FVector(0.03f, 0.03f, 0.1f), Mat(TEXT("MI_Graphite")), false);
	TaskChair(FVector(4250.0f, 560.0f, Zf(F, 0.0f)), 90.0f);
	// Recorders: thirty days, then the oldest footage is overwritten.
	AddBox(FVector(X1 - 32.0f, 300.0f, Zf(F, 100.0f)), FVector(60.0f, 62.0f, 200.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, true);
	for (int32 u = 0; u < 8; ++u)
	{
		for (int32 l = 0; l < 4; ++l)
		{
			AddBox(FVector(X1 - 62.5f, 280.0f + l * 9.0f, Zf(F, 40.0f + u * 20.0f)), FVector(0.3f, 1.0f, 0.6f), Mat((u + l) % 3 ? TEXT("MI_LedGreen") : TEXT("MI_LedAmber")), 0.0f, false);
		}
	}
	AddLabel(TEXT("NVR-1  NVR-2   30-DAY RETENTION"), FVector(X1 - 62.6f, 300.0f, Zf(F, 193.0f)), 180.0f, 2.4f, Cream, F); // on the rack's face, not above it
	// Export station: footage leaves this room only on sealed, logged media.
	Desk(FVector(X0 + 40.0f, 420.0f, Zf(F, 0.0f)), 90.0f, FVector(140.0f, 70.0f, DeskH), Mat(TEXT("MI_WoodOak")));
	Monitor(FVector(X0 + 25.0f, 420.0f, Zf(F, DeskH)), 0.0f, Mat(TEXT("MI_ScreenExport")));
	AddBox(FVector(X0 + 55.0f, 470.0f, Zf(F, DeskH + 4.0f)), FVector(16.0f, 24.0f, 8.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	SpawnCasePoint(TEXT("cctv_room"), FString(), TEXT("Export log"), FVector(X0 + 55.0f, 380.0f, Zf(F, DeskH + 1.0f)), -6.0f, Cube, FVector(0.23f, 0.32f, 0.02f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	AddBox(FVector(X0 + 1.5f, 760.0f, Zf(F, 160.0f)), FVector(3.0f, 120.0f, 80.0f), Mat(TEXT("MI_FloorPlan")), 0.0f, false);
	AddLabel(TEXT("CAMERA MAP"), FVector(X0 + 1.4f, 760.0f, Zf(F, 206.0f)), 0.0f, 3.0f, Ink, F); // on the wall over the map
	// Dim, cool light so the screens read.
	for (float X : {4000.0f, 4500.0f}) CeilingLight(F, FVector(X, 450.0f, Zf(F, CeilB1)), 950.0f, Cool, 650.0f); // 650 against the video wall still crushed the room black (walk 12)
	AddLight(F, FVector(4250.0f, 900.0f, Zf(F, 180.0f)), 220.0f, 420.0f, FLinearColor(0.6f, 0.75f, 1.0f));
	// The door end and the side walls, away from the screens.
	for (float X : {3950.0f, 4550.0f}) CeilingLight(F, FVector(X, 250.0f, Zf(F, CeilB1)), 650.0f, Cool, 550.0f);
	for (float X : {X0 + 40.0f, X1 - 40.0f}) AddLight(F, FVector(X, 600.0f, Zf(F, CeilB1 - 25.0f)), 320.0f, 380.0f, Cool); // the side walls' upper band
	// A warm desk lamp at the console, so the operator's desk and the floor read in front of the wall.
	Lamp(F, FVector(4418.0f, 680.0f, Zf(F, DeskH)), false);
	SmokeDetector(F, FVector2D(4250.0f, 420.0f), CeilB1);
}

void AKcHotel::BuildHousekeeping()
{
	// Housekeeping and laundry, west of the core: washers, linen shelving, the folding table, parked
	// trolleys, the supervisor's desk and the master key cabinet by the door (M008).
	const int32 F = -1;
	const float X0 = 15.0f, X1 = CoreX0, Y0 = -HalfY + 15.0f, Y1 = -CorrY - Wall * 0.5f;
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FloorVinyl"))); // sheet vinyl: the tile read as guest carpet (walk 12)
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilB1, Mat(TEXT("MI_CeilingLow"))); // lower glow: MI_CeilingMid blew out white under the battens (walk 14)
	AddBox(FVector(X1 - 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Paint, 0.0f, false);
	AddBox(FVector((X0 + X1) * 0.5f, Y0 + 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(X1 - X0, 1.0f, CeilB1), Paint, 0.0f, false);
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Paint, 0.0f, false);

	// Three washer-extractors on plinths along the west wall, glass doors facing the room.
	for (int32 i = 0; i < 3; ++i)
	{
		const float Y = -420.0f - i * 150.0f;
		AddBox(FVector(X0 + 10.0f + 50.0f, Y, Zf(F, 8.0f)), FVector(100.0f, 110.0f, 16.0f), Mat(TEXT("MI_Concrete")));
		AddBox(FVector(X0 + 60.0f, Y, Zf(F, 16.0f + 60.0f)), FVector(90.0f, 100.0f, 120.0f), Steel);
		AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(X0 + 106.0f, Y, Zf(F, 72.0f)), FVector(0.56f, 0.56f, 0.03f)), Mat(TEXT("MI_Chrome")), false, false);
		AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(X0 + 107.5f, Y, Zf(F, 72.0f)), FVector(0.44f, 0.44f, 0.02f)), Mat(TEXT("MI_GlassDark")), false, false);
		AddBox(FVector(X0 + 105.5f, Y, Zf(F, 122.0f)), FVector(1.0f, 80.0f, 14.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddBox(FVector(X0 + 106.2f, Y + 30.0f, Zf(F, 122.0f)), FVector(0.4f, 6.0f, 2.0f), Mat(i == 1 ? TEXT("MI_LedAmber") : TEXT("MI_LedGreen")), 0.0f, false);
		AddLabel(FString::Printf(TEXT("W%d"), i + 1), FVector(X0 + 106.4f, Y - 25.0f, Zf(F, 123.0f)), 0.0f, 3.0f, Cream, F);
	}
	AddLabel(TEXT("Laundry restarts 05:00"), FVector(X0 + 1.4f, -570.0f, Zf(F, 190.0f)), 0.0f, 3.0f, Ink, F);

	// Folded linen: a stack of separate pieces, each a little out of line, so a shelf reads as towels and sheets, not as
	// white blocks (walk 12). Each has a rounded fold along its front edge (square edges read as slabs, walk 14), and a
	// grey core behind them shows through the gaps between pieces as the shadow between folds (0.6 cm gaps over nothing
	// left a stack one white slab, walk 17).
	FRandomStream LinenRand(812);
	auto Folded = [&](const FVector& Base, float SX, float SY, float Height, const TCHAR* Material)
	{
		float Z = 0.0f;
		while (Z + 4.0f <= Height)
		{
			const float T = LinenRand.FRandRange(4.5f, 7.5f);
			const FVector Off(LinenRand.FRandRange(-1.5f, 1.5f), LinenRand.FRandRange(-1.5f, 1.5f), Z + T * 0.5f);
			AddBox(Base + Off, FVector(SX, SY, T - 1.2f), Mat(Material), LinenRand.FRandRange(-2.0f, 2.0f), false);
			const float Rad = (T - 1.2f) * 0.5f;
			AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), Base + Off + FVector(0, SY * 0.5f - Rad * 0.5f, 0), FVector(Rad / 50.0f, Rad / 50.0f, SX / 100.0f)), Mat(Material), false, false);
			Z += T;
		}
		if (Z > 0.0f) AddBox(Base + FVector(0, 0, Z * 0.5f), FVector(SX - 5.0f, SY - 5.0f, Z - 1.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	};
	// Linen shelving along the south wall: sheets, pillowcases, bath and hand towels, bathrobes. The bays share sheet-steel
	// ends (front and back posts side by side read as doubled uprights, walk 17).
	for (int32 Bay = 0; Bay < 4; ++Bay)
	{
		const float Xc = 380.0f + Bay * 240.0f;
		for (float Side : {-1.0f, 1.0f})
		{
			if (Side < 0.0f && Bay > 0) continue;
			AddBox(FVector(Xc + Side * 120.0f, Y0 + 28.0f, Zf(F, 100.0f)), FVector(2.0f, 50.0f, 200.0f), Steel);
		}
		for (int32 L = 0; L < 5; ++L)
		{
			const float Z = 10.0f + L * 46.0f;
			AddBox(FVector(Xc, Y0 + 28.0f, Zf(F, Z)), FVector(240.0f, 50.0f, 2.0f), Steel, 0.0f, false);
			if (L == 4) break;
			for (int32 k = 0; k < 5; ++k)
			{
				if ((Bay * 7 + L * 3 + k) % 9 == 0) continue; // a gap where tonight's turn-down took them
				const bool bSheets = (L + k + Bay) % 3 == 0;
				const TCHAR* M = Bay == 3 && L > 1 ? TEXT("MI_Paper") : (L == 0 && k % 2 ? TEXT("MI_Rug") : TEXT("MI_Linen")); // bath mats on the bottom shelf
				Folded(FVector(Xc - 92.0f + k * 46.0f, Y0 + 28.0f, Zf(F, Z + 1.0f)), bSheets ? 38.0f : 34.0f, bSheets ? 40.0f : 36.0f, bSheets ? 22.0f : 32.0f, M);
			}
		}
	}
	// Folding table with the night's pressed linen, and the wire linen cages waiting for the morning.
	Desk(FVector(700.0f, -620.0f, Zf(F, 0.0f)), 0.0f, FVector(240.0f, 90.0f, 90.0f), Steel);
	for (int32 k = 0; k < 4; ++k)
	{
		Folded(FVector(615.0f + k * 56.0f, -620.0f, Zf(F, 90.0f)), 40.0f, 36.0f, 10.0f + (k % 2) * 10.0f, TEXT("MI_Linen"));
	}
	for (float X : {1150.0f, 1290.0f})
	{
		for (float Dx : {-53.0f, 53.0f}) for (float Dy : {-33.0f, 33.0f}) AddBox(FVector(X + Dx, -560.0f + Dy, Zf(F, 80.0f)), FVector(3.0f, 3.0f, 150.0f), Steel, 0.0f, true);
		for (float Z : {12.0f, 80.0f, 154.0f})
		{
			for (float Dy : {-33.0f, 33.0f}) AddBox(FVector(X, -560.0f + Dy, Zf(F, Z)), FVector(106.0f, 1.5f, 1.5f), Steel, 0.0f, false);
			for (float Dx : {-53.0f, 53.0f}) AddBox(FVector(X + Dx, -560.0f, Zf(F, Z)), FVector(1.5f, 66.0f, 1.5f), Steel, 0.0f, false);
		}
		for (int32 i = 1; i < 8; ++i) for (float Dy : {-33.0f, 33.0f}) AddBox(FVector(X - 53.0f + i * 13.25f, -560.0f + Dy, Zf(F, 80.0f)), FVector(0.8f, 0.8f, 148.0f), Steel, 0.0f, false);
		AddBox(FVector(X, -560.0f, Zf(F, 11.0f)), FVector(104.0f, 64.0f, 2.0f), Steel, 0.0f, false);
		// Soiled linen in a canvas liner bag that fills the cage, a crumpled sheet and a towel spilling over its top.
		// Ellipsoids heaped in the cage read as pebbles (walks 14 to 18).
		AddBox(FVector(X, -560.0f, Zf(F, 60.0f)), FVector(98.0f, 58.0f, 96.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
		AddInstance(Sphere, FVector(X, -560.0f, Zf(F, 108.0f)), 0.0f, FVector(0.49f, 0.29f, 0.1f), Mat(TEXT("MI_FabricAccent")), false);
		AddInstance(Sphere, FTransform(FRotator(8.0f, LinenRand.FRandRange(0.0f, 30.0f), -6.0f), FVector(X - 8.0f, -560.0f, Zf(F, 114.0f)), FVector(0.42f, 0.26f, 0.08f)), Mat(TEXT("MI_Linen")), false, false);
		AddInstance(Sphere, FTransform(FRotator(-10.0f, LinenRand.FRandRange(60.0f, 120.0f), 4.0f), FVector(X + 20.0f, -556.0f, Zf(F, 118.0f)), FVector(0.22f, 0.16f, 0.06f)), Mat(TEXT("MI_Rug")), false, false);
	}
	// Trolleys parked for the night, each loaded for the morning: open shelves of linen, the amenity caddy on top,
	// the linen bag at one end and the rubbish bag at the other.
	for (int32 i = 0; i < 3; ++i)
	{
		const FVector T(300.0f + i * 230.0f, -300.0f, Zf(F, 0.0f)); // 150 apart, one trolley's bags hung in the next
		for (float Dx : {-63.0f, 63.0f}) AddBox(T + FVector(Dx, 0, 52.0f), FVector(4.0f, 58.0f, 92.0f), Mat(TEXT("MI_Graphite")), 0.0f, true);
		for (float Z : {14.0f, 52.0f, 96.0f}) AddBox(T + FVector(0, 0, Z), FVector(126.0f, 58.0f, 3.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
		for (float Dx : {-55.0f, 55.0f}) for (float Dy : {-24.0f, 24.0f})
		{
			AddInstance(Cylinder, FTransform(FRotator(90.0f, 90.0f, 0.0f), T + FVector(Dx, Dy, 6.0f), FVector(0.1f, 0.1f, 0.03f)), Mat(TEXT("MI_DeviceBlack")), false, false);
		}
		for (int32 k = 0; k < 3; ++k)
		{
			Folded(T + FVector(-40.0f + k * 40.0f, 0, 16.0f), 34.0f, 44.0f, 30.0f, k == 1 ? TEXT("MI_Rug") : TEXT("MI_Linen"));
			Folded(T + FVector(-40.0f + k * 40.0f, 0, 54.0f), 34.0f, 44.0f, 34.0f, TEXT("MI_Linen"));
		}
		AddBox(T + FVector(0, 0, 103.0f), FVector(40.0f, 30.0f, 12.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
		for (int32 k = 0; k < 4; ++k) AddInstance(Cylinder, T + FVector(-12.0f + k * 8.0f, 0, 113.0f), 0.0f, FVector(0.04f, 0.04f, 0.1f), Mat(k % 2 ? TEXT("MI_Ceramic") : TEXT("MI_BottleAmber")), false);
		AddInstance(Sphere, T + FVector(88.0f, 0, 62.0f), 0.0f, FVector(0.36f, 0.5f, 0.75f), Mat(TEXT("MI_FabricDark")), false);
		AddInstance(Sphere, T + FVector(-86.0f, 0, 55.0f), 0.0f, FVector(0.32f, 0.44f, 0.6f), Mat(TEXT("MI_Graphite")), false);
		for (float Dx : {-70.0f, 70.0f}) AddBox(T + FVector(Dx, 0, 96.0f), FVector(14.0f, 50.0f, 2.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddLabel(FString::Printf(TEXT("FLOOR %d"), 12 + i), T + FVector(65.4f, 0.0f, 82.0f), 0.0f, 3.0f, Cream, F);
	}

	// Supervisor's desk against the core wall, with the allocation sheets and the radio charger.
	Desk(FVector(X1 - 50.0f, -380.0f, Zf(F, 0.0f)), 90.0f, FVector(160.0f, 80.0f, DeskH), Mat(TEXT("MI_WoodOak")));
	Monitor(FVector(X1 - 30.0f, -380.0f, Zf(F, DeskH)), 180.0f, Mat(TEXT("MI_ScreenFrontOffice")));
	TaskChair(FVector(X1 - 130.0f, -380.0f, Zf(F, 0.0f)), 0.0f);
	AddBox(FVector(X1 - 60.0f, -320.0f, Zf(F, DeskH + 1.0f)), FVector(23.0f, 30.0f, 1.2f), Mat(TEXT("MI_Paper")), 6.0f, false);
	// Room assignments board: tonight's turn-downs by floor (the room's own case point).
	SpawnCasePoint(TEXT("housekeeping"), FString(), TEXT("Room assignments board"), FVector(X1 - 2.5f, -560.0f, Zf(F, 160.0f)), 180.0f, Cube,
		FVector(0.03f, 1.8f, 1.0f), Mat(TEXT("MI_Whiteboard")), EKcUseStyle::Screen);
	AddLabel(TEXT("TONIGHT: FLOORS 12-14 TURN-DOWN  M. DUARTE"), FVector(X1 - 4.4f, -560.0f, Zf(F, 204.0f)), 180.0f, 2.4f, Ink, F);

	// Master key cabinet on the corridor wall by the door: glazed, a hook per key, the log on the shelf
	// under it, and its own camera.
	const float Face = Y1 - 1.0f;
	// The back panel is the case point; the hooks and keys hang in front of it behind the glass door.
	const FVector Cab(1250.0f, Face - 2.0f, Zf(F, 150.0f));
	SpawnCasePoint(TEXT("housekeeping"), TEXT("key_cabinet"), FString(), Cab, -90.0f, Cube, FVector(0.04f, 0.8f, 0.9f), Steel, EKcUseStyle::Screen, Cab - FVector(0, 30.0f, 0));
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(Cab + FVector(Side * 39.0f, -5.0f, 0), FVector(2.0f, 10.0f, 90.0f), Steel, 0.0f, false);
		AddBox(Cab + FVector(0, -5.0f, Side * 44.0f), FVector(80.0f, 10.0f, 2.0f), Steel, 0.0f, false);
	}
	AddBox(Cab - FVector(0, 9.8f, 0), FVector(76.0f, 0.4f, 86.0f), Mat(TEXT("MI_Glass")), 0.0f, false);
	for (int32 r = 0; r < 3; ++r)
	{
		for (int32 c = 0; c < 6; ++c)
		{
			// One empty hook: HK-M12-14-03 (M008).
			const bool bEmpty = r == 1 && c == 2;
			AddBox(Cab + FVector(-28.0f + c * 11.0f, -3.0f, 24.0f - r * 24.0f), FVector(1.5f, 2.0f, 1.5f), Mat(TEXT("MI_Brass")), 0.0f, false);
			if (!bEmpty) AddBox(Cab + FVector(-28.0f + c * 11.0f, -4.4f, 18.0f - r * 24.0f), FVector(5.4f, 0.6f, 8.6f), Mat(c % 2 ? TEXT("MI_PlasticGrey") : TEXT("MI_DeviceBlack")), 0.0f, false);
		}
	}
	AddBox(FVector(1250.0f, Face - 15.0f, Zf(F, 95.0f)), FVector(80.0f, 30.0f, 3.0f), Steel, 0.0f, false);
	AddBox(FVector(1235.0f, Face - 16.0f, Zf(F, 98.0f)), FVector(30.0f, 23.0f, 3.0f), Mat(TEXT("MI_SuitNavy")), -8.0f, false);
	AddLabel(TEXT("MASTER KEYS"), FVector(1250.0f, Face - 0.6f, Zf(F, 210.0f)), -90.0f, 4.0f, Ink, F);
	AddLabel(TEXT("Sign out. Sign in. Keys stay on the person."), FVector(1250.0f, Face - 0.6f, Zf(F, 202.0f)), -90.0f, 2.0f, Ink, F);
	CctvDome(F, FVector(1250.0f, -360.0f, Zf(F, CeilB1)), TEXT("B1-03"));

	// Chemical store (locked) in the south-east corner, and the sink.
	AddBox(FVector(X1 - 40.0f, Y0 + 40.0f, Zf(F, 90.0f)), FVector(70.0f, 70.0f, 180.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, true);
	AddLabel(TEXT("CHEMICALS: KEEP LOCKED"), FVector(X1 - 76.0f, Y0 + 40.0f, Zf(F, 150.0f)), 180.0f, 2.4f, Ink, F);
	AddLabel(TEXT("Data sheets: red folder"), FVector(X1 - 76.0f, Y0 + 40.0f, Zf(F, 143.0f)), 180.0f, 1.8f, Ink, F);
	AddBox(FVector(X1 - 40.0f, Y0 + 150.0f, Zf(F, 45.0f)), FVector(60.0f, 80.0f, 90.0f), Steel, 0.0f, true);

	for (float X : {400.0f, 1000.0f, 1600.0f})
	{
		AddBox(FVector(X, -560.0f, Zf(F, CeilB1 - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, -560.0f, Zf(F, CeilB1 - 30.0f)), 600.0f, 700.0f, Neutral, X == 1000.0f); // 16 below left the upper walls black; 800 blew the ceiling out (walk 13); cool tinted the linen lavender (walk 16)
	}
	// Small washes high on both long walls: the battens alone leave a black band under the ceiling.
	// 1.1 m off the walls: at 40 cm the ceiling between them and the battens was left a black band (walk 18).
	for (float X : {700.0f, 1300.0f}) for (float Y : {Y0 + 110.0f, Y1 - 110.0f}) AddLight(F, FVector(X, Y, Zf(F, CeilB1 - 25.0f)), 300.0f, 480.0f, Neutral);
	for (float X : {500.0f, 1500.0f}) SmokeDetector(F, FVector2D(X, -700.0f), CeilB1);
	FireExtinguisher(F, FVector(1800.0f, Face, 0.0f), -90.0f);
}

void AKcHotel::BuildLoadingDock()
{
	// Loading dock and receiving, east of the core: the receiving office, two roller shutters, the
	// alarmed dock door to the apron, the liquor cage and the night's deliveries (M009).
	const int32 F = -1;
	const float X0 = CoreX1, X1 = MaxX - 15.0f, Y0 = -HalfY + 15.0f, Y1 = -CorrY - Wall * 0.5f;
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));
	UMaterialInterface* Yellow = Mat(TEXT("MI_SafetyYellow"));
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FloorConcrete")));
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilB1, Mat(TEXT("MI_CeilingMid"))); // Soft glowed white over the room
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Paint, 0.0f, false);
	AddBox(FVector(X1 - 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilB1 * 0.5f)), FVector(1.0f, Y1 - Y0, CeilB1), Paint, 0.0f, false);
	// Walkway lines: a yellow-edged pedestrian route from the corridor doors to the office.
	for (float Y : {-260.0f, -340.0f}) AddBox(FVector(3300.0f, Y, Zf(F, 0.7f)), FVector(800.0f, 6.0f, 0.4f), Yellow, 0.0f, false);

	// Receiving office: a glazed booth in the south-west corner looking over the dock.
	const float OffX = 3150.0f, OffY = -650.0f;
	UMaterialInterface* Glass = Mat(TEXT("MI_Glass"));
	AddBox(FVector((X0 + OffX - 90.0f) * 0.5f, OffY, Zf(F, 45.0f)), FVector(OffX - 90.0f - X0, Wall, 90.0f), Paint);
	AddBox(FVector((X0 + OffX - 90.0f) * 0.5f, OffY, Zf(F, 90.0f + (CeilB1 - 90.0f) * 0.5f)), FVector(OffX - 90.0f - X0, 3.0f, CeilB1 - 90.0f), Glass);
	WallY(F, OffX, Y0, OffY + 4.0f, {}, Paint, 90.0f);
	AddBox(FVector(OffX, (Y0 + OffY) * 0.5f, Zf(F, 90.0f + (CeilB1 - 90.0f) * 0.5f)), FVector(3.0f, OffY - Y0, CeilB1 - 90.0f), Glass);
	DoorLeaf(F, FVector(OffX - 45.0f, OffY, 0.0f), 90.0f, 90.0f, TEXT("RECEIVING OFFICE"), false, true);
	AddBox(FVector(OffX - 45.0f, OffY, Zf(F, (DoorH + CeilB1) * 0.5f)), FVector(90.0f, Wall, CeilB1 - DoorH), Paint);
	Desk(FVector(2860.0f, Y0 + 45.0f, Zf(F, 0.0f)), 0.0f, FVector(220.0f, 80.0f, DeskH), Mat(TEXT("MI_WoodOak")));
	Monitor(FVector(2830.0f, Y0 + 30.0f, Zf(F, DeskH)), 90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
	TaskChair(FVector(2850.0f, Y0 + 130.0f, Zf(F, 0.0f)), -90.0f);
	AddBox(FVector(2920.0f, Y0 + 55.0f, Zf(F, DeskH + 1.0f)), FVector(23.0f, 30.0f, 1.2f), Mat(TEXT("MI_Paper")), -5.0f, false);
	AddBox(FVector(2950.0f, Y0 + 35.0f, Zf(F, DeskH + 8.0f)), FVector(16.0f, 12.0f, 16.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	Shelf(FVector(3080.0f, Y0 + 22.0f, Zf(F, 0.0f)), 90.0f, 100.0f, 41);
	// The dock bell's indoor chime over the office door: broken, with its work order taped on (M009).
	AddBox(FVector(OffX - 45.0f, OffY + 6.0f, Zf(F, 240.0f)), FVector(14.0f, 6.0f, 14.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	AddBox(FVector(OffX - 45.0f, OffY + 9.2f, Zf(F, 226.0f)), FVector(16.0f, 0.4f, 10.0f), Mat(TEXT("MI_Paper")), 4.0f, false);
	AddLabel(TEXT("OUT OF ORDER  WO 4471"), FVector(OffX - 45.0f, OffY + 9.6f, Zf(F, 226.0f)), 90.0f, 1.2f, FColor(170, 30, 30), F);

	// Two roller shutters on the south wall, closed, with their bollards and the dock leveller plates.
	for (int32 i = 0; i < 2; ++i)
	{
		const float Xc = 3400.0f + i * 450.0f;
		AddBox(FVector(Xc, Y0 + 4.0f, Zf(F, 125.0f)), FVector(320.0f, 6.0f, 250.0f), Steel, 0.0f, false);
		for (int32 r = 0; r < 12; ++r) AddBox(FVector(Xc, Y0 + 7.4f, Zf(F, 10.0f + r * 20.0f)), FVector(318.0f, 1.0f, 2.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(Xc, Y0 + 20.0f, Zf(F, 262.0f)), FVector(340.0f, 36.0f, 28.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		// The leveller: dark tread plate with a hazard edge and its hinge (chrome glared white like paper, walk 12).
		AddBox(FVector(Xc, Y0 + 60.0f, Zf(F, 1.0f)), FVector(260.0f, 100.0f, 2.0f), Mat(TEXT("MI_SteelDark")), 0.0f, false);
		for (int32 k = 0; k < 13; ++k) AddBox(FVector(Xc - 120.0f + k * 20.0f, Y0 + 106.0f, Zf(F, 2.1f)), FVector(10.0f, 8.0f, 0.4f), k % 2 ? Mat(TEXT("MI_DeviceBlack")) : Yellow, 0.0f, false);
		AddBox(FVector(Xc, Y0 + 12.0f, Zf(F, 2.4f)), FVector(260.0f, 4.0f, 1.2f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		for (float Side : {-1.0f, 1.0f}) AddInstance(Cylinder, FVector(Xc + Side * 175.0f, Y0 + 30.0f, Zf(F, 55.0f)), 0.0f, FVector(0.16f, 0.16f, 1.1f), Yellow, true);
		AddLabel(FString::Printf(TEXT("DOCK %d"), i + 1), FVector(Xc, Y0 + 39.0f, Zf(F, 262.0f)), 90.0f, 7.0f, FColor::White, F);
	}

	// The dock door: alarmed, push bar, a held-open alarm after 30 seconds; it opens out onto the apron.
	DynamicDoor(TEXT("dock"), F, FVector(DockDoorX, -HalfY, 0.0f), 90.0f, 100.0f, FString());
	const float DoorFace = -HalfY + 15.0f;
	for (float Side : {-1.0f, 1.0f}) AddBox(FVector(DockDoorX + Side * 53.0f, DoorFace + 1.0f, Zf(F, DoorH * 0.5f + 3.0f)), FVector(6.0f, 2.0f, DoorH + 6.0f), Steel, 0.0f, false);
	AddBox(FVector(DockDoorX, DoorFace + 1.0f, Zf(F, DoorH + 3.0f)), FVector(112.0f, 2.0f, 6.0f), Steel, 0.0f, false);
	AddBox(FVector(DockDoorX + 80.0f, DoorFace + 0.6f, Zf(F, 152.0f)), FVector(36.0f, 1.2f, 9.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("DOCK DOOR"), FVector(DockDoorX + 80.0f, DoorFace + 1.4f, Zf(F, 152.0f)), 90.0f, 3.2f, Ink, F);
	AddBox(FVector(DockDoorX, DoorFace + 4.0f, Zf(F, DoorH + 30.0f)), FVector(22.0f, 8.0f, 14.0f), Mat(TEXT("MI_FireRed")), 0.0f, false);
	AddBox(FVector(DockDoorX + 30.0f, DoorFace + 2.0f, Zf(F, DoorH + 6.0f)), FVector(10.0f, 4.0f, 4.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	AddBox(FVector(DockDoorX - 90.0f, DoorFace + 1.0f, Zf(F, 150.0f)), FVector(40.0f, 1.0f, 30.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("ALARMED DOOR"), FVector(DockDoorX - 90.0f, DoorFace + 1.7f, Zf(F, 158.0f)), 90.0f, 2.8f, FColor(170, 30, 30), F);
	AddLabel(TEXT("Held-open alarm after 30 s."), FVector(DockDoorX - 90.0f, DoorFace + 1.7f, Zf(F, 150.0f)), 90.0f, 1.6f, Ink, F);
	AddLabel(TEXT("Never prop this door."), FVector(DockDoorX - 90.0f, DoorFace + 1.7f, Zf(F, 144.0f)), 90.0f, 1.6f, Ink, F);
	if (AKcCasePoint* P = SpawnCasePoint(TEXT("loading_dock"), TEXT("dock_door"), FString(), FVector(DockDoorX, -HalfY + 8.0f, Zf(F, 100.0f)), 90.0f, Cube,
			FVector(0.05f, 0.8f, 0.06f), Steel, EKcUseStyle::Screen, FVector(DockDoorX, -HalfY + 20.0f, Zf(F, 130.0f))))
	{
		if (const FDynDoor* Dyn = Doors.Find(TEXT("dock")))
		{
			if (Dyn->Pivot.IsValid()) P->AttachToComponent(Dyn->Pivot.Get(), FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	ExitSign(F, FVector(DockDoorX, DoorFace + 2.0f, Zf(F, DoorH + 52.0f)), 90.0f);

	// The covered dock apron under the plaza: asphalt, retaining walls, a sodium lamp.
	AddBox(FVector(4200.0f, -1300.0f, Zf(F, -10.0f)), FVector(1200.0f, 570.0f, 20.0f), Mat(TEXT("MI_Asphalt")));
	AddBox(FVector(4200.0f, -1600.0f, Zf(F, 200.0f)), FVector(1200.0f, 30.0f, 400.0f), Mat(TEXT("MI_Concrete")));
	for (float X : {3600.0f, 4800.0f}) AddBox(FVector(X, -1300.0f, Zf(F, 200.0f)), FVector(30.0f, 600.0f, 400.0f), Mat(TEXT("MI_Concrete")));
	for (float X : {3800.0f, 4100.0f, 4400.0f}) AddBox(FVector(X, -1250.0f, Zf(F, 0.6f)), FVector(10.0f, 400.0f, 0.4f), Yellow, 0.0f, false);
	AddBox(FVector(4250.0f, -1580.0f, Zf(F, 300.0f)), FVector(40.0f, 12.0f, 14.0f), Mat(TEXT("MI_LampWarm")), 0.0f, false);
	AddLight(F, FVector(4250.0f, -1560.0f, Zf(F, 290.0f)), 1200.0f, 900.0f, FLinearColor(1.0f, 0.62f, 0.3f));
	AddBox(FVector(DockDoorX + 80.0f, -HalfY - 17.0f, Zf(F, 130.0f)), FVector(10.0f, 4.0f, 12.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	AddLabel(TEXT("DELIVERIES: RING"), FVector(DockDoorX + 80.0f, -HalfY - 15.4f, Zf(F, 146.0f)), -90.0f, 2.4f, Cream, F); // on the wall above the push

	// Liquor cage in the south-east corner: steel bars, a gate with its padlock, the evening's cases.
	const float CageX0 = 4380.0f, CageY1 = -720.0f;
	for (float X = CageX0; X <= X1 - 4.0f; X += 12.0f) AddInstance(Cylinder, FVector(X, CageY1, Zf(F, 120.0f)), 0.0f, FVector(0.015f, 0.015f, 2.4f), Steel, false);
	for (float Y = Y0 + 4.0f; Y <= CageY1; Y += 12.0f) AddInstance(Cylinder, FVector(CageX0, Y, Zf(F, 120.0f)), 0.0f, FVector(0.015f, 0.015f, 2.4f), Steel, false);
	for (float Z : {2.0f, 120.0f, 238.0f})
	{
		AddBox(FVector((CageX0 + X1) * 0.5f, CageY1, Zf(F, Z)), FVector(X1 - CageX0, 4.0f, 4.0f), Steel, 0.0f, false);
		AddBox(FVector(CageX0, (Y0 + CageY1) * 0.5f, Zf(F, Z)), FVector(4.0f, CageY1 - Y0, 4.0f), Steel, 0.0f, false);
	}
	AddCollider(FVector((CageX0 + X1) * 0.5f, CageY1, Zf(F, 120.0f)), FVector(X1 - CageX0, 4.0f, 240.0f), FRotator::ZeroRotator);
	AddCollider(FVector(CageX0, (Y0 + CageY1) * 0.5f, Zf(F, 120.0f)), FVector(4.0f, CageY1 - Y0, 240.0f), FRotator::ZeroRotator);
	for (int32 Stack = 0; Stack < 4; ++Stack)
	{
		for (int32 h = 0; h < 4 - (Stack == 1); ++h)
		{
			AddBox(FVector(CageX0 + 70.0f + Stack * 90.0f, Y0 + 60.0f, Zf(F, 16.0f + h * 30.0f)), FVector(40.0f, 30.0f, 30.0f), Mat(TEXT("MI_Cardboard")), (h % 2) * 4.0f, false);
		}
	}
	SpawnCasePoint(TEXT("loading_dock"), FString(), TEXT("Liquor cage"), FVector(4560.0f, CageY1 + 4.0f, Zf(F, 105.0f)), 90.0f, Cube, FVector(0.04f, 0.06f, 0.08f),
		Mat(TEXT("MI_Brass")), EKcUseStyle::Screen, FVector(4560.0f, CageY1 - 80.0f, Zf(F, 80.0f)));
	AddBox(FVector(4560.0f, CageY1 + 1.0f, Zf(F, 200.0f)), FVector(80.0f, 1.0f, 20.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("LIQUOR CAGE: LOCK IT EVERY TIME"), FVector(4560.0f, CageY1 + 1.7f, Zf(F, 200.0f)), 90.0f, 2.0f, Ink, F);

	// Tonight's deliveries on pallets by Dock 1, the pallet jack beside them.
	for (int32 p = 0; p < 2; ++p)
	{
		const FVector Pal(3300.0f + p * 170.0f, -560.0f, Zf(F, 0.0f));
		AddBox(Pal + FVector(0, 0, 7.0f), FVector(120.0f, 100.0f, 14.0f), Mat(TEXT("MI_WoodOak")), 0.0f, true);
		for (int32 k = 0; k < 6; ++k)
		{
			AddBox(Pal + FVector(-30.0f + (k % 2) * 60.0f, -25.0f + ((k / 2) % 2) * 50.0f, 34.0f + (k / 4) * 40.0f), FVector(56.0f, 46.0f, 40.0f), Mat(TEXT("MI_Cardboard")), 0.0f, false);
		}
	}
	// (The pallet jack is a scene prop: parked here, or wedged in the dock door in M009.)

	for (float X : {3000.0f, 3600.0f, 4200.0f})
	{
		AddBox(FVector(X, -560.0f, Zf(F, CeilB1 - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, -560.0f, Zf(F, CeilB1 - 30.0f)), 900.0f, 750.0f, Cool, X == 3600.0f);
	}
	// Upper-wall fill, 1.1 m off the walls: at 40 cm each left a blown disc high on the shutter wall (walk 18).
	for (float X : {3300.0f, 3900.0f, 4500.0f}) for (float Y : {Y0 + 110.0f, Y1 - 110.0f}) AddLight(F, FVector(X, Y, Zf(F, CeilB1 - 25.0f)), 300.0f, 480.0f, Cool);
	for (float X : {3300.0f, 4200.0f}) SmokeDetector(F, FVector2D(X, -450.0f), CeilB1);
	CctvDome(F, FVector(4100.0f, -760.0f, Zf(F, CeilB1)), TEXT("B1-05"));
	CctvDome(F, FVector(4300.0f, -400.0f, Zf(F, CeilB1)), TEXT("B1-06"));
	FireExtinguisher(F, FVector(X0 + 1.0f, -400.0f, 0.0f), 0.0f);
}

void AKcHotel::BuildLuggageRoom()
{
	// Behind the bell stand: racks of tagged bags, the window filmed for privacy, the door on a
	// closer (open while the case is at the bell stand, M010).
	const int32 F = 0;
	const float X0 = 15.0f, X1 = LuggageX1 - Wall * 0.5f, Y0 = LuggageY0 + Wall * 0.5f, Y1 = HalfY - 15.0f;
	UMaterialInterface* Plaster = Mat(TEXT("MI_WallPlaster"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));
	WallX(F, LuggageY0, 15.0f, LuggageX1, {FVector2D(LuggageDoorX, 100.0f)}, Plaster, CeilLobby);
	WallY(F, LuggageX1, LuggageY0, HalfY - 15.0f, {}, Plaster, CeilLobby);
	DynamicDoor(TEXT("luggage"), F, FVector(LuggageDoorX, LuggageY0, 0.0f), -90.0f, 100.0f, TEXT("LUGGAGE"));
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FloorVinyl")), 0.8f);
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), 260.0f, Mat(TEXT("MI_CeilingSoft")));
	AddBox(FVector((X0 + X1) * 0.5f, Y1 - 2.0f, Zf(F, 165.0f)), FVector(X1 - X0, 1.0f, 330.0f), Mat(TEXT("MI_Sheer")), 0.0f, false);
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, 130.0f)), FVector(1.0f, Y1 - Y0, 260.0f), Plaster, 0.0f, false);
	// Two-tier racks along the window and the west wall.
	struct FRack { FVector Center; float Yaw; float Length; };
	const FRack Racks[] = {{FVector((X0 + X1) * 0.5f + 30.0f, Y1 - 32.0f, 0.0f), 0.0f, X1 - X0 - 80.0f}, {FVector(X0 + 32.0f, (Y0 + Y1) * 0.5f - 20.0f, 0.0f), 90.0f, Y1 - Y0 - 110.0f}};
	FRandomStream R(4471);
	static const TCHAR* const BagMats[] = {TEXT("MI_FabricDark"), TEXT("MI_Leather"), TEXT("MI_SuitNavy"), TEXT("MI_PlasticGrey"), TEXT("MI_FabricAccent")};
	int32 Tag = 4460;
	for (const FRack& Rk : Racks)
	{
		const FRotator Rot(0.0f, Rk.Yaw, 0.0f);
		auto At = [&](float U, float V, float Z) { return FVector(Rk.Center.X, Rk.Center.Y, Zf(F, Z)) + Rot.RotateVector(FVector(U, V, 0.0f)); };
		for (float Z : {8.0f, 92.0f}) AddBox(At(0, 0, Z), FVector(Rk.Length, 56.0f, 3.0f), Steel, Rk.Yaw, Z < 50.0f);
		for (float Side : {-1.0f, 1.0f}) AddBox(At(Side * Rk.Length * 0.5f, 0, 90.0f), FVector(3.0f, 56.0f, 180.0f), Steel, Rk.Yaw);
		for (float Tier : {9.5f, 93.5f})
		{
			float U = -Rk.Length * 0.5f + 8.0f;
			while (U < Rk.Length * 0.5f - 30.0f)
			{
				const float W = R.FRandRange(26.0f, 46.0f);
				const float H = R.FRandRange(40.0f, 66.0f);
				// One gap on the window rack: where 4471 stood (M010).
				if (Tag == 4471)
				{
					++Tag;
					U += 42.0f;
					continue;
				}
				AddBox(At(U + W * 0.5f, 0, Tier + H * 0.5f), FVector(W, R.FRandRange(34.0f, 50.0f), H), Mat(BagMats[R.RandRange(0, 4)]), Rk.Yaw, false);
				AddBox(At(U + W * 0.5f, -26.0f, Tier + H - 8.0f), FVector(5.0f, 0.4f, 8.0f), Mat(TEXT("MI_Paper")), Rk.Yaw, false);
				U += W + R.FRandRange(4.0f, 10.0f);
				++Tag;
			}
		}
	}
	SpawnCasePoint(TEXT("bell_stand"), TEXT("luggage_room"), FString(), FVector((X0 + X1) * 0.5f + 30.0f, Y1 - 60.0f, Zf(F, 96.0f)), 0.0f, Cube, FVector(0.6f, 0.04f, 0.03f),
		Steel, EKcUseStyle::Screen, FVector((X0 + X1) * 0.5f, Y1 - 40.0f, Zf(F, 90.0f)));
	// On the lobby face of the store, right of the door plate: the key on its hook, then the store rules framed at eye
	// level (a bare sheet under the plate read as a pale rectangle, and two brass stubs as nothing, walks 16-17).
	const float StoreFace = LuggageY0 - Wall * 0.5f;
	{
		// The key, on its hook by the stand instead of signed out (M010): a brass hook plate and pin, the split ring,
		// the key and a leather fob with its paper tag, all hanging flat to the wall.
		const FVector Hook(LuggageDoorX + 112.0f, StoreFace, Zf(F, 150.0f));
		const FRotator AlongY(0.0f, 0.0f, 90.0f);
		AddBox(Hook + FVector(0.0f, -0.4f, 0.0f), FVector(6.0f, 0.8f, 6.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
		AddInstance(Cylinder, FTransform(AlongY, Hook + FVector(0.0f, -2.3f, 1.5f), FVector(0.008f, 0.008f, 0.03f)), Mat(TEXT("MI_Brass")), false);
		AddInstance(Cylinder, FTransform(AlongY, Hook + FVector(0.0f, -3.0f, 0.2f), FVector(0.028f, 0.028f, 0.003f)), Mat(TEXT("MI_Steel")), false);
		AddInstance(Cylinder, FTransform(AlongY, Hook + FVector(-0.8f, -2.9f, -2.2f), FVector(0.022f, 0.022f, 0.0025f)), Mat(TEXT("MI_Steel")), false);
		AddBox(Hook + FVector(-0.8f, -2.9f, -5.4f), FVector(0.9f, 0.25f, 4.4f), Mat(TEXT("MI_Steel")), 0.0f, false);
		AddBox(Hook + FVector(2.2f, -3.0f, -4.6f), FVector(3.2f, 0.8f, 7.0f), Mat(TEXT("MI_Leather")), 0.0f, false);
		AddBox(Hook + FVector(2.2f, -3.5f, -5.6f), FVector(2.4f, 0.2f, 2.8f), Mat(TEXT("MI_Paper")), 0.0f, false);
	}
	{
		const float NX = LuggageDoorX + 160.0f;
		AddBox(FVector(NX, StoreFace - 0.4f, Zf(F, 150.0f)), FVector(40.0f, 0.8f, 34.0f), Mat(TEXT("MI_WoodWalnut")), 0.0f, false);
		AddBox(FVector(NX, StoreFace - 1.0f, Zf(F, 150.0f)), FVector(36.0f, 0.4f, 30.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
		AddLabel(TEXT("LUGGAGE STORE"), FVector(NX, StoreFace - 1.5f, Zf(F, 158.5f)), -90.0f, 2.4f, Ink, F);
		AddBox(FVector(NX, StoreFace - 1.3f, Zf(F, 155.2f)), FVector(26.0f, 0.2f, 0.25f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddLabel(TEXT("Release against the ticket only."), FVector(NX, StoreFace - 1.5f, Zf(F, 150.8f)), -90.0f, 1.5f, Ink, F);
		AddLabel(TEXT("No ticket: photo ID and a description."), FVector(NX, StoreFace - 1.5f, Zf(F, 146.0f)), -90.0f, 1.5f, Ink, F);
		AddLabel(TEXT("Key: signed out to the bell captain."), FVector(NX, StoreFace - 1.5f, Zf(F, 141.2f)), -90.0f, 1.5f, Ink, F);
	}
	CeilingLight(F, FVector((X0 + X1) * 0.5f, (Y0 + Y1) * 0.5f, Zf(F, 260.0f)), 500.0f, Neutral, 400.0f);
	SmokeDetector(F, FVector2D((X0 + X1) * 0.5f - 100.0f, (Y0 + Y1) * 0.5f), 260.0f);
}

// ---------------------------------------------------------------------------- G: lobby and staff corridor

void AKcHotel::BuildGround()
{
	const int32 F = 0;
	UMaterialInterface* Plaster = Mat(TEXT("MI_WallPlaster"));
	// Lobby: marble, a high ceiling with a cove of warm light, the lift lobby to the east.
	FloorFinish(F, FBox2D(FVector2D(15.0f, -450.0f), FVector2D(CoreX0, HalfY - 15.0f)), Mat(TEXT("MI_FloorMarble")));
	FloorFinish(F, FBox2D(FVector2D(CoreX0, -CorrY), FVector2D(CoreX1, CorrY)), Mat(TEXT("MI_FloorMarble")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -HalfY), FVector2D(CoreX0, HalfY)), CeilLobby, Mat(TEXT("MI_CeilingWhite")));
	AddBox(FVector(1050.0f, 60.0f, Zf(F, CeilLobby - 22.0f)), FVector(1300.0f, 900.0f, 4.0f), Mat(TEXT("MI_CeilingWhite")), 0.0f, false);
	// Cove: LED strips on top of the island's rim wash the ceiling above it; from the floor only the glow shows.
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(FVector(1050.0f, 60.0f + Side * 440.0f, Zf(F, CeilLobby - 19.5f)), FVector(1280.0f, 4.0f, 1.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddBox(FVector(1050.0f + Side * 640.0f, 60.0f, Zf(F, CeilLobby - 19.5f)), FVector(4.0f, 880.0f, 1.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
	}
	// Skins on the core and stair faces, and the step down from the lobby ceiling to the lift lobby.
	AddBox(FVector(CoreX0 - 0.5f, (CorrY + HalfY) * 0.5f, Zf(F, CeilLobby * 0.5f)), FVector(1.0f, HalfY - CorrY, CeilLobby), Plaster, 0.0f, false);
	AddBox(FVector(CoreX0 - 0.5f, (-HalfY - CorrY) * 0.5f, Zf(F, CeilLobby * 0.5f)), FVector(1.0f, HalfY - CorrY, CeilLobby), Plaster, 0.0f, false);
	AddBox(FVector(CoreX0 + 2.0f, 0.0f, Zf(F, 340.0f)), FVector(4.0f, 2.0f * CorrY, 80.0f), Plaster, 0.0f, false);
	// Street doors: a vestibule with two pairs of glass sliders, both locked overnight.
	for (float Y : {-130.0f, 130.0f}) AddBox(FVector(150.0f, Y, Zf(F, 165.0f)), FVector(300.0f, 3.0f, 330.0f), Mat(TEXT("MI_Glass")));
	AddBox(FVector(150.0f, 0.0f, Zf(F, 300.0f)), FVector(300.0f, 260.0f, 4.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(FVector(0.0f, Side * 60.0f, Zf(F, 125.0f)), FVector(3.0f, 118.0f, 250.0f), Mat(TEXT("MI_Glass")));
		SpawnSign(TEXT("lobby"), TEXT("Street doors"), TEXT("Street doors: locked from 01:00 to 05:30. Arrivals ring the night bell, and the desk checks them on camera L-01 before it releases the door."),
			FVector(300.0f, Side * 60.0f, Zf(F, 125.0f)), 0.0f, FVector(3.0f, 118.0f, 250.0f), Mat(TEXT("MI_Glass")));
	}
	// Transoms over both pairs (the slider gear is housed in them).
	AddBox(FVector(0.0f, 0.0f, Zf(F, 290.0f)), FVector(14.0f, 260.0f, 80.0f), Mat(TEXT("MI_Graphite")));
	AddBox(FVector(300.0f, 0.0f, Zf(F, 275.0f)), FVector(14.0f, 260.0f, 50.0f), Mat(TEXT("MI_Graphite")));
	// The name in brass on the inner transom's lobby face (bare, it read as a blank black sign, walks 14-17; at 12 cm the
	// letters smeared and the sub-line was lost, walk 18).
	AddLabel(TEXT("VELDANE GRAND"), FVector(307.5f, 0.0f, Zf(F, 283.0f)), 0.0f, 15.0f, BrassText, F);
	AddLabel(TEXT("HARBOURFRONT"), FVector(307.5f, 0.0f, Zf(F, 265.0f)), 0.0f, 6.5f, BrassText, F);
	// Every leaf in a slim graphite frame (stiles, a head rail and a deep bottom rail) with a frosted band at eye level,
	// so the sliders read as doors, not open gaps; jambs close the inner pair to the vestibule glass.
	for (float X : {0.0f, 300.0f}) for (float Side : {-1.0f, 1.0f})
	{
		for (float Y : {3.0f, 117.0f}) AddBox(FVector(X, Side * Y, Zf(F, 125.0f)), FVector(5.0f, 4.0f, 250.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(X, Side * 60.0f, Zf(F, 247.5f)), FVector(5.0f, 110.0f, 5.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(X, Side * 60.0f, Zf(F, 6.0f)), FVector(5.0f, 110.0f, 12.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(X, Side * 60.0f, Zf(F, 145.0f)), FVector(3.4f, 110.0f, 18.0f), Mat(TEXT("MI_Sheer")), 0.0f, false); // 6 cm read as a rail (walk 18)
	}
	for (float Side : {-1.0f, 1.0f}) AddBox(FVector(300.0f, Side * 125.5f, Zf(F, 125.0f)), FVector(14.0f, 13.0f, 250.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(150.0f, 0.0f, Zf(F, 0.8f)), FVector(260.0f, 200.0f, 1.6f), Mat(TEXT("MI_Rug")), 0.0f, false);
	// Jambs at both sides of the outer pair, and the night bell push outside on one (inside, it floated in the glass, walk 14).
	for (float Side : {-1.0f, 1.0f}) AddBox(FVector(0.0f, Side * 122.0f, Zf(F, 125.0f)), FVector(14.0f, 6.0f, 250.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(-7.6f, 122.0f, Zf(F, 135.0f)), FVector(1.2f, 4.0f, 7.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
	AddLabel(TEXT("NIGHT BELL"), FVector(-2.0f, 160.0f, Zf(F, 150.0f)), 180.0f, 2.4f, BrassText, F);
	CctvDome(F, FVector(200.0f, 0.0f, Zf(F, 300.0f)), TEXT("L-01"));
	// Rain walked in past the mat: the wet patch where Mrs Ashcombe fell (M014).
	WetFloorPoint = SpawnCasePoint(TEXT("lobby"), TEXT("wet_floor"), FString(), FVector(420.0f, -45.0f, Zf(F, 0.25f)), 20.0f, Cylinder, FVector(1.1f, 0.8f, 0.004f),
		Mat(TEXT("MI_Water")), EKcUseStyle::Screen, FVector(420.0f, -45.0f, Zf(F, 60.0f)));
	// Round table with a plant between the doors and the lounge (sphere bouquet if the model is missing).
	AddInstance(Cylinder, FVector(620.0f, 0.0f, Zf(F, 38.0f)), 0.0f, FVector(1.4f, 1.4f, 0.04f), Mat(TEXT("MI_WoodWalnut")), true);
	AddInstance(Cylinder, FVector(620.0f, 0.0f, Zf(F, 18.0f)), 0.0f, FVector(0.4f, 0.4f, 0.36f), Mat(TEXT("MI_WoodWalnut")), true);
	if (!AddInstance(Model(TEXT("potted_plant_02")), FVector(620.0f, 0.0f, Zf(F, 40.0f)), 25.0f, FVector(0.8f)))
	{
		AddInstance(Cylinder, FVector(620.0f, 0.0f, Zf(F, 62.0f)), 0.0f, FVector(0.3f, 0.3f, 0.45f), Mat(TEXT("MI_Ceramic")), false);
		FRandomStream R(1406);
		for (int32 i = 0; i < 14; ++i)
		{
			const FVector P(620.0f + R.FRandRange(-26.0f, 26.0f), R.FRandRange(-26.0f, 26.0f), Zf(F, R.FRandRange(92.0f, 120.0f)));
			AddInstance(Sphere, P, 0.0f, FVector(R.FRandRange(0.14f, 0.22f)), Mat(i % 3 ? TEXT("MI_Leaf") : TEXT("MI_Blossom")), false);
		}
	}
	// Lounge: two sofas face to face, armchairs at the ends, a rug, lamps and plants.
	AddBox(FVector(1050.0f, 60.0f, Zf(F, 1.2f)), FVector(560.0f, 380.0f, 1.2f), Mat(TEXT("MI_Rug")), 0.0f, false);
	Sofa(FVector(1050.0f, -110.0f, Zf(F, 0.0f)), 90.0f);
	Sofa(FVector(1050.0f, 230.0f, Zf(F, 0.0f)), -90.0f);
	Chair(FVector(770.0f, 60.0f, Zf(F, 0.0f)), 0.0f);
	Chair(FVector(1330.0f, 60.0f, Zf(F, 0.0f)), 180.0f);
	if (!AddInstance(Model(TEXT("modern_coffee_table_01")), FVector(1050.0f, 60.0f, Zf(F, 0.0f)), ModelYawOffset, FVector(1.0f)))
	{
		AddBox(FVector(1050.0f, 60.0f, Zf(F, 38.0f)), FVector(130.0f, 70.0f, 6.0f), Mat(TEXT("MI_StoneCounter")));
		AddBox(FVector(1050.0f, 60.0f, Zf(F, 17.0f)), FVector(110.0f, 50.0f, 34.0f), Mat(TEXT("MI_Graphite")));
	}
	AddBox(FVector(1030.0f, 50.0f, Zf(F, 43.0f)), FVector(30.0f, 24.0f, 3.0f), Mat(TEXT("MI_Paper")), 12.0f, false);
	for (const FVector2D& P : {FVector2D(800.0f, -120.0f), FVector2D(1300.0f, 240.0f)}) Lamp(F, FVector(P.X, P.Y, Zf(F, 0.0f)), true);
	for (const FVector2D& P : {FVector2D(360.0f, -300.0f), FVector2D(360.0f, 300.0f), FVector2D(1960.0f, -330.0f), FVector2D(1960.0f, 330.0f)}) Plant(FVector(P.X, P.Y, Zf(F, 0.0f)), true);
	// The chandelier over the lounge, hung from the ceiling: its top against a brass ceiling rose, measured from the mesh's
	// own bounds (at a fixed 1.2 m down it hung low over the lounge, owner's playthrough).
	UStaticMesh* Chandelier = Model(TEXT("Chandelier_03"));
	const float ChandelierScale = 1.2f;
	const float ChandelierTop = Chandelier ? (Chandelier->GetBounds().Origin.Z + Chandelier->GetBounds().BoxExtent.Z) * ChandelierScale : 0.0f;
	const float RaftUnder = CeilLobby - 24.0f; // the underside of the lounge's ceiling raft
	if (Chandelier) AddInstance(Cylinder, FVector(1050.0f, 60.0f, Zf(F, RaftUnder - 1.5f)), 0.0f, FVector(0.22f, 0.22f, 0.03f), Mat(TEXT("MI_Brass")), false);
	if (!AddInstance(Chandelier, FVector(1050.0f, 60.0f, Zf(F, RaftUnder - 3.0f) - ChandelierTop), 0.0f, FVector(ChandelierScale), nullptr, false))
	{
		for (int32 i = 0; i < 7; ++i)
		{
			const FVector P(1050.0f + 70.0f * FMath::Cos(i * 0.9f), 60.0f + 70.0f * FMath::Sin(i * 0.9f), Zf(F, CeilLobby - 90.0f - (i % 3) * 25.0f));
			AddInstance(Cylinder, P + FVector(0, 0, (CeilLobby - P.Z + Zf(F, 0.0f)) * 0.5f), 0.0f, FVector(0.006f, 0.006f, (Zf(F, CeilLobby) - P.Z) / 100.0f), Mat(TEXT("MI_Brass")), false);
			AddInstance(Sphere, P, 0.0f, FVector(0.18f), Mat(TEXT("MI_LampWarm")), false);
		}
	}
	// No shadows: from inside the fixture its crown and arms threw large black shapes onto the ceiling.
	AddLight(F, FVector(1050.0f, 60.0f, Chandelier ? Zf(F, RaftUnder - 3.0f) - ChandelierTop * 0.5f : Zf(F, CeilLobby - 140.0f)), 2600.0f, 900.0f, Warm, false); // inside the fixture
	for (const FVector2D& P : {FVector2D(400.0f, -250.0f), FVector2D(400.0f, 250.0f), FVector2D(1700.0f, -250.0f), FVector2D(1700.0f, 250.0f),
			FVector2D(1100.0f, 450.0f), FVector2D(1850.0f, 0.0f)})
	{
		CeilingLight(F, FVector(P.X, P.Y, Zf(F, CeilLobby)), DownLumens * 1.2f, Warm, 750.0f);
	}
	for (float Y : {-700.0f, 700.0f}) Sconce(F, FVector(CoreX0 - 1.0f, Y, Zf(F, 200.0f)), 180.0f);
	ArtPrint(FVector(CoreX0 - 1.0f, 600.0f, Zf(F, 190.0f)), 180.0f, TEXT("MI_ArtHarbourA"), FVector2D(160.0f, 110.0f));
	for (float X : {700.0f, 1400.0f}) SmokeDetector(F, FVector2D(X, -200.0f), CeilLobby);
	// The lobby AED on the core wall by the lifts, a green sign above it (M005).
	const FVector Aed(CoreX0 - 7.0f, -280.0f, Zf(F, 125.0f));
	SpawnCasePoint(TEXT("lobby"), TEXT("aed_cabinet"), FString(), Aed, 180.0f, Cube, FVector(0.12f, 0.42f, 0.5f), Mat(TEXT("MI_Paper")), EKcUseStyle::Screen);
	AddBox(Aed - FVector(6.2f, 0, -4.0f), FVector(0.4f, 34.0f, 30.0f), Mat(TEXT("MI_Glass")), 0.0f, false);
	AddBox(Aed - FVector(5.0f, 0, 6.0f), FVector(8.0f, 24.0f, 20.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, false);
	AddBox(FVector(CoreX0 - 3.0f, -280.0f, Zf(F, 215.0f)), FVector(4.0f, 40.0f, 22.0f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
	AddLabel(TEXT("AED"), FVector(CoreX0 - 5.4f, -280.0f, Zf(F, 215.0f)), 180.0f, 9.0f, FColor::White, F);
	// The holdall by the sofa: only there in the case that has it (M006).
	BagPoint = SpawnCasePoint(TEXT("lobby"), TEXT("unattended_bag"), FString(), FVector(1250.0f, -175.0f, Zf(F, 18.0f)), 14.0f, Cube, FVector(0.55f, 0.28f, 0.34f),
		Mat(TEXT("MI_FabricDark")), EKcUseStyle::Screen, FVector(1250.0f, -175.0f, Zf(F, 30.0f)));
	// Bell stand, the brass luggage cart, and the luggage room behind its partition.
	// The stand: a dark plinth, the walnut body, a brass band under the stone top and a service bell (a box with a slab
	// on it read as a placeholder, walk 14).
	AddBox(FVector(320.0f, 520.0f, Zf(F, 4.0f)), FVector(64.0f, 50.0f, 8.0f), Mat(TEXT("MI_Skirting")), -40.0f);
	AddBox(FVector(320.0f, 520.0f, Zf(F, 54.0f)), FVector(56.0f, 42.0f, 92.0f), Mat(TEXT("MI_WoodWalnut")), -40.0f);
	AddBox(FVector(320.0f, 520.0f, Zf(F, 101.0f)), FVector(58.0f, 44.0f, 2.0f), Mat(TEXT("MI_Brass")), -40.0f, false);
	{
		const FVector Bell = FVector(320.0f, 520.0f, Zf(F, 108.0f)) + FRotator(0.0f, -40.0f, 0.0f).RotateVector(FVector(-18.0f, 14.0f, 0.0f));
		AddInstance(Cylinder, Bell + FVector(0, 0, 0.5f), 0.0f, FVector(0.08f, 0.08f, 0.01f), Mat(TEXT("MI_DeviceBlack")), false);
		AddInstance(Sphere, Bell + FVector(0, 0, 2.5f), 0.0f, FVector(0.07f, 0.07f, 0.05f), Mat(TEXT("MI_Brass")), false);
		AddInstance(Cylinder, Bell + FVector(0, 0, 5.0f), 0.0f, FVector(0.008f, 0.008f, 0.02f), Mat(TEXT("MI_Brass")), false);
	}
	AddBox(FVector(320.0f, 520.0f, Zf(F, 106.0f)), FVector(66.0f, 52.0f, 4.0f), Mat(TEXT("MI_StoneCounter")), -40.0f, false);
	AddBox(FVector(150.0f, 640.0f, Zf(F, 12.0f)), FVector(110.0f, 60.0f, 6.0f), Mat(TEXT("MI_Rug")), 0.0f, true);
	AddBox(FVector(150.0f, 640.0f, Zf(F, 15.6f)), FVector(114.0f, 64.0f, 1.2f), Mat(TEXT("MI_Brass")), 0.0f, false); // brass rim and castors
	AddBox(FVector(150.0f, 640.0f, Zf(F, 15.7f)), FVector(108.0f, 58.0f, 1.2f), Mat(TEXT("MI_Rug")), 0.0f, false);
	for (float X : {100.0f, 200.0f}) for (float Y : {616.0f, 664.0f})
	{
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(X, Y, Zf(F, 5.0f)), FVector(0.09f, 0.09f, 0.03f)), Mat(TEXT("MI_DeviceBlack")), false, false);
	}
	// Four brass uprights and a top frame with the hanging rail (two posts and a bar read as a goalpost, walk 16).
	for (float X : {98.0f, 202.0f}) for (float Y : {614.0f, 666.0f})
	{
		AddInstance(Cylinder, FVector(X, Y, Zf(F, 100.0f)), 0.0f, FVector(0.03f, 0.03f, 1.8f), Mat(TEXT("MI_Brass")), false);
	}
	for (float Y : {614.0f, 640.0f, 666.0f}) AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), FVector(150.0f, Y, Zf(F, 190.0f)), FVector(0.03f, 0.03f, 1.06f)), Mat(TEXT("MI_Brass")), false, false);
	for (float X : {98.0f, 202.0f}) AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(X, 640.0f, Zf(F, 190.0f)), FVector(0.03f, 0.03f, 0.54f)), Mat(TEXT("MI_Brass")), false, false);
	AddBox(FVector(140.0f, 640.0f, Zf(F, 40.0f)), FVector(70.0f, 45.0f, 50.0f), Mat(TEXT("MI_FabricDark")), 4.0f, false);
	AddBox(FVector(165.0f, 630.0f, Zf(F, 80.0f)), FVector(45.0f, 30.0f, 30.0f), Mat(TEXT("MI_Leather")), -6.0f, false);
	// The tag book on the stand (M006, M010), the stand's camera, and the luggage room.
	SpawnCasePoint(TEXT("bell_stand"), TEXT("luggage_log"), FString(), FVector(318.0f, 522.0f, Zf(F, 109.5f)), -40.0f, Cube, FVector(0.22f, 0.3f, 0.03f),
		Mat(TEXT("MI_SuitNavy")), EKcUseStyle::PickUp);
	AddBox(FVector(340.0f, 500.0f, Zf(F, 109.0f)), FVector(8.0f, 12.0f, 2.0f), Mat(TEXT("MI_Paper")), -40.0f, false);
	CctvDome(F, FVector(420.0f, 600.0f, Zf(F, CeilLobby)), TEXT("L-03"));
	BuildLuggageRoom();
	BuildFrontDesk();
	BuildLobbyBar();

	// Staff corridor east of the core.
	const float StaffCeil = CeilBack;
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	FloorFinish(F, FBox2D(FVector2D(CoreX1, -CorrY), FVector2D(MaxX - 15.0f, CorrY)), Mat(TEXT("MI_FloorVinyl")));
	Ceiling(F, FBox2D(FVector2D(CoreX1, -CorrY), FVector2D(MaxX, CorrY)), StaffCeil, Mat(TEXT("MI_CeilingSoft")));
	WallY(F, CoreX1 + 8.0f, -CorrY, CorrY, {FVector2D(0.0f, 100.0f)}, Plaster, 300.0f);
	DoorLeaf(F, FVector(CoreX1 + 8.0f, 0.0f, 0.0f), 180.0f, 100.0f, TEXT("STAFF ONLY"), false, true);
	WallX(F, CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3100.0f, 100.0f), FVector2D(4100.0f, 100.0f)}, Paint, StaffCeil);
	WallX(F, -CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3300.0f, 160.0f)}, Paint, StaffCeil);
	WallY(F, 3500.0f, CorrY, HalfY - 15.0f, {}, Paint, StaffCeil);
	DoorLeaf(F, FVector(3100.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("FIRE COMMAND"), false, true);
	DoorLeaf(F, FVector(4100.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("STAFF CANTEEN"), false, false, TEXT("Staff canteen"),
		TEXT("Staff canteen: dark until 05:30, when the breakfast crew arrives."), FString());
	for (float Side : {-1.0f, 1.0f})
	{
		// Held open on their closers like the B1 pairs: casework (the cook line, the pass, the permit board) is inside.
		DoorLeaf(F, FVector(3300.0f + Side * 40.0f, -CorrY, 0.0f), 90.0f, 80.0f, Side < 0.0f ? TEXT("MAIN KITCHEN") : FString(), false, true, FString(), FString(), FString(),
			Side < 0.0f, true);
	}
	DoorLeaf(F, FVector(MaxX - 17.0f, 0.0f, 0.0f), 180.0f, 100.0f, TEXT("STAFF ENTRANCE"), false, false, TEXT("Staff entrance"),
		TEXT("Staff entrance: everyone badges in and out. Bag checks on the way out follow the posted policy: with a witness, by consent, and never a search of the person."), FString());
	ExitSign(F, FVector(MaxX - 19.0f, 0.0f, Zf(F, DoorH + 22.0f)), 180.0f);
	for (float X = 3000.0f; X < MaxX; X += 600.0f)
	{
		AddBox(FVector(X, 0.0f, Zf(F, StaffCeil - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, 0.0f, Zf(F, StaffCeil - 30.0f)), 650.0f, 600.0f, Cool);
	}
	// The staff time clock on the corridor wall: its name and display across the top of its face, the badge reader below.
	// (Built 30 deep and 10 wide, it stood out of the wall like a fin, with its name floating above it.)
	AddBox(FVector(4600.0f, CorrY - Wall * 0.5f - 6.0f, Zf(F, 140.0f)), FVector(30.0f, 12.0f, 40.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	AddLabel(TEXT("TIME CLOCK"), FVector(4600.0f, CorrY - Wall * 0.5f - 12.4f, Zf(F, 155.0f)), -90.0f, 2.6f, Ink, F);
	AddBox(FVector(4600.0f, CorrY - Wall * 0.5f - 12.15f, Zf(F, 146.0f)), FVector(18.0f, 0.3f, 6.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	AddBox(FVector(4604.0f, CorrY - Wall * 0.5f - 12.35f, Zf(F, 146.0f)), FVector(6.0f, 0.1f, 1.6f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
	AddBox(FVector(4600.0f, CorrY - Wall * 0.5f - 12.3f, Zf(F, 131.0f)), FVector(9.0f, 0.6f, 9.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	CctvDome(F, FVector(4500.0f, 0.0f, Zf(F, StaffCeil)), TEXT("L-09"));
	BuildFireCommand();
	BuildKitchen();
}

void AKcHotel::BuildFrontDesk()
{
	const int32 F = 0;
	// Stone wall behind the desk: walnut cladding, the property name, the clock.
	const float BackY = HalfY - 15.0f;
	AddBox(FVector(1150.0f, BackY - 2.0f, Zf(F, CeilLobby * 0.5f)), FVector(1280.0f, 4.0f, CeilLobby), Mat(TEXT("MI_WoodWalnut")), 0.0f, false);
	// Brass letters on a dark board, back-lit: the glow shows only as a halo round the board (a lit face behind the
	// letters blew out and the name could not be read, walk 12).
	AddBox(FVector(1150.0f, BackY - 5.0f, Zf(F, 262.0f)), FVector(560.0f, 2.0f, 70.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(1150.0f, BackY - 4.2f, Zf(F, 262.0f)), FVector(574.0f, 0.4f, 82.0f), Mat(TEXT("MI_SignGlow")), 0.0f, false);
	AddLabel(TEXT("VELDANE GRAND"), FVector(1150.0f, BackY - 6.6f, Zf(F, 272.0f)), -90.0f, 30.0f, BrassText, F);
	AddLabel(TEXT("HARBOURFRONT"), FVector(1150.0f, BackY - 6.6f, Zf(F, 242.0f)), -90.0f, 10.0f, BrassText, F);
	AddBox(FVector(1150.0f, BackY - 5.0f, Zf(F, 181.0f)), FVector(60.0f, 2.0f, 30.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false); // the clock, with the city under its time
	LobbyClock = AddLabel(TEXT("00:30"), FVector(1150.0f, BackY - 6.6f, Zf(F, 186.0f)), -90.0f, 10.0f, FColor(255, 170, 60), F);
	AddLabel(TEXT("PORT AVENER"), FVector(1150.0f, BackY - 6.6f, Zf(F, 170.0f)), -90.0f, 3.6f, Cream, F);
	// Two-level counter: stone on the guest side at 110, walnut work surface for the staff at 76.
	UMaterialInterface* Stone = Mat(TEXT("MI_StoneCounter"));
	AddBox(FVector(1100.0f, 646.0f, Zf(F, 54.0f)), FVector(1000.0f, 8.0f, 108.0f), Stone);
	AddBox(FVector(1100.0f, 654.0f, Zf(F, 110.0f)), FVector(1012.0f, 34.0f, 4.0f), Stone);
	AddBox(FVector(1100.0f, 642.0f, Zf(F, 4.0f)), FVector(1000.0f, 1.0f, 3.0f), Mat(TEXT("MI_SignGlow")), 0.0f, false);
	AddBox(FVector(1100.0f, 700.0f, Zf(F, 75.0f)), FVector(1000.0f, 64.0f, 3.0f), Mat(TEXT("MI_WoodWalnut")));
	for (float X : {600.0f, 1600.0f}) AddBox(FVector(X, 685.0f, Zf(F, 55.0f)), FVector(8.0f, 90.0f, 110.0f), Stone);
	for (float X : {760.0f, 1060.0f, 1360.0f}) AddBox(FVector(X, 700.0f, Zf(F, 37.0f)), FVector(40.0f, 58.0f, 74.0f), Mat(TEXT("MI_WoodWalnut")));
	// Three stations: screen, keyboard, and the keycard encoders E-01 to E-03 (M002: there is no E-77).
	for (int32 i = 0; i < 3; ++i)
	{
		const float X = 800.0f + i * 300.0f;
		Monitor(FVector(X, 668.0f, Zf(F, 76.5f)), 90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
		AddBox(FVector(X, 708.0f, Zf(F, 77.5f)), FVector(44.0f, 15.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		const FVector Enc(X + 62.0f, 690.0f, Zf(F, 80.5f));
		if (i == 0)
		{
			// Station 1's screen: the front office and lock system terminal (M008, M011).
			SpawnCasePoint(TEXT("front_desk"), TEXT("front_office_pc"), FString(), FVector(X, 669.6f, Zf(F, 76.5f + 38.5f)), 90.0f, Cube, FVector(0.003f, 0.59f, 0.33f),
				Mat(TEXT("MI_ScreenFrontOffice")), EKcUseStyle::Screen);
		}
		if (i == 1)
		{
			SpawnCasePoint(TEXT("front_desk"), TEXT("key_encoder"), TEXT("Keycard encoder E-02"), Enc, 0.0f, Cube, FVector(0.16f, 0.2f, 0.08f), Mat(TEXT("MI_PlasticGrey")),
				EKcUseStyle::Screen);
		}
		else
		{
			AddBox(Enc, FVector(16.0f, 20.0f, 8.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
		}
		AddBox(Enc + FVector(0, 0, 4.1f), FVector(9.0f, 1.2f, 0.4f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		AddBox(Enc + FVector(5.0f, 10.1f, 1.0f), FVector(1.0f, 0.4f, 1.0f), Mat(TEXT("MI_LedGreen")), 0.0f, false);
		AddLabel(FString::Printf(TEXT("E-0%d"), i + 1), Enc + FVector(-3.0f, 10.4f, 0.0f), 90.0f, 1.6f, Ink, F);
		for (int32 k = 0; k < 4; ++k) AddBox(Enc + FVector(26.0f, -4.0f, -3.4f + k * 0.35f), FVector(8.5f, 5.4f, 0.3f), Mat(TEXT("MI_Paper")), 4.0f * k, false);
	}
	SpawnCasePoint(TEXT("front_desk"), TEXT("desk_phone"), FString(), FVector(1235.0f, 690.0f, Zf(F, 80.0f)), 0.0f, Cube, FVector(0.2f, 0.18f, 0.06f), Mat(TEXT("MI_DeviceBlack")),
		EKcUseStyle::Screen);
	AddBox(FVector(1225.0f, 690.0f, Zf(F, 84.0f)), FVector(5.0f, 17.0f, 4.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
	// Front office safe for lost property, in the back counter.
	AddBox(FVector(1600.0f, BackY - 30.0f, Zf(F, 40.0f)), FVector(160.0f, 56.0f, 80.0f), Mat(TEXT("MI_WoodWalnut")));
	SpawnCasePoint(TEXT("front_desk"), TEXT("lost_property"), TEXT("Front office safe (lost property)"), FVector(1640.0f, BackY - 32.0f, Zf(F, 104.0f)), 0.0f, Cube,
		FVector(0.46f, 0.42f, 0.46f), Mat(TEXT("MI_Steel")), EKcUseStyle::Screen);
	AddBox(FVector(1640.0f, BackY - 53.5f, Zf(F, 110.0f)), FVector(1.0f, 8.0f, 10.0f), Mat(TEXT("MI_DeviceBlack")), 90.0f, false);
	AddBox(FVector(1560.0f, BackY - 32.0f, Zf(F, 81.5f)), FVector(30.0f, 23.0f, 3.0f), Mat(TEXT("MI_SuitNavy")), 6.0f, false);
	AddLabel(TEXT("LOST PROPERTY LOG"), FVector(1561.2f, BackY - 43.8f, Zf(F, 81.5f)), -84.0f, 1.2f, Cream, F); // on the spine, turned with the book
	// Guest side: registration folder (the desk's own case point), pens, brochures.
	SpawnCasePoint(TEXT("front_desk"), FString(), TEXT("Front desk counter"), FVector(1000.0f, 652.0f, Zf(F, 113.0f)), -4.0f, Cube, FVector(0.3f, 0.22f, 0.03f),
		Mat(TEXT("MI_WoodWalnut")), EKcUseStyle::PickUp);
	AddInstance(Cylinder, FVector(1080.0f, 652.0f, Zf(F, 117.0f)), 0.0f, FVector(0.06f, 0.06f, 0.1f), Mat(TEXT("MI_Brass")), false);
	AddBox(FVector(1450.0f, 652.0f, Zf(F, 122.0f)), FVector(20.0f, 8.0f, 22.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(1450.0f, 648.0f, Zf(F, 124.0f)), FVector(16.0f, 1.0f, 20.0f), Mat(TEXT("MI_ArtHarbourC")), 0.0f, false);
	// Pendants over the counter and the camera on the desk.
	for (float X : {800.0f, 1100.0f, 1400.0f})
	{
		AddInstance(Cylinder, FVector(X, 690.0f, Zf(F, (CeilLobby + 260.0f) * 0.5f)), 0.0f, FVector(0.006f, 0.006f, (CeilLobby - 260.0f) / 100.0f), Mat(TEXT("MI_Brass")), false);
		// A glowing drum with a diffuser underneath, its light inside it (15 cm below, it lit the drum white, walk 17).
		AddInstance(Cylinder, FVector(X, 690.0f, Zf(F, 255.0f)), 0.0f, FVector(0.22f, 0.22f, 0.12f), Mat(TEXT("MI_LampShade")), false);
		AddInstance(Cylinder, FVector(X, 690.0f, Zf(F, 248.7f)), 0.0f, FVector(0.18f, 0.18f, 0.004f), Mat(TEXT("MI_LampWarm")), false);
		AddLight(F, FVector(X, 690.0f, Zf(F, 254.0f)), 650.0f, 500.0f, Warm);
	}
	CctvDome(F, FVector(1100.0f, 520.0f, Zf(F, CeilLobby)), TEXT("L-02"));
}

void AKcHotel::BuildLobbyBar()
{
	const int32 F = 0;
	FloorFinish(F, FBox2D(FVector2D(15.0f, -HalfY + 15.0f), FVector2D(CoreX0, -450.0f)), Mat(TEXT("MI_FloorParquet")), 0.6f);
	// Planter screen with the way in, and the closed sign.
	for (const FVector2D& Run : {FVector2D(120.0f, 880.0f), FVector2D(1220.0f, 1950.0f)})
	{
		AddBox(FVector((Run.X + Run.Y) * 0.5f, -450.0f, Zf(F, 45.0f)), FVector(Run.Y - Run.X, 40.0f, 90.0f), Mat(TEXT("MI_WoodWalnut")));
		for (float X = Run.X + 60.0f; X < Run.Y; X += 150.0f) Plant(FVector(X, -450.0f, Zf(F, 90.0f)), false);
	}
	for (float X : {900.0f, 1200.0f}) AddInstance(Cylinder, FVector(X, -450.0f, Zf(F, 50.0f)), 0.0f, FVector(0.08f, 0.08f, 1.0f), Mat(TEXT("MI_Brass")), true);
	AddBox(FVector(1050.0f, -450.0f, Zf(F, 85.0f)), FVector(300.0f, 3.0f, 3.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
	AddBox(FVector(1050.0f, -452.0f, Zf(F, 120.0f)), FVector(70.0f, 2.0f, 30.0f), Mat(TEXT("MI_WoodWalnut")), 0.0f, false);
	AddLabel(TEXT("LOBBY BAR"), FVector(1050.0f, -450.6f, Zf(F, 126.0f)), 90.0f, 4.4f, BrassText, F);
	AddLabel(TEXT("Closed. Opens at 11:00"), FVector(1050.0f, -450.6f, Zf(F, 115.0f)), 90.0f, 2.4f, Cream, F);
	// Bar counter along the east side, the back bar against the core.
	AddBox(FVector(1750.0f, -740.0f, Zf(F, 54.0f)), FVector(60.0f, 440.0f, 108.0f), Mat(TEXT("MI_WoodWalnut")));
	AddBox(FVector(1742.0f, -740.0f, Zf(F, 110.0f)), FVector(80.0f, 450.0f, 4.0f), Mat(TEXT("MI_StoneCounter")));
	AddBox(FVector(CoreX0 - 25.0f, -740.0f, Zf(F, 45.0f)), FVector(50.0f, 440.0f, 90.0f), Mat(TEXT("MI_WoodWalnut")));
	AddBox(FVector(CoreX0 - 2.0f, -740.0f, Zf(F, 175.0f)), FVector(2.0f, 440.0f, 150.0f), Mat(TEXT("MI_Mirror")), 0.0f, false);
	FRandomStream R(707);
	static const TCHAR* const Bottles[] = {TEXT("MI_BottleGreen"), TEXT("MI_BottleAmber"), TEXT("MI_Glass")};
	for (int32 Shelf = 0; Shelf < 3; ++Shelf)
	{
		const float Z = 110.0f + Shelf * 42.0f;
		AddBox(FVector(CoreX0 - 14.0f, -740.0f, Zf(F, Z)), FVector(24.0f, 430.0f, 2.0f), Mat(TEXT("MI_Glass")), 0.0f, false);
		for (float Y = -950.0f; Y < -530.0f; Y += R.FRandRange(9.0f, 15.0f))
		{
			const float H = R.FRandRange(24.0f, 33.0f);
			AddInstance(Cylinder, FVector(CoreX0 - 14.0f, Y, Zf(F, Z + 1.0f + H * 0.5f)), 0.0f, FVector(0.075f, 0.075f, H / 100.0f), Mat(Bottles[R.RandRange(0, 2)]), false);
		}
	}
	AddBox(FVector(CoreX0 - 25.0f, -740.0f, Zf(F, 92.0f)), FVector(46.0f, 430.0f, 1.0f), Mat(TEXT("MI_SignGlow")), 0.0f, false);
	for (float Y = -920.0f; Y <= -560.0f; Y += 90.0f)
	{
		if (!AddInstance(Model(TEXT("bar_chair_round_01")), FVector(1680.0f, Y, Zf(F, 0.0f)), ModelYawOffset, FVector(1.0f)))
		{
			AddInstance(Cylinder, FVector(1680.0f, Y, Zf(F, 38.0f)), 0.0f, FVector(0.05f, 0.05f, 0.76f), Mat(TEXT("MI_Brass")), true);
			AddInstance(Cylinder, FVector(1680.0f, Y, Zf(F, 78.0f)), 0.0f, FVector(0.38f, 0.38f, 0.06f), Mat(TEXT("MI_Leather")), false);
		}
	}
	// Eight numbered tables by the windows (table 6: where Mr Whitlock left his bag in M001).
	for (int32 i = 0; i < 8; ++i)
	{
		const FVector T(290.0f + (i % 4) * 400.0f, i < 4 ? -890.0f : -620.0f, Zf(F, 0.0f)); // off the mullion lines (1050: the pendant stood in front of one)
		AddInstance(Cylinder, T + FVector(0, 0, 36.0f), 0.0f, FVector(0.75f, 0.75f, 0.03f), Mat(TEXT("MI_StoneCounter")), true);
		AddInstance(Cylinder, T + FVector(0, 0, 18.0f), 0.0f, FVector(0.08f, 0.08f, 0.36f), Mat(TEXT("MI_Brass")), true);
		AddBox(T + FVector(0, 0, 42.0f), FVector(2.0f, 7.0f, 9.0f), Mat(TEXT("MI_Paper")), 30.0f, false);
		AddLabel(FString::FromInt(i + 1), T + FVector(1.2f, 0.7f, 42.0f), 30.0f, 4.0f, Ink, F);
		Chair(T + FVector(-88.0f, 0, 0), 0.0f); // at 70 the armchairs' arms went into the table top (walk 12)
		Chair(T + FVector(88.0f, 0, 0), 180.0f);
		AddLight(F, T + FVector(0, 0, 239.0f), 360.0f, 380.0f, Warm); // inside the drum: 10 cm under it, it lit the drum white (walk 17)
		AddInstance(Cylinder, T + FVector(0, 0, 240.0f), 0.0f, FVector(0.3f, 0.3f, 0.14f), Mat(TEXT("MI_LampShade")), false);
		AddInstance(Cylinder, T + FVector(0, 0, 232.7f), 0.0f, FVector(0.24f, 0.24f, 0.004f), Mat(TEXT("MI_LampWarm")), false);
		AddInstance(Cylinder, T + FVector(0, 0, (CeilLobby + 247.0f) * 0.5f), 0.0f, FVector(0.006f, 0.006f, (CeilLobby - 247.0f) / 100.0f), Mat(TEXT("MI_Brass")), false);
	}
	CctvDome(F, FVector(1050.0f, -760.0f, Zf(F, CeilLobby)), TEXT("L-07"));
	SmokeDetector(F, FVector2D(600.0f, -760.0f), CeilLobby);
	// The bar's own case point: the night bar log on the counter (M005 moves it to the casualty).
	LobbyBarCounter = FVector(1742.0f, -600.0f, Zf(F, 112.5f));
	LobbyBarPoint = SpawnCasePoint(TEXT("lobby_bar"), FString(), TEXT("Lobby bar"), LobbyBarCounter, -84.0f, Cube, FVector(0.22f, 0.3f, 0.02f), Mat(TEXT("MI_Paper")),
		EKcUseStyle::PickUp);
}

void AKcHotel::BuildFireCommand()
{
	const int32 F = 0;
	const float X0 = CoreX1, X1 = 3500.0f - Wall * 0.5f, Y0 = CorrY + Wall * 0.5f, Y1 = HalfY - 15.0f;
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FloorVinyl")));
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilBack, Mat(TEXT("MI_CeilingLow"))); // Low, not Dim: the downlights are spots now and no longer light it (round 18)
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilBack * 0.5f)), FVector(1.0f, Y1 - Y0, CeilBack), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	AddBox(FVector((X0 + X1) * 0.5f, Y1 - 0.5f, Zf(F, CeilBack * 0.5f)), FVector(X1 - X0, 1.0f, CeilBack), Mat(TEXT("MI_WallPaint")), 0.0f, false);
	// Fire alarm control panel: a quiet night reads SYSTEM NORMAL.
	const FVector P(3100.0f, Y1 - 6.0f, Zf(F, 150.0f));
	AddBox(P, FVector(130.0f, 12.0f, 100.0f), Mat(TEXT("MI_FireRed")));
	// The panel's screen is where its work is done (M012, M013, M015); what it reads is set per scene.
	SpawnCasePoint(TEXT("fire_command"), TEXT("fire_panel"), FString(), P - FVector(0, 6.3f, -18.0f), -90.0f, Cube, FVector(0.004f, 0.6f, 0.22f), Mat(TEXT("MI_ScreenPanel")),
		EKcUseStyle::Screen, P - FVector(0, 80.0f, -10.0f));
	AddLabel(TEXT("FIRE ALARM CONTROL PANEL"), P - FVector(0, 6.6f, -42.0f), -90.0f, 2.8f, FColor::White, F);
	for (int32 i = 0; i < 6; ++i) AddBox(P - FVector(-45.0f + i * 18.0f, 6.4f, 22.0f), FVector(8.0f, 0.6f, 5.0f), Mat(i == 5 ? TEXT("MI_LedAmber") : TEXT("MI_Ceramic")), 0.0f, false);
	AddLabel(TEXT("Alarm  Fault  Silence  Reset  Evacuate"), P - FVector(0, 6.6f, 32.0f), -90.0f, 1.8f, FColor::White, F);
	// Building plans for the fire authority, the red phone, the PA desk, the evacuation plan.
	AddBox(FVector(X1 - 40.0f, 500.0f, Zf(F, 60.0f)), FVector(70.0f, 120.0f, 120.0f), Mat(TEXT("MI_FireRed")));
	AddLabel(TEXT("PLANS: FIRE AUTHORITY"), FVector(X1 - 75.6f, 500.0f, Zf(F, 100.0f)), 180.0f, 2.6f, FColor::White, F);
	Desk(FVector(2860.0f, 600.0f, Zf(F, 0.0f)), 90.0f, FVector(140.0f, 70.0f, DeskH), Mat(TEXT("MI_Graphite")));
	AddBox(FVector(2850.0f, 640.0f, Zf(F, DeskH + 4.0f)), FVector(18.0f, 20.0f, 8.0f), Mat(TEXT("MI_FireRed")), 0.0f, false);
	AddInstance(Cylinder, FVector(2870.0f, 570.0f, Zf(F, DeskH + 14.0f)), 0.0f, FVector(0.01f, 0.01f, 0.26f), Mat(TEXT("MI_Graphite")), false);
	AddInstance(Sphere, FVector(2870.0f, 570.0f, Zf(F, DeskH + 28.0f)), 0.0f, FVector(0.05f), Mat(TEXT("MI_DeviceBlack")), false);
	AddLabel(TEXT("EVACUATION PA"), FVector(2896.0f, 600.0f, Zf(F, DeskH + 1.8f)), 0.0f, 1.8f, Ink, F);
	TaskChair(FVector(2790.0f, 600.0f, Zf(F, 0.0f)), 0.0f);
	AddBox(FVector(X0 + 2.0f, 820.0f, Zf(F, 160.0f)), FVector(3.0f, 140.0f, 100.0f), Mat(TEXT("MI_FloorPlan")), 0.0f, false);
	AddLabel(TEXT("EVACUATION PLAN: TYPICAL GUEST FLOOR (2-14)"), FVector(X0 + 1.4f, 820.0f, Zf(F, 216.0f)), 0.0f, 2.6f, Ink, F); // on the wall over the plan
	AddBox(FVector(3380.0f, Y0 + 6.0f, Zf(F, 150.0f)), FVector(30.0f, 10.0f, 40.0f), Mat(TEXT("MI_FireRed")), 0.0f, false);
	AddLabel(TEXT("KEY BOX"), FVector(3380.0f, Y0 + 11.4f, Zf(F, 163.0f)), 90.0f, 2.0f, Ink, F); // on its door, not floating above it
	SpawnCasePoint(TEXT("fire_command"), FString(), TEXT("Fire panel event log"), P - FVector(-50.0f, 7.0f, 40.0f), 0.0f, Cube, FVector(0.2f, 0.02f, 0.14f), Mat(TEXT("MI_Paper")),
		EKcUseStyle::Screen, P);
	// The rest of the room (it read nearly empty in walk 12): the sprinkler and voice-alarm rack, the wardens' vests
	// and helmets by the door, the drawings chest, a clock, skirting on the lined walls.
	{
		const FVector Rack(X0 + 32.0f, 420.0f, Zf(F, 100.0f));
		AddBox(Rack, FVector(60.0f, 90.0f, 200.0f), Mat(TEXT("MI_Graphite")), 0.0f, true);
		AddBox(Rack + FVector(30.4f, 0, 30.0f), FVector(0.4f, 80.0f, 110.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
		for (int32 r = 0; r < 6; ++r)
		{
			for (int32 c = 0; c < 8; ++c)
			{
				AddBox(Rack + FVector(30.8f, -32.0f + c * 9.0f, 75.0f - r * 16.0f), FVector(0.3f, 1.2f, 0.8f), Mat((r * 3 + c) % 7 ? TEXT("MI_LedGreen") : TEXT("MI_LedAmber")), 0.0f, false);
			}
		}
		AddLabel(TEXT("SPRINKLER ZONES / VOICE ALARM"), Rack + FVector(31.0f, 0, 92.0f), 0.0f, 2.4f, Cream, F);
		AddBox(FVector(2900.0f, Y0 + 3.0f, Zf(F, 182.0f)), FVector(160.0f, 3.0f, 3.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
		for (int32 i = 0; i < 3; ++i)
		{
			AddBox(FVector(2840.0f + i * 55.0f, Y0 + 6.0f, Zf(F, 150.0f)), FVector(44.0f, 4.0f, 62.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, false);
			AddBox(FVector(2840.0f + i * 55.0f, Y0 + 8.2f, Zf(F, 148.0f)), FVector(44.0f, 0.4f, 4.0f), Mat(TEXT("MI_Chrome")), 0.0f, false);
			AddLabel(TEXT("WARDEN"), FVector(2840.0f + i * 55.0f, Y0 + 8.6f, Zf(F, 160.0f)), 90.0f, 2.4f, Ink, F);
		}
		AddBox(FVector(2900.0f, Y0 + 20.0f, Zf(F, 112.0f)), FVector(170.0f, 36.0f, 3.0f), Mat(TEXT("MI_WoodOak")), 0.0f, false);
		for (int32 i = 0; i < 3; ++i) AddInstance(Sphere, FVector(2840.0f + i * 55.0f, Y0 + 22.0f, Zf(F, 122.0f)), 0.0f, FVector(0.28f, 0.32f, 0.2f), Mat(i == 0 ? TEXT("MI_FireRed") : TEXT("MI_Paper")), false);
		AddBox(FVector(3300.0f, 840.0f, Zf(F, 45.0f)), FVector(120.0f, 90.0f, 90.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, true);
		for (int32 d = 0; d < 4; ++d) AddBox(FVector(3300.0f, 794.6f, Zf(F, 12.0f + d * 21.0f)), FVector(110.0f, 0.6f, 18.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
		AddBox(FVector(3300.0f, 840.0f, Zf(F, 91.5f)), FVector(84.0f, 60.0f, 1.2f), Mat(TEXT("MI_FloorPlan")), 4.0f, false);
		// The alarm sounder-beacon: a red body with a clear lens (a 30 cm white disc under the downlight blew out, walk 18).
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(3100.0f, Y1 - 4.0f, Zf(F, 232.0f)), FVector(0.11f, 0.11f, 0.08f)), Mat(TEXT("MI_FireRed")), false, false);
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(3100.0f, Y1 - 8.5f, Zf(F, 232.0f)), FVector(0.06f, 0.06f, 0.02f)), Mat(TEXT("MI_PlasticGrey")), false, false);
		AddBox(FVector((X0 + X1) * 0.5f, Y1 - 1.6f, Zf(F, 6.0f)), FVector(X1 - X0, 1.2f, 12.0f), Mat(TEXT("MI_Skirting")), 0.0f, false);
		AddBox(FVector(X0 + 1.6f, (Y0 + Y1) * 0.5f, Zf(F, 6.0f)), FVector(1.2f, Y1 - Y0, 12.0f), Mat(TEXT("MI_Skirting")), 0.0f, false);
	}
	CeilingLight(F, FVector(3100.0f, 450.0f, Zf(F, CeilBack)), DownLumens * 1.3f, Neutral);
	CeilingLight(F, FVector(3100.0f, 800.0f, Zf(F, CeilBack)), DownLumens * 1.3f, Neutral);
	SmokeDetector(F, FVector2D(3100.0f, 620.0f), CeilBack);
}

void AKcHotel::BuildKitchen()
{
	// Main kitchen, under the staff corridor's south side: the cook line and its extract canopy on the
	// south wall, prep tables, the pass by the doors, dish wash, the walk-in, and the permit board (M015).
	const int32 F = 0;
	const float X0 = CoreX1, X1 = MaxX - 15.0f, Y0 = -HalfY + 15.0f, Y1 = -CorrY - Wall * 0.5f;
	UMaterialInterface* Tile = Mat(TEXT("MI_Tile"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));
	UMaterialInterface* Chrome = Mat(TEXT("MI_Chrome"));
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_KitchenTile")));
	// Faintly self-lit like the B1 corridor: plain white went black between the battens and white round them (walk 17).
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), CeilBack, Mat(TEXT("MI_CeilingMid")));
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilBack * 0.5f)), FVector(1.0f, Y1 - Y0, CeilBack), Tile, 0.0f, false);
	AddBox(FVector(X1 - 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilBack * 0.5f)), FVector(1.0f, Y1 - Y0, CeilBack), Tile, 0.0f, false);
	AddBox(FVector((X0 + X1) * 0.5f, Y0 + 0.5f, Zf(F, CeilBack * 0.5f)), FVector(X1 - X0, 1.0f, CeilBack), Tile, 0.0f, false); // full height: a bare band showed over 200 (walk 14)

	// The cook line: two fryers at the west end, then ranges, a griddle and a salamander, all under one canopy.
	const float LineY = Y0 + 50.0f;
	for (int32 i = 0; i < 9; ++i)
	{
		const float X = 3500.0f + i * 110.0f;
		const bool bFryer = i < 2;
		AddBox(FVector(X, LineY, Zf(F, 45.0f)), FVector(104.0f, 90.0f, 90.0f), Steel);
		AddBox(FVector(X, LineY, Zf(F, 90.5f)), FVector(100.0f, 86.0f, 1.0f), Mat(bFryer ? TEXT("MI_Graphite") : TEXT("MI_DeviceBlack")), 0.0f, false);
		for (int32 k = 0; k < 4; ++k) AddBox(FVector(X - 36.0f + k * 24.0f, LineY + 46.0f, Zf(F, 70.0f)), FVector(4.0f, 2.0f, 4.0f), Chrome, 0.0f, false);
		if (bFryer)
		{
			// Two baskets down in the vats, their handles out to the front (plates above the fryer floated, walk 16).
			for (float Side : {-1.0f, 1.0f})
			{
				AddBox(FVector(X + Side * 22.0f, LineY - 4.0f, Zf(F, 86.0f)), FVector(30.0f, 36.0f, 10.0f), Mat(TEXT("MI_SteelDark")), 0.0f, false);
				AddInstance(Cube, FTransform(FRotator(0.0f, 0.0f, -30.0f), FVector(X + Side * 22.0f, LineY + 22.0f, Zf(F, 96.0f)), FVector(0.02f, 0.02f, 0.24f)), Chrome, false, false);
				AddBox(FVector(X + Side * 22.0f, LineY + 28.0f, Zf(F, 106.0f)), FVector(3.0f, 10.0f, 3.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
			}
		}
		else if (i < 7)
		{
			// A six-burner range: cast-iron grates over the burners, the oven door with its window and bar handle.
			for (int32 b = 0; b < 6; ++b)
			{
				const FVector B(X - 30.0f + (b % 3) * 30.0f, LineY - 18.0f + (b / 3) * 36.0f, Zf(F, 91.5f));
				AddInstance(Cylinder, B, 0.0f, FVector(0.12f, 0.12f, 0.015f), Mat(TEXT("MI_Graphite")), false);
				AddBox(B + FVector(0, 0, 2.0f), FVector(26.0f, 2.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
				AddBox(B + FVector(0, 0, 2.0f), FVector(2.0f, 26.0f, 2.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);
			}
			AddBox(FVector(X, LineY + 45.3f, Zf(F, 38.0f)), FVector(80.0f, 0.6f, 50.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
			AddBox(FVector(X, LineY + 45.8f, Zf(F, 36.0f)), FVector(40.0f, 0.4f, 20.0f), Mat(TEXT("MI_GlassDark")), 0.0f, false);
			AddBox(FVector(X, LineY + 48.0f, Zf(F, 60.0f)), FVector(64.0f, 2.0f, 2.0f), Chrome, 0.0f, false);
		}
		else if (i == 7)
		{
			AddBox(FVector(X, LineY - 5.0f, Zf(F, 92.0f)), FVector(96.0f, 70.0f, 2.0f), Mat(TEXT("MI_Steel")), 0.0f, false); // the griddle plate
		}
		else
		{
			AddBox(FVector(X, LineY - 20.0f, Zf(F, 178.0f)), FVector(90.0f, 40.0f, 36.0f), Steel, 0.0f, false);  // the salamander over the line
			AddBox(FVector(X, LineY - 20.0f, Zf(F, 159.7f)), FVector(80.0f, 32.0f, 0.6f), Mat(TEXT("MI_LampShade")), 0.0f, false); // its glowing element
		}
	}
	AddBox(FVector(3500.0f + 4.0f * 110.0f, LineY, Zf(F, 150.0f)), FVector(990.0f, 30.0f, 2.0f), Steel, 0.0f, false);
	for (float X : {3460.0f, 3940.0f, 4420.0f})
	{
		// Its brackets, each with a diagonal strut to the wall: arms alone read as a floating ledge (walk 14).
		AddBox(FVector(X, (Y0 + LineY + 15.0f) * 0.5f, Zf(F, 144.0f)), FVector(3.0f, LineY + 15.0f - Y0, 10.0f), Steel, 0.0f, false);
		AddInstance(Cube, FTransform(FRotator(0.0f, 0.0f, 54.0f), FVector(X, Y0 + 27.5f, Zf(F, 120.0f)), FVector(0.03f, 0.03f, 0.68f)), Steel, false, false);
		AddBox(FVector(X, Y0 + 1.5f, Zf(F, 115.0f)), FVector(8.0f, 3.0f, 70.0f), Steel, 0.0f, false);
	}
	// The extract canopy: stainless box, baffle filters along its face (a scene prop: M015 takes them
	// out), wet chemical nozzles, and four hanging rods to the slab. The rear one at the west end is cracked.
	const float CanX0 = 3420.0f, CanX1 = 4520.0f, CanY = Y0 + 65.0f;
	AddBox(FVector((CanX0 + CanX1) * 0.5f, CanY, Zf(F, 235.0f)), FVector(CanX1 - CanX0, 130.0f, 50.0f), Steel);
	AddBox(FVector((CanX0 + CanX1) * 0.5f, CanY + 66.0f, Zf(F, 214.0f)), FVector(CanX1 - CanX0, 2.0f, 8.0f), Chrome, 0.0f, false);
	for (float X = CanX0 + 60.0f; X < CanX1; X += 110.0f) AddInstance(Cylinder, FVector(X, CanY, Zf(F, 207.0f)), 0.0f, FVector(0.03f, 0.03f, 0.06f), Chrome, false);
	for (float X : {CanX0 + 40.0f, CanX1 - 40.0f})
	{
		for (float Y : {CanY - 50.0f, CanY + 50.0f}) AddInstance(Cylinder, FVector(X, Y, Zf(F, 270.0f)), 0.0f, FVector(0.02f, 0.02f, 0.2f), Steel, false);
	}
	AddBox(FVector(CanX0 + 40.0f, CanY - 50.0f, Zf(F, 266.0f)), FVector(3.0f, 3.0f, 1.0f), Mat(TEXT("MI_Cardboard")), 0.0f, false); // rust at the crack (a red LED square read as a marker)
	AddBox(FVector((CanX0 + CanX1) * 0.5f, Y0 + 30.0f, Zf(F, CeilBack - 10.0f)), FVector(200.0f, 50.0f, 20.0f), Steel, 0.0f, false);
	// Detector zone K2: the two heat detectors over the canopy front.
	for (float X : {3700.0f, 4300.0f}) SmokeDetector(F, FVector2D(X, CanY + 120.0f), CeilBack);
	// Wet chemical release and the fire blanket at the west end of the line.
	AddBox(FVector(CanX0 - 40.0f, Y0 + 4.0f, Zf(F, 140.0f)), FVector(22.0f, 8.0f, 30.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, false);
	// Its sign on the tile above it (the name floated in the air over the pull box).
	AddBox(FVector(CanX0 - 40.0f, Y0 + 1.3f, Zf(F, 163.0f)), FVector(36.0f, 0.6f, 6.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, false);
	AddLabel(TEXT("HOOD FIRE SUPPRESSION: PULL"), FVector(CanX0 - 40.0f, Y0 + 2.0f, Zf(F, 163.0f)), 90.0f, 1.8f, Ink, F);
	AddBox(FVector(CanX0 - 90.0f, Y0 + 5.0f, Zf(F, 150.0f)), FVector(30.0f, 10.0f, 34.0f), Mat(TEXT("MI_FireRed")), 0.0f, false);
	AddLabel(TEXT("FIRE BLANKET"), FVector(CanX0 - 90.0f, Y0 + 10.4f, Zf(F, 150.0f)), 90.0f, 2.0f, FColor::White, F);
	FireExtinguisher(F, FVector(CanX0 - 140.0f, Y0, 0.0f), 90.0f);
	// The floor gully in front of the line.
	AddBox(FVector((CanX0 + CanX1) * 0.5f, LineY + 120.0f, Zf(F, 0.4f)), FVector(CanX1 - CanX0, 14.0f, 0.6f), Steel, 0.0f, false);
	SpawnCasePoint(TEXT("kitchen"), TEXT("kitchen_line"), FString(), FVector(3560.0f, LineY + 46.5f, Zf(F, 82.0f)), 90.0f, Cube, FVector(0.02f, 0.9f, 0.08f), Chrome,
		EKcUseStyle::Screen, FVector(CanX0 + 40.0f, CanY - 40.0f, Zf(F, 255.0f)));

	// Prep: two steel tables down the middle with boards and the shelf of pans above them.
	for (float X : {3450.0f, 4050.0f})
	{
		Desk(FVector(X, -560.0f, Zf(F, 0.0f)), 0.0f, FVector(260.0f, 80.0f, 90.0f), Steel);
		AddBox(FVector(X - 60.0f, -560.0f, Zf(F, 91.0f)), FVector(50.0f, 34.0f, 2.0f), Mat(TEXT("MI_Ceramic")), 6.0f, false);
		AddBox(FVector(X + 60.0f, -560.0f, Zf(F, 91.0f)), FVector(50.0f, 34.0f, 2.0f), Mat(TEXT("MI_WoodOak")), -4.0f, false); // a wooden board (green read as a lit panel, walk 13-14)
	}
	// The pass by the doors: a steel shelf under heat lamps, the ticket rail, tonight's last chits.
	AddBox(FVector(3700.0f, Y1 - 40.0f, Zf(F, 45.0f)), FVector(500.0f, 70.0f, 90.0f), Steel);
	AddBox(FVector(3700.0f, Y1 - 40.0f, Zf(F, 140.0f)), FVector(500.0f, 50.0f, 3.0f), Steel, 0.0f, false);
	for (float X : {3530.0f, 3700.0f, 3870.0f})
	{
		// Heat lamps: a steel hood on a rod, the element glowing underneath.
		AddBox(FVector(X, Y1 - 40.0f, Zf(F, 190.0f)), FVector(40.0f, 20.0f, 8.0f), Steel, 0.0f, false);
		AddBox(FVector(X, Y1 - 40.0f, Zf(F, 185.7f)), FVector(36.0f, 16.0f, 0.6f), Mat(TEXT("MI_LampShade")), 0.0f, false);
		AddBox(FVector(X, Y1 - 40.0f, Zf(F, (194.0f + CeilBack) * 0.5f)), FVector(1.5f, 1.5f, CeilBack - 194.0f), Chrome, 0.0f, false);
	}
	AddBox(FVector(3700.0f, Y1 - 8.0f, Zf(F, 160.0f)), FVector(480.0f, 2.0f, 3.0f), Chrome, 0.0f, false);
	for (int32 i = 0; i < 5; ++i) AddBox(FVector(3520.0f + i * 70.0f, Y1 - 9.2f, Zf(F, 150.0f)), FVector(8.0f, 0.4f, 16.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	SpawnCasePoint(TEXT("kitchen"), FString(), TEXT("The pass"), FVector(3700.0f, Y1 - 40.0f, Zf(F, 91.0f)), 0.0f, Cube, FVector(0.3f, 0.22f, 0.02f), Mat(TEXT("MI_Paper")),
		EKcUseStyle::PickUp);
	// Dish wash in the north-west corner: the pot sink, the hood machine, the rack trolley.
	AddBox(FVector(X0 + 140.0f, Y1 - 45.0f, Zf(F, 45.0f)), FVector(240.0f, 80.0f, 90.0f), Steel);
	AddBox(FVector(X0 + 90.0f, Y1 - 45.0f, Zf(F, 80.0f)), FVector(70.0f, 60.0f, 20.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(X0 + 300.0f, Y1 - 50.0f, Zf(F, 95.0f)), FVector(70.0f, 70.0f, 190.0f), Steel);
	AddBox(FVector(X0 + 80.0f, -560.0f, Zf(F, 80.0f)), FVector(60.0f, 60.0f, 160.0f), Steel, 0.0f, true);
	// Dry goods and pans on steel shelving along the south wall west of the line, and a sheet-pan rack: the west half
	// of the kitchen read empty (walk 12-13).
	{
		FRandomStream K(2251);
		for (float X : {2720.0f, 2960.0f, 3200.0f}) AddBox(FVector(X, Y0 + 26.0f, Zf(F, 95.0f)), FVector(3.0f, 48.0f, 190.0f), Steel);
		for (int32 L = 0; L < 4; ++L)
		{
			const float Z = 15.0f + L * 50.0f;
			AddBox(FVector(2960.0f, Y0 + 26.0f, Zf(F, Z)), FVector(480.0f, 48.0f, 2.0f), Steel, 0.0f, false);
			if (L == 3) break;
			for (float X = 2740.0f; X < 3180.0f; X += K.FRandRange(38.0f, 56.0f))
			{
				if (L == 0)
				{
					AddInstance(Cylinder, FVector(X + 14.0f, Y0 + 26.0f, Zf(F, Z + 16.0f)), 0.0f, FVector(0.3f, 0.3f, 0.3f), Chrome, false); // stock pots
				}
				else
				{
					const float H = K.FRandRange(18.0f, 34.0f);
					AddBox(FVector(X + 14.0f, Y0 + 26.0f, Zf(F, Z + 1.0f + H * 0.5f)), FVector(K.FRandRange(22.0f, 32.0f), 36.0f, H),
						Mat(L == 1 ? TEXT("MI_Cardboard") : (K.FRand() < 0.5f ? TEXT("MI_Ceramic") : TEXT("MI_PlasticGrey"))), 0.0f, false);
				}
			}
		}
		const FVector Rack(3150.0f, -430.0f, Zf(F, 0.0f));
		for (float Dx : {-24.0f, 24.0f}) for (float Dy : {-32.0f, 32.0f}) AddBox(Rack + FVector(Dx, Dy, 90.0f), FVector(2.5f, 2.5f, 172.0f), Steel, 0.0f, true);
		for (int32 r = 0; r < 12; ++r) AddBox(Rack + FVector(0, 0, 20.0f + r * 13.0f), FVector(46.0f, 66.0f, 1.2f), r % 3 ? Steel : Chrome, 0.0f, false);
		AddBox(FVector(X0 + 25.0f, -400.0f, Zf(F, 85.0f)), FVector(40.0f, 50.0f, 22.0f), Steel, 0.0f, false); // hand-wash basin
		AddBox(FVector(X0 + 4.0f, -400.0f, Zf(F, 112.0f)), FVector(4.0f, 4.0f, 30.0f), Chrome, 0.0f, false);
		AddBox(FVector(X0 + 2.0f, -400.0f, Zf(F, 150.0f)), FVector(2.0f, 34.0f, 20.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
		AddLabel(TEXT("HAND WASH ONLY"), FVector(X0 + 3.4f, -400.0f, Zf(F, 150.0f)), 0.0f, 2.2f, Ink, F);
	}
	// Walk-in cooler in the north-east corner.
	AddBox(FVector(4600.0f, (Y1 - 420.0f + Y1) * 0.5f - 210.0f, Zf(F, 125.0f)), FVector(370.0f, 420.0f, 250.0f), Mat(TEXT("MI_PlasticGrey")));
	AddLabel(TEXT("WALK-IN COOLER  3 C"), FVector(4411.4f, Y1 - 420.0f, Zf(F, 190.0f)), 180.0f, 3.0f, Ink, F); // on the door's face, not inside it
	AddBox(FVector(4413.0f, Y1 - 420.0f, Zf(F, 100.0f)), FVector(2.0f, 90.0f, 200.0f), Steel, 0.0f, false);

	// The permit board on the west wall by the dish area: hot work and other permits to work.
	const FVector Board(X0 + 2.0f, -700.0f, Zf(F, 150.0f));
	AddBox(Board, FVector(2.0f, 120.0f, 80.0f), Mat(TEXT("MI_Cork")), 0.0f, false);
	AddLabel(TEXT("PERMITS TO WORK"), Board + FVector(2.0f, 0, 48.0f), 0.0f, 3.0f, Ink, F);
	AddLabel(TEXT("No permit, no flame."), Board + FVector(2.0f, 0, -48.0f), 0.0f, 2.0f, FColor(170, 30, 30), F);
	SpawnCasePoint(TEXT("kitchen"), TEXT("permit_board"), FString(), Board + FVector(2.0f, -20.0f, 4.0f), 0.0f, Cube, FVector(0.01f, 0.22f, 0.3f), Mat(TEXT("MI_Paper")),
		EKcUseStyle::Screen, Board + FVector(70.0f, 0, 0));
	AddBox(Board + FVector(2.0f, 34.0f, 6.0f), FVector(0.4f, 22.0f, 30.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	// By the doors: the hygiene sign and the second extinguisher.
	AddBox(FVector(3200.0f, Y1 - 1.0f, Zf(F, 160.0f)), FVector(50.0f, 1.0f, 36.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("WHITES  HAIR NET  WASH HANDS"), FVector(3200.0f, Y1 - 1.7f, Zf(F, 164.0f)), -90.0f, 2.2f, Ink, F);
	FireExtinguisher(F, FVector(3420.0f, Y1, 0.0f), -90.0f);

	for (float X : {3000.0f, 3600.0f, 4200.0f})
	{
		AddBox(FVector(X, -480.0f, Zf(F, CeilBack - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, -480.0f, Zf(F, CeilBack - 30.0f)), 900.0f, 750.0f, Neutral, X == 3600.0f); // 16 below lit the ceiling in hot rings (walk 17)
	}
	// A second row over the prep tables and the west end: with one row the south half and the doors' end read black.
	for (float X : {2950.0f, 3600.0f, 4250.0f})
	{
		AddBox(FVector(X, -760.0f, Zf(F, CeilBack - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, -760.0f, Zf(F, CeilBack - 30.0f)), 700.0f, 650.0f, Neutral);
	}
	// Under the canopy, a narrow beam onto the line: full strength blew the hood out (walk 16), a point light its underside
	// and the tiles behind (walk 17).
	CeilingLight(F, FVector(3900.0f, CanY + 30.0f, Zf(F, 210.0f)), DownLumens * 0.5f, Neutral, 300.0f, 50.0f);
	// The west wall by the doors: a wash, or it reads black behind the pass.
	for (float Y : {Y0 + 200.0f, Y1 - 150.0f}) AddLight(F, FVector(X0 + 50.0f, Y, Zf(F, CeilBack - 50.0f)), 800.0f, 500.0f, Neutral);
}

// ---------------------------------------------------------------------------- 1: ballroom floor

void AKcHotel::BuildBallroomFloor()
{
	// Floor 1: the pre-function foyer west of the core, the Harbour Ballroom east of it, and the banquet
	// back of house along the ballroom's south side: service corridor, banquet office, bar store (M016, M017).
	const int32 F = 1;
	const float CeilFoyer = 300.0f, ServiceY = -760.0f;
	UMaterialInterface* Plaster = Mat(TEXT("MI_WallPlaster"));
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	UMaterialInterface* Steel = Mat(TEXT("MI_Steel"));

	// The foyer: carpet, two function rooms closed tonight, sofas and the event screen.
	FloorFinish(F, FBox2D(FVector2D(15.0f, -CorrY), FVector2D(CoreX1, CorrY)), Mat(TEXT("MI_CorridorCarpet")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -CorrY), FVector2D(CoreX0, CorrY)), CeilFoyer, Mat(TEXT("MI_CeilingWhite")));
	WallX(F, CorrY, 15.0f, CoreX0, {FVector2D(1000.0f, 200.0f)}, Plaster, CeilFoyer);
	WallX(F, -CorrY, 15.0f, CoreX0, {FVector2D(1000.0f, 200.0f)}, Plaster, CeilFoyer);
	for (float Side : {-1.0f, 1.0f})
	{
		DoorLeaf(F, FVector(1000.0f + Side * 50.0f, CorrY, 0.0f), -90.0f, 100.0f, Side < 0.0f ? TEXT("QUAY SUITE") : FString(), false, false, TEXT("Quay Suite"),
			TEXT("Quay Suite: no event tonight. Set for a breakfast meeting at 07:30."), TEXT("ballroom"), Side > 0.0f, true);
		DoorLeaf(F, FVector(1000.0f + Side * 50.0f, -CorrY, 0.0f), 90.0f, 100.0f, Side < 0.0f ? TEXT("HARBOUR ROOMS") : FString(), false, false, TEXT("Harbour Rooms"),
			TEXT("Harbour Rooms 1-3: meeting rooms, locked overnight. Housekeeping resets them at 06:00."), TEXT("ballroom"), Side < 0.0f, true);
	}
	Sofa(FVector(450.0f, 95.0f, Zf(F, 0.0f)), -90.0f);
	Sofa(FVector(1600.0f, -95.0f, Zf(F, 0.0f)), 90.0f);
	Plant(FVector(80.0f, -100.0f, Zf(F, 0.0f)), true);
	Plant(FVector(2020.0f, 110.0f, Zf(F, 0.0f)), false);
	const FVector Screen(1400.0f, CorrY - Wall * 0.5f - 1.0f, Zf(F, 170.0f));
	AddBox(Screen, FVector(110.0f, 2.0f, 64.0f), Mat(TEXT("MI_SuitNavy")), 0.0f, false); // not MI_ScreenPanel: that is the fire panel's display (walk 16)
	AddLabel(TEXT("TONIGHT"), Screen + FVector(0, -1.4f, 20.0f), -90.0f, 4.0f, Cream, F);
	AddLabel(TEXT("Harbour Ballroom: private event"), Screen + FVector(0, -1.4f, 4.0f), -90.0f, 3.0f, Cream, F);
	AddLabel(TEXT("Quay Suite: breakfast 07:30"), Screen + FVector(0, -1.4f, -8.0f), -90.0f, 2.6f, Cream, F);
	for (float X = 300.0f; X < CoreX0; X += 450.0f) CeilingLight(F, FVector(X, 0.0f, Zf(F, CeilFoyer)), DownLumens, Warm);
	Sconce(F, FVector(600.0f, -CorrY + Wall * 0.5f + 1.0f, Zf(F, 200.0f)), 90.0f);
	SmokeDetector(F, FVector2D(700.0f, 0.0f), CeilFoyer);
	SmokeDetector(F, FVector2D(1600.0f, 0.0f), CeilFoyer);
	CctvDome(F, FVector(1800.0f, 0.0f, Zf(F, CeilFoyer)), TEXT("1-03"));
	FireExtinguisher(F, FVector(1300.0f, -CorrY + Wall * 0.5f, 0.0f), 90.0f);

	// The Harbour Ballroom: ten rounds of eight, a dance floor, the stage, a portable bar.
	const float X0 = CoreX1, X1 = MaxX - 15.0f, Y0 = ServiceY + Wall * 0.5f, Y1 = HalfY - 15.0f;
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_BallroomCarpet"))); // patterned: plain grey read as an office
	Ceiling(F, FBox2D(FVector2D(X0, Y0), FVector2D(MaxX, HalfY)), CeilBallroom, Mat(TEXT("MI_CeilingWhite")));
	// The west wall is the core's face, lined; the doors from the lift lobby stand open.
	AddBox(FVector(X0 + 0.5f, (Y0 - 110.0f) * 0.5f, Zf(F, CeilBallroom * 0.5f)), FVector(1.0f, -110.0f - Y0, CeilBallroom), Plaster);
	AddBox(FVector(X0 + 0.5f, (110.0f + Y1) * 0.5f, Zf(F, CeilBallroom * 0.5f)), FVector(1.0f, Y1 - 110.0f, CeilBallroom), Plaster);
	AddBox(FVector(X0 + 0.5f, 0.0f, Zf(F, (DoorH + CeilBallroom) * 0.5f)), FVector(Wall, 220.0f, CeilBallroom - DoorH), Plaster);
	for (float Side : {-1.0f, 1.0f})
	{
		DoorLeaf(F, FVector(X0, Side * 55.0f, 0.0f), 180.0f, 110.0f, Side < 0.0f ? TEXT("HARBOUR BALLROOM") : FString(), false, true, FString(), FString(), FString(), Side < 0.0f, true);
	}
	AddBox(FVector(X1 - 0.5f, (Y0 + Y1) * 0.5f, Zf(F, CeilBallroom * 0.5f)), FVector(1.0f, Y1 - Y0, CeilBallroom), Plaster, 0.0f, false);
	FloorFinish(F, FBox2D(FVector2D(4150.0f, -350.0f), FVector2D(4550.0f, 350.0f)), Mat(TEXT("MI_FloorParquet")), 1.0f);
	// The stage as an event riser: black skirt, a carpeted deck with a dark nosing, two steps up at the front; a black
	// drape behind with the event screen framed on it; a lectern with a sloped desk and a microphone. (A walnut box, a
	// sheer and a box lectern read as placeholders, walk 14.)
	AddBox(FVector(4672.0f, 0.0f, Zf(F, 29.0f)), FVector(226.0f, 800.0f, 58.0f), Mat(TEXT("MI_FabricDark")));
	AddBox(FVector(4672.0f, 0.0f, Zf(F, 59.0f)), FVector(226.0f, 800.0f, 2.0f), Mat(TEXT("MI_Carpet")), 0.0f, false);
	AddBox(FVector(4560.0f, 0.0f, Zf(F, 59.0f)), FVector(4.0f, 804.0f, 3.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(4529.0f, -250.0f, Zf(F, 10.0f)), FVector(60.0f, 120.0f, 20.0f), Mat(TEXT("MI_FabricDark")));
	AddBox(FVector(4544.0f, -250.0f, Zf(F, 20.0f)), FVector(30.0f, 120.0f, 40.0f), Mat(TEXT("MI_FabricDark")));
	for (const FVector2D& T : {FVector2D(4514.0f, 20.5f), FVector2D(4544.0f, 40.5f)})
	{
		AddBox(FVector(T.X, -250.0f, Zf(F, T.Y)), FVector(30.0f, 120.0f, 1.0f), Mat(TEXT("MI_Carpet")), 0.0f, false);
	}
	AddBox(FVector(X1 - 4.0f, 0.0f, Zf(F, 210.0f)), FVector(4.0f, 820.0f, 300.0f), Mat(TEXT("MI_FabricDark")), 0.0f, false);
	AddBox(FVector(X1 - 8.0f, 0.0f, Zf(F, 215.0f)), FVector(3.0f, 424.0f, 244.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	AddBox(FVector(X1 - 9.8f, 0.0f, Zf(F, 215.0f)), FVector(0.6f, 404.0f, 228.0f), Mat(TEXT("MI_SuitNavy")), 0.0f, false); // the fire panel's display showed here (walk 16)
	AddLabel(TEXT("HARBOUR BALLROOM"), FVector(X1 - 10.4f, 0.0f, Zf(F, 240.0f)), 180.0f, 30.0f, Cream, F);
	AddLabel(TEXT("Private event"), FVector(X1 - 10.4f, 0.0f, Zf(F, 196.0f)), 180.0f, 16.0f, Cream, F);
	{
		const FVector L(4620.0f, 260.0f, Zf(F, 60.0f));
		AddBox(L + FVector(0, 0, 2.0f), FVector(44.0f, 52.0f, 4.0f), Mat(TEXT("MI_Graphite")));
		AddBox(L + FVector(0, 0, 55.0f), FVector(34.0f, 44.0f, 102.0f), Mat(TEXT("MI_WoodWalnut")));
		AddInstance(Cube, FTransform(FRotator(-15.0f, 0.0f, 0.0f), L + FVector(0, 0, 110.0f), FVector(0.48f, 0.56f, 0.04f)), Mat(TEXT("MI_WoodWalnut")), false, false);
		AddBox(L + FVector(-17.4f, 0, 80.0f), FVector(0.6f, 28.0f, 12.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
		AddInstance(Cylinder, FTransform(FRotator(-35.0f, 0.0f, 0.0f), L + FVector(-14.0f, -14.0f, 124.0f), FVector(0.006f, 0.006f, 0.3f)), Mat(TEXT("MI_Graphite")), false, false);
		AddInstance(Sphere, L + FVector(-22.0f, -14.0f, 136.0f), 0.0f, FVector(0.025f, 0.025f, 0.04f), Mat(TEXT("MI_DeviceBlack")), false);
	}
	struct FRound { float X, Y; };
	TArray<FRound> Rounds;
	// Aisles of 1.1 m or more between the chair backs of neighbouring rounds, and 1.8 m clear of the service
	// doors in the south wall (the rounds of 1.0.0 stood 3.1-3.5 m apart and closed every way through).
	// The doors from the lift lobby (X 2700, Y -110..110) open onto the centre aisle, not a round.
	for (float X : {2980.0f, 3380.0f, 3780.0f}) for (float Y : {-430.0f, -20.0f, 390.0f}) if (X > 3000.0f || Y != -20.0f) Rounds.Add({X, Y});
	for (float Y : {-590.0f, 590.0f}) Rounds.Add({4350.0f, Y});
	for (const FRound& R : Rounds)
	{
		// A floor-length ivory cloth in one material from the top to the hem, soft folds round its skirt and the hem on
		// the carpet. Box pleats read as a sawtooth (walk 13), a white drum alone as a plain cylinder (walk 14), a 25 cm
		// overlay as a disc (walk 16), white under taupe as a blown drum with a hard ring (walk 17), and a champagne
		// overlay over an ivory skirt as a lumpy plate rim with a hard step (walk 18).
		FRandomStream Drape(static_cast<int32>(R.X * 3.0f + R.Y));
		UMaterialInterface* Cloth = Mat(TEXT("MI_TableLinen"));
		AddInstance(Cylinder, FVector(R.X, R.Y, Zf(F, 37.5f)), 0.0f, FVector(1.8f, 1.8f, 0.75f), Cloth, true);
		AddInstance(Cylinder, FVector(R.X, R.Y, Zf(F, 1.0f)), 0.0f, FVector(1.88f, 1.88f, 0.02f), Cloth, false);
		for (int32 k = 0; k < 32; ++k)
		{
			const FVector Dir = FRotator(0.0f, k * 11.25f + Drape.FRandRange(-2.0f, 2.0f), 0.0f).Vector();
			AddInstance(Cylinder, FVector(R.X, R.Y, Zf(F, 37.0f)) + Dir * Drape.FRandRange(89.0f, 90.5f), 0.0f, FVector(0.06f, 0.06f, 0.72f), Cloth, false);
		}
		// The table edge rounded where the cloth breaks over it.
		AddInstance(Cylinder, FVector(R.X, R.Y, Zf(F, 73.0f)), 0.0f, FVector(1.84f, 1.84f, 0.04f), Cloth, false);
		if (!AddInstance(Model(TEXT("potted_plant_04")), FVector(R.X, R.Y, Zf(F, 75.0f)), R.X + R.Y, FVector(1.35f)))
		{
			AddInstance(Sphere, FVector(R.X, R.Y, Zf(F, 90.0f)), 0.0f, FVector(0.34f, 0.34f, 0.28f), Mat(TEXT("MI_Blossom")), false);
		}
		for (float D : {-22.0f, 22.0f}) AddInstance(Cylinder, FVector(R.X + D, R.Y - D, Zf(F, 86.0f)), 0.0f, FVector(0.04f, 0.04f, 0.22f), Mat(TEXT("MI_Paper")), false);
		for (int32 k = 0; k < 8; ++k)
		{
			const float A = k * 45.0f;
			const FVector Dir = FRotator(0.0f, A, 0.0f).Vector();
			Chair(FVector(R.X, R.Y, Zf(F, 0.0f)) + Dir * 126.0f, A + 180.0f, TEXT("dining_chair_02")); // at 115-120 the seats pushed into the cloth
			// The place: a plate, a folded napkin on it, a glass to the right.
			const FVector Place = FVector(R.X, R.Y, Zf(F, 75.6f)) + Dir * 66.0f;
			AddInstance(Cylinder, Place - FVector(0, 0, 0.2f), 0.0f, FVector(0.32f, 0.32f, 0.008f), Mat(TEXT("MI_Brass")), false); // charger
			AddInstance(Cylinder, Place, 0.0f, FVector(0.27f, 0.27f, 0.01f), Mat(TEXT("MI_Ceramic")), false);
			AddBox(Place + FVector(0, 0, 1.2f), FVector(10.0f, 16.0f, 1.6f), Mat(TEXT("MI_FabricAccent")), A, false);
			AddInstance(Cylinder, Place + FRotator(0.0f, A + 90.0f, 0.0f).Vector() * 18.0f - Dir * 12.0f + FVector(0, 0, 7.0f), 0.0f, FVector(0.06f, 0.06f, 0.14f), Mat(TEXT("MI_Glass")), false);
		}
	}
	// Drapes at every other mullion of the harbour windows under a walnut pelmet (it hides the slab edge), and
	// sconces on the south wall between the service doors: the room read as bare walls in walk 12.
	for (int32 i = 0; i <= 14; i += 2)
	{
		const float Px = CoreX1 + (MaxX - CoreX1) * i / 14.0f;
		const float Cx = FMath::Clamp(Px, X0 + 25.0f, X1 - 25.0f);
		AddBox(FVector(Cx, HalfY - 26.0f, Zf(F, 172.0f)), FVector(46.0f, 8.0f, 344.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
	}
	AddBox(FVector((X0 + X1) * 0.5f, HalfY - 24.0f, Zf(F, CeilBallroom - 28.0f)), FVector(X1 - X0, 14.0f, 56.0f), Mat(TEXT("MI_WoodWalnut")), 0.0f, false);
	for (float X : {3300.0f, 3900.0f, 4300.0f}) Sconce(F, FVector(X, ServiceY + Wall * 0.5f, Zf(F, 230.0f)), 90.0f);
	AddBox(FVector((X0 + X1) * 0.5f, ServiceY + Wall * 0.5f + 1.0f, Zf(F, 95.0f)), FVector(X1 - X0, 2.0f, 5.0f), Mat(TEXT("MI_WoodWalnut")), 0.0f, false); // dado rail
	// The portable bar by the doors (the bartenders draw from the store and return to it, M017).
	AddBox(FVector(3000.0f, 900.0f, Zf(F, 55.0f)), FVector(320.0f, 60.0f, 110.0f), Mat(TEXT("MI_WoodWalnut")));
	AddBox(FVector(3000.0f, 900.0f, Zf(F, 111.0f)), FVector(330.0f, 66.0f, 3.0f), Mat(TEXT("MI_StoneCounter")), 0.0f, false);
	for (int32 i = 0; i < 8; ++i)
	{
		static const TCHAR* const B[] = {TEXT("MI_BottleGreen"), TEXT("MI_BottleAmber"), TEXT("MI_Glass")};
		AddInstance(Cylinder, FVector(2880.0f + i * 34.0f, 950.0f, Zf(F, 126.0f)), 0.0f, FVector(0.07f, 0.07f, 0.28f), Mat(B[i % 3]), false);
	}
	SpawnCasePoint(TEXT("ballroom"), FString(), TEXT("Event order, Harbour Ballroom"), FVector(3080.0f, 890.0f, Zf(F, 113.0f)), 8.0f, Cube, FVector(0.22f, 0.3f, 0.02f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	// Chandeliers: glowing drums on chains, one light each.
	for (float X : {3175.0f, 3875.0f})
	{
		for (float Y : {-400.0f, 400.0f})
		{
			AddInstance(Cylinder, FVector(X, Y, Zf(F, CeilBallroom - 25.0f)), 0.0f, FVector(0.02f, 0.02f, 0.5f), Mat(TEXT("MI_Brass")), false);
			// A linen drum with brass bands and a glowing diffuser underneath (fully glowing drums blew out, walk 14).
			AddInstance(Cylinder, FVector(X, Y, Zf(F, CeilBallroom - 70.0f)), 0.0f, FVector(1.1f, 1.1f, 0.4f), Mat(TEXT("MI_Linen")), false);
			for (float Z : {-51.0f, -89.0f}) AddInstance(Cylinder, FVector(X, Y, Zf(F, CeilBallroom + Z)), 0.0f, FVector(1.12f, 1.12f, 0.02f), Mat(TEXT("MI_Brass")), false);
			AddInstance(Cylinder, FVector(X, Y, Zf(F, CeilBallroom - 90.3f)), 0.0f, FVector(1.04f, 1.04f, 0.004f), Mat(TEXT("MI_LampWarm")), false);
			AddLight(F, FVector(X, Y, Zf(F, CeilBallroom - 100.0f)), 3000.0f, 1100.0f, Warm);
		}
	}
	CeilingLight(F, FVector(4500.0f, 0.0f, Zf(F, CeilBallroom)), DownLumens * 1.5f, Warm, 900.0f);
	// Downlights round the edge: the doors' end and the walls fell dark between the chandeliers.
	for (float X : {3000.0f, 3550.0f, 4100.0f}) for (float Y : {-620.0f, 820.0f}) CeilingLight(F, FVector(X, Y, Zf(F, CeilBallroom)), DownLumens, Warm, 700.0f);
	CeilingLight(F, FVector(2950.0f, 0.0f, Zf(F, CeilBallroom)), DownLumens, Warm, 700.0f);
	for (float X : {3200.0f, 4200.0f}) for (float Y : {-400.0f, 500.0f}) SmokeDetector(F, FVector2D(X, Y), CeilBallroom);
	CctvDome(F, FVector(2800.0f, 900.0f, Zf(F, CeilBallroom)), TEXT("1-01"));
	ExitSign(F, FVector(X0 + 2.0f, 0.0f, Zf(F, DoorH + 30.0f)), 0.0f);
	FireExtinguisher(F, FVector(X0 + 1.0f, -300.0f, 0.0f), 0.0f);

	// Banquet back of house: the service corridor from the kitchen lift, the banquet office, the bar store.
	const float SY0 = -HalfY + 15.0f, SY1 = ServiceY - Wall * 0.5f;
	WallX(F, ServiceY, X0, X1, {FVector2D(3000.0f, 120.0f), FVector2D(3600.0f, 120.0f)}, Plaster, CeilBallroom);
	for (float X : {3000.0f, 3600.0f})
	{
		DoorLeaf(F, FVector(X, ServiceY, 0.0f), -90.0f, 120.0f, FString(), false, true);
		ExitSign(F, FVector(X, ServiceY + Wall * 0.5f + 1.5f, Zf(F, DoorH + 22.0f)), -90.0f);
	}
	FloorFinish(F, FBox2D(FVector2D(X0, SY0), FVector2D(X1, SY1)), Mat(TEXT("MI_FloorVinyl")));
	Ceiling(F, FBox2D(FVector2D(X0, SY0 - 15.0f), FVector2D(MaxX, SY1)), CeilBack, Mat(TEXT("MI_CeilingSoft")));
	AddBox(FVector(X0 + 0.5f, (SY0 + SY1) * 0.5f, Zf(F, CeilBack * 0.5f)), FVector(1.0f, SY1 - SY0, CeilBack), Paint, 0.0f, false);
	// The kitchen service lift (it serves the main kitchen; not one you ride tonight).
	for (float Side : {-1.0f, 1.0f}) AddBox(FVector(X0 + 2.0f, -870.0f + Side * 30.0f, Zf(F, 105.0f)), FVector(3.0f, 58.0f, 210.0f), Steel, 0.0f, false);
	AddBox(FVector(X0 + 1.5f, -870.0f, Zf(F, 225.0f)), FVector(1.0f, 48.0f, 7.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
	AddLabel(TEXT("SERVICE LIFT  KITCHEN"), FVector(X0 + 2.4f, -870.0f, Zf(F, 225.0f)), 0.0f, 2.6f, Ink, F);
	const float OfficeX = 3900.0f, StoreX = 4350.0f;
	WallY(F, OfficeX, SY0, SY1, {FVector2D(-870.0f, 100.0f)}, Paint, CeilBack);
	WallY(F, StoreX, SY0, SY1, {FVector2D(-870.0f, 100.0f)}, Paint, CeilBack);
	DoorLeaf(F, FVector(OfficeX, -870.0f, 0.0f), 180.0f, 100.0f, TEXT("BANQUET OFFICE"), false, true);
	DoorLeaf(F, FVector(StoreX, -870.0f, 0.0f), 180.0f, 100.0f, TEXT("BAR STORE"), false, true);
	// The corridor: banquet trolleys, stacked chairs, a plate warmer.
	for (int32 i = 0; i < 3; ++i) AddBox(FVector(3150.0f + i * 160.0f, SY0 + 40.0f, Zf(F, 50.0f)), FVector(120.0f, 60.0f, 100.0f), Steel, 0.0f, true);
	for (int32 h = 0; h < 6; ++h) AddBox(FVector(3700.0f, SY0 + 40.0f, Zf(F, 10.0f + h * 18.0f)), FVector(46.0f, 46.0f, 4.0f), Mat(TEXT("MI_FabricAccent")), h * 2.0f, false);
	AddBox(FVector(3700.0f, SY0 + 40.0f, Zf(F, 55.0f)), FVector(40.0f, 40.0f, 110.0f), Mat(TEXT("MI_Graphite")), 0.0f, true);
	AddBox(FVector(3350.0f, SY1 - 30.0f, Zf(F, 80.0f)), FVector(100.0f, 50.0f, 160.0f), Steel, 0.0f, true);
	AddLabel(TEXT("PLATE WARMER"), FVector(3350.0f, SY1 - 56.0f, Zf(F, 140.0f)), -90.0f, 2.0f, Ink, F);
	for (float X : {3100.0f, 3700.0f})
	{
		AddBox(FVector(X, -870.0f, Zf(F, CeilBack - 3.0f)), FVector(124.0f, 14.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
		AddLight(F, FVector(X, -870.0f, Zf(F, CeilBack - 30.0f)), 700.0f, 600.0f, Cool);
	}
	CctvDome(F, FVector(3300.0f, -870.0f, Zf(F, CeilBack)), TEXT("1-02"));
	SmokeDetector(F, FVector2D(3500.0f, -870.0f), CeilBack);
	// The banquet office: Celia's desk, the event sheets, the roster on the wall.
	Desk(FVector(4125.0f, SY0 + 45.0f, Zf(F, 0.0f)), 0.0f, FVector(200.0f, 75.0f, DeskH), Mat(TEXT("MI_WoodOak")));
	Monitor(FVector(4080.0f, SY0 + 30.0f, Zf(F, DeskH)), 90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
	TaskChair(FVector(4125.0f, SY0 + 110.0f, Zf(F, 0.0f)), -90.0f); // tucked in: the way to the bar store passes behind it
	SpawnCasePoint(TEXT("ballroom"), TEXT("banquet_office"), FString(), FVector(4170.0f, SY0 + 50.0f, Zf(F, DeskH + 3.0f)), 4.0f, Cube, FVector(0.24f, 0.32f, 0.05f),
		Mat(TEXT("MI_SuitNavy")), EKcUseStyle::PickUp);
	AddBox(FVector(4125.0f, SY1 - 1.0f, Zf(F, 160.0f)), FVector(120.0f, 1.0f, 70.0f), Mat(TEXT("MI_Whiteboard")), 0.0f, false);
	AddLabel(TEXT("BANQUET ROSTER"), FVector(4125.0f, SY1 - 1.7f, Zf(F, 186.0f)), -90.0f, 2.8f, Ink, F);
	CeilingLight(F, FVector(4125.0f, -870.0f, Zf(F, CeilBack)), DownLumens, Neutral, 400.0f);
	// The bar store: shelves of spirits and cases, no par levels, no camera on the door.
	for (int32 r = 0; r < 2; ++r)
	{
		const float Y = SY0 + 25.0f + r * 150.0f;
		for (float Z : {5.0f, 55.0f, 105.0f, 155.0f}) AddBox(FVector(4570.0f, Y, Zf(F, Z)), FVector(400.0f, 40.0f, 2.0f), Steel, 0.0f, false);
		for (float X : {4380.0f, 4760.0f}) AddBox(FVector(X, Y, Zf(F, 85.0f)), FVector(3.0f, 40.0f, 170.0f), Steel, 0.0f, true);
		for (int32 i = 0; i < 12; ++i)
		{
			static const TCHAR* const B[] = {TEXT("MI_BottleAmber"), TEXT("MI_BottleGreen"), TEXT("MI_Glass")};
			AddInstance(Cylinder, FVector(4400.0f + i * 30.0f, Y, Zf(F, 70.0f + (i % 2) * 50.0f)), 0.0f, FVector(0.08f, 0.08f, 0.28f), Mat(B[(i + r) % 3]), false);
		}
		AddBox(FVector(4520.0f, Y, Zf(F, 22.0f)), FVector(40.0f, 30.0f, 30.0f), Mat(TEXT("MI_Cardboard")), 0.0f, false);
		AddBox(FVector(4640.0f, Y, Zf(F, 22.0f)), FVector(40.0f, 30.0f, 30.0f), Mat(TEXT("MI_Cardboard")), 6.0f, false);
	}
	SpawnCasePoint(TEXT("ballroom"), TEXT("banquet_bar"), FString(), FVector(4570.0f, SY0 + 175.0f, Zf(F, 108.0f)), 0.0f, Cube, FVector(0.24f, 0.16f, 0.03f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::Screen, FVector(4570.0f, -820.0f, Zf(F, 120.0f)));
	AddBox(FVector(StoreX - Wall * 0.5f - 1.0f, -800.0f, Zf(F, 110.0f)), FVector(1.0f, 12.0f, 8.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
	CeilingLight(F, FVector(4570.0f, -870.0f, Zf(F, CeilBack)), DownLumens * 0.8f, Cool, 400.0f);
}

// ---------------------------------------------------------------------------- 4: executive floor

void AKcHotel::BuildExecutiveFloor()
{
	// Floor 4: the executive offices with HR and finance off a private passage, the GM's office and the
	// boardroom across the corridor; sales and revenue east of the core, closed tonight (M016-M019).
	const int32 F = 4;
	const float Ceil = CeilExec;
	UMaterialInterface* Paper = Mat(TEXT("MI_Wallpaper"));
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	UMaterialInterface* Carpet = Mat(TEXT("MI_Carpet"));
	FloorFinish(F, FBox2D(FVector2D(15.0f, -CorrY), FVector2D(MaxX - 15.0f, CorrY)), Mat(TEXT("MI_CorridorCarpet")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -CorrY), FVector2D(CoreX0, CorrY)), Ceil, Mat(TEXT("MI_CeilingWhite")));
	Ceiling(F, FBox2D(FVector2D(CoreX1, -CorrY), FVector2D(MaxX, CorrY)), Ceil, Mat(TEXT("MI_CeilingWhite")));
	WallX(F, CorrY, 15.0f, CoreX0, {FVector2D(1700.0f, 180.0f)}, Paper, Ceil);
	WallX(F, CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3700.0f, 100.0f)}, Paper, Ceil);
	WallX(F, -CorrY, 15.0f, CoreX0, {FVector2D(400.0f, 100.0f), FVector2D(1450.0f, 180.0f)}, Paper, Ceil);
	WallX(F, -CorrY, CoreX1, MaxX - 15.0f, {FVector2D(3700.0f, 100.0f)}, Paper, Ceil);
	for (float Side : {-1.0f, 1.0f})
	{
		DoorLeaf(F, FVector(1700.0f + Side * 45.0f, CorrY, 0.0f), -90.0f, 90.0f, Side < 0.0f ? TEXT("EXECUTIVE OFFICES") : FString(), false, true, FString(), FString(), FString(), Side > 0.0f, true);
		DoorLeaf(F, FVector(1450.0f + Side * 45.0f, -CorrY, 0.0f), 90.0f, 90.0f, Side < 0.0f ? TEXT("BOARDROOM") : FString(), false, true, FString(), FString(), FString(), Side < 0.0f, true);
	}
	DoorLeaf(F, FVector(400.0f, -CorrY, 0.0f), 90.0f, 100.0f, TEXT("GENERAL MANAGER"), false, true);
	DoorLeaf(F, FVector(3700.0f, CorrY, 0.0f), -90.0f, 100.0f, TEXT("SALES AND EVENTS"), false, false, TEXT("Sales and events"),
		TEXT("Sales and events: dark until the team arrives at 08:30."), TEXT("exec_offices"));
	DoorLeaf(F, FVector(3700.0f, -CorrY, 0.0f), 90.0f, 100.0f, TEXT("REVENUE AND IT"), false, false, TEXT("Revenue and IT"),
		TEXT("Revenue and IT: the server room is on B1. Nobody here until morning."), TEXT("exec_offices"));
	for (float X = 300.0f; X < MaxX; X += 600.0f)
	{
		if (X > CoreX0 - 100.0f && X < CoreX1 + 100.0f) continue;
		CeilingLight(F, FVector(X, 0.0f, Zf(F, Ceil)), DownLumens, Warm);
	}
	for (float X : {800.0f, 3500.0f}) SmokeDetector(F, FVector2D(X, 0.0f), Ceil);
	CctvDome(F, FVector(1600.0f, 0.0f, Zf(F, Ceil)), TEXT("4-01"));
	ArtPrint(FVector(900.0f, CorrY - Wall * 0.5f - 1.0f, Zf(F, 160.0f)), -90.0f, TEXT("MI_ArtHarbourC"), FVector2D(120.0f, 80.0f));

	// Executive offices (north-west): reception and open plan, a passage to finance and HR.
	const float PassY = 330.0f, Y1 = HalfY - 15.0f;
	FloorFinish(F, FBox2D(FVector2D(15.0f, CorrY + Wall * 0.5f), FVector2D(CoreX0, Y1)), Carpet);
	Ceiling(F, FBox2D(FVector2D(0.0f, CorrY), FVector2D(CoreX0, HalfY)), Ceil, Mat(TEXT("MI_CeilingLow"))); // Low, not Dim: the downlights are spots now and no longer light it (round 18)
	AddBox(FVector(CoreX0 - 0.5f, (CorrY + Y1) * 0.5f, Zf(F, Ceil * 0.5f)), FVector(1.0f, Y1 - CorrY, Ceil), Paint, 0.0f, false);
	WallX(F, PassY, 15.0f, 1300.0f, {FVector2D(350.0f, 90.0f), FVector2D(1000.0f, 90.0f)}, Paint, Ceil);
	WallY(F, 700.0f, PassY + Wall * 0.5f, Y1, {}, Paint, Ceil);
	WallY(F, 1300.0f, PassY - Wall * 0.5f, Y1, {}, Paint, Ceil);
	DoorLeaf(F, FVector(350.0f, PassY, 0.0f), -90.0f, 90.0f, TEXT("FINANCE"), false, true);
	DoorLeaf(F, FVector(1000.0f, PassY, 0.0f), -90.0f, 90.0f, TEXT("HR  PLEASE KNOCK"), false, true);
	// Reception desk and the visitors' book (the floor's own case point).
	Desk(FVector(1750.0f, 420.0f, Zf(F, 0.0f)), 0.0f, FVector(220.0f, 70.0f, 100.0f), Mat(TEXT("MI_WoodWalnut")));
	SpawnCasePoint(TEXT("exec_offices"), FString(), TEXT("Executive offices reception"), FVector(1700.0f, 400.0f, Zf(F, 101.5f)), -6.0f, Cube, FVector(0.26f, 0.2f, 0.03f),
		Mat(TEXT("MI_SuitNavy")), EKcUseStyle::PickUp);
	TaskChair(FVector(1750.0f, 500.0f, Zf(F, 0.0f)), -90.0f);
	for (int32 i = 0; i < 4; ++i)
	{
		const FVector D(1500.0f + (i % 2) * 380.0f, 720.0f + (i / 2) * 170.0f, Zf(F, 0.0f));
		Desk(D, 0.0f, FVector(160.0f, 75.0f, DeskH), Mat(TEXT("MI_WoodOak")));
		Monitor(D + FVector(-20.0f, 20.0f, DeskH), -90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
		TaskChair(D + FVector(0.0f, -70.0f, 0.0f), 90.0f);
	}
	Plant(FVector(2040.0f, 230.0f, Zf(F, 0.0f)), true);
	for (float X : {1500.0f, 1880.0f}) CeilingLight(F, FVector(X, 650.0f, Zf(F, Ceil)), DownLumens, Neutral);
	CeilingLight(F, FVector(650.0f, 245.0f, Zf(F, Ceil)), DownLumens * 0.7f, Warm, 450.0f);
	// Finance: Hamish's desk, the budget pack, binders.
	Desk(FVector(350.0f, 780.0f, Zf(F, 0.0f)), 180.0f, FVector(180.0f, 80.0f, DeskH), Mat(TEXT("MI_WoodWalnut")));
	Monitor(FVector(330.0f, 760.0f, Zf(F, DeskH)), 90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
	SpawnCasePoint(TEXT("exec_offices"), TEXT("finance_office"), FString(), FVector(400.0f, 760.0f, Zf(F, DeskH + 2.0f)), 10.0f, Cube, FVector(0.22f, 0.3f, 0.04f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	Chair(FVector(350.0f, 620.0f, Zf(F, 0.0f)), 90.0f);
	Shelf(FVector(30.0f, 650.0f, Zf(F, 0.0f)), 0.0f, 160.0f, 44);
	CeilingLight(F, FVector(350.0f, 680.0f, Zf(F, Ceil)), DownLumens, Neutral, 500.0f);
	// HR interview room: a round table for four, water and tissues, nothing on the walls to read.
	// A pedestal table (a solid wooden drum read as a block and the chairs went into it, walk 12).
	AddInstance(Cylinder, FVector(1000.0f, 680.0f, Zf(F, 72.0f)), 0.0f, FVector(1.1f, 1.1f, 0.04f), Mat(TEXT("MI_WoodOak")), true);
	AddInstance(Cylinder, FVector(1000.0f, 680.0f, Zf(F, 36.0f)), 0.0f, FVector(0.14f, 0.14f, 0.68f), Mat(TEXT("MI_Graphite")), true);
	AddInstance(Cylinder, FVector(1000.0f, 680.0f, Zf(F, 1.5f)), 0.0f, FVector(0.6f, 0.6f, 0.03f), Mat(TEXT("MI_Graphite")), false);
	for (int32 k = 0; k < 4; ++k)
	{
		const float A = 45.0f + k * 90.0f;
		Chair(FVector(1000.0f, 680.0f, Zf(F, 0.0f)) + FRotator(0.0f, A, 0.0f).Vector() * 98.0f, A + 180.0f, TEXT("dining_chair_02"));
	}
	AddInstance(Cylinder, FVector(980.0f, 700.0f, Zf(F, 84.0f)), 0.0f, FVector(0.1f, 0.1f, 0.2f), Mat(TEXT("MI_Glass")), false);
	AddBox(FVector(1030.0f, 660.0f, Zf(F, 78.0f)), FVector(20.0f, 12.0f, 8.0f), Mat(TEXT("MI_Linen")), 15.0f, false);
	SpawnCasePoint(TEXT("exec_offices"), TEXT("hr_office"), FString(), FVector(1010.0f, 640.0f, Zf(F, 74.6f)), -8.0f, Cube, FVector(0.21f, 0.29f, 0.01f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	Plant(FVector(1240.0f, 930.0f, Zf(F, 0.0f)), false);
	CeilingLight(F, FVector(1000.0f, 680.0f, Zf(F, Ceil)), DownLumens, Warm, 500.0f);

	// The GM's office (south-west) and the boardroom.
	const float SY0 = -HalfY + 15.0f;
	FloorFinish(F, FBox2D(FVector2D(15.0f, SY0), FVector2D(CoreX0, -CorrY - Wall * 0.5f)), Carpet);
	Ceiling(F, FBox2D(FVector2D(0.0f, -HalfY), FVector2D(CoreX0, -CorrY)), Ceil, Mat(TEXT("MI_CeilingLow"))); // Low, not Dim: the downlights are spots now and no longer light it (round 18)
	AddBox(FVector(CoreX0 - 0.5f, (SY0 - CorrY) * 0.5f, Zf(F, Ceil * 0.5f)), FVector(1.0f, -CorrY - SY0, Ceil), Paint, 0.0f, false);
	WallY(F, 800.0f, SY0, -CorrY - Wall * 0.5f, {}, Paint, Ceil);
	Desk(FVector(400.0f, -760.0f, Zf(F, 0.0f)), 0.0f, FVector(200.0f, 90.0f, DeskH), Mat(TEXT("MI_WoodWalnut")));
	Monitor(FVector(360.0f, -735.0f, Zf(F, DeskH)), -90.0f, Mat(TEXT("MI_ScreenFrontOffice")));
	SpawnCasePoint(TEXT("gm_office"), FString(), TEXT("The GM's desk"), FVector(440.0f, -740.0f, Zf(F, DeskH + 1.5f)), 4.0f, Cube, FVector(0.22f, 0.3f, 0.02f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	TaskChair(FVector(470.0f, -850.0f, Zf(F, 0.0f)), 90.0f); // the GM's own, pushed back beside where they stand
	Chair(FVector(340.0f, -600.0f, Zf(F, 0.0f)), -90.0f);
	Chair(FVector(460.0f, -600.0f, Zf(F, 0.0f)), -90.0f);
	Sofa(FVector(110.0f, -400.0f, Zf(F, 0.0f)), 0.0f);
	Plant(FVector(740.0f, -930.0f, Zf(F, 0.0f)), true);
	ArtPrint(FVector(15.0f + 2.0f, -650.0f, Zf(F, 160.0f)), 0.0f, TEXT("MI_ArtHarbourA"), FVector2D(130.0f, 90.0f));
	Lamp(F, FVector(150.0f, -900.0f, Zf(F, 0.0f)), true);
	CeilingLight(F, FVector(400.0f, -600.0f, Zf(F, Ceil)), DownLumens, Warm, 550.0f);
	// A rug under the visitors' chairs, and a credenza with books and a photograph on the east wall.
	FloorFinish(F, FBox2D(FVector2D(220.0f, -700.0f), FVector2D(580.0f, -480.0f)), Mat(TEXT("MI_Rug")), 1.6f);
	AddBox(FVector(800.0f - Wall * 0.5f - 23.0f, -480.0f, Zf(F, 38.0f)), FVector(46.0f, 200.0f, 76.0f), Mat(TEXT("MI_WoodWalnut")), 0.0f, true);
	{
		FRandomStream B(404);
		float Y = -560.0f;
		while (Y < -470.0f)
		{
			const float T = B.FRandRange(3.0f, 6.0f), H = B.FRandRange(22.0f, 30.0f);
			AddBox(FVector(800.0f - Wall * 0.5f - 20.0f, Y + T * 0.5f, Zf(F, 76.0f + H * 0.5f)), FVector(24.0f, T, H),
				Mat(B.RandRange(0, 2) == 0 ? TEXT("MI_SuitNavy") : (B.FRand() < 0.5f ? TEXT("MI_Leather") : TEXT("MI_FabricAccent"))), 0.0f, false);
			Y += T + 0.4f;
		}
		AddBox(FVector(800.0f - Wall * 0.5f - 25.0f, -420.0f, Zf(F, 88.0f)), FVector(2.0f, 22.0f, 24.0f), Mat(TEXT("MI_Brass")), 0.0f, false);
		Lamp(F, FVector(800.0f - Wall * 0.5f - 22.0f, -395.0f, Zf(F, 76.0f)), false);
	}
	// Boardroom: a long table for twelve, the screen on the core wall.
	Desk(FVector(1450.0f, -570.0f, Zf(F, 0.0f)), 0.0f, FVector(800.0f, 160.0f, DeskH), Mat(TEXT("MI_WoodWalnut")));
	for (int32 i = 0; i < 5; ++i)
	{
		for (float Side : {-1.0f, 1.0f}) TaskChair(FVector(1130.0f + i * 160.0f, -570.0f + Side * 122.0f, Zf(F, 0.0f)), Side > 0.0f ? -90.0f : 90.0f);
	}
	// Meeting chairs, not the lobby's lounge armchairs (walk 12).
	TaskChair(FVector(1000.0f, -570.0f, Zf(F, 0.0f)), 0.0f);
	TaskChair(FVector(1900.0f, -570.0f, Zf(F, 0.0f)), 180.0f);
	AddBox(FVector(CoreX0 - 3.0f, -570.0f, Zf(F, 150.0f)), FVector(4.0f, 180.0f, 100.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false); // off for the night
	SpawnCasePoint(TEXT("boardroom"), FString(), TEXT("Boardroom"), FVector(1450.0f, -570.0f, Zf(F, DeskH + 1.5f)), 0.0f, Cube, FVector(0.22f, 0.3f, 0.02f),
		Mat(TEXT("MI_Paper")), EKcUseStyle::PickUp);
	for (float X : {1150.0f, 1750.0f}) CeilingLight(F, FVector(X, -570.0f, Zf(F, Ceil)), DownLumens, Neutral, 550.0f);
	SmokeDetector(F, FVector2D(1450.0f, -400.0f), Ceil);
	SmokeDetector(F, FVector2D(1000.0f, 800.0f), Ceil);
}

// ---------------------------------------------------------------------------- 14: the guest floor

void AKcHotel::BuildGuestFloor()
{
	const int32 F = TopFloor;
	UMaterialInterface* Paper = Mat(TEXT("MI_Wallpaper"));
	FloorFinish(F, FBox2D(FVector2D(15.0f, -CorrY), FVector2D(MaxX - 15.0f, CorrY)), Mat(TEXT("MI_CorridorCarpet")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -CorrY), FVector2D(CoreX0, CorrY)), Ceil14, Mat(TEXT("MI_CeilingWhite")));
	Ceiling(F, FBox2D(FVector2D(CoreX1, -CorrY), FVector2D(MaxX, CorrY)), Ceil14, Mat(TEXT("MI_CeilingWhite")));
	// Corridor walls with every room's door, 75 cm in from its west wall.
	TArray<FVector2D> NorthW, NorthE, SouthW, SouthE;
	SouthW.Add(FVector2D(150.0f, 90.0f)); // floor closet
	for (const FRoomBand& B : Bands)
	{
		for (int32 i = 0; i < B.Count; ++i)
		{
			const float X0 = B.X0 + i * B.W;
			const FVector2D D(RoomDoorX(X0), GuestDoorW);
			(B.bNorth ? (X0 < CoreX0 ? NorthW : NorthE) : (X0 < CoreX0 ? SouthW : SouthE)).Add(D);
		}
	}
	WallX(F, CorrY, 15.0f, CoreX0, NorthW, Paper, Ceil14);
	WallX(F, CorrY, CoreX1, MaxX - 15.0f, NorthE, Paper, Ceil14);
	WallX(F, -CorrY, 15.0f, CoreX0, SouthW, Paper, Ceil14);
	WallX(F, -CorrY, CoreX1, MaxX - 15.0f, SouthE, Paper, Ceil14);
	for (const FRoomBand& B : Bands)
	{
		for (int32 i = 0; i < B.Count; ++i)
		{
			const FString N = FString::FromInt(B.First + i);
			const float X0 = B.X0 + i * B.W;
			const FBox2D Area(FVector2D(X0, B.bNorth ? CorrY : -HalfY), FVector2D(X0 + B.W, B.bNorth ? HalfY : -CorrY));
			GuestRoom(N, Area, B.bNorth, N == TEXT("1412") || N == TEXT("1406"));
			// A sconce on every other room's wall, between its door and the next room.
			if (i % 2 == 0)
			{
				const float X = X0 + (122.5f + B.W) * 0.5f;
				const float Y = B.bNorth ? CorrY - Wall * 0.5f : -CorrY + Wall * 0.5f;
				Sconce(F, FVector(X, Y, Zf(F, 178.0f)), B.bNorth ? -90.0f : 90.0f);
			}
		}
	}
	BuildServiceCloset();
	// West end: Stair B. East end: the window over the harbour, a bench.
	DoorLeaf(F, FVector(17.0f, 0.0f, 0.0f), 0.0f, 100.0f, TEXT("STAIR B"), false, false, TEXT("Stair B"),
		TEXT("Stair B: alarmed exit door. In an emergency, use it; tonight, use Stair A by the lifts."), TEXT("f14_corridor"));
	ExitSign(F, FVector(19.0f, 0.0f, Zf(F, DoorH + 22.0f)), 0.0f);
	AddBox(FVector(MaxX - 22.0f, 0.0f, Zf(F, 41.0f)), FVector(16.0f, 2.0f * CorrY - 20.0f, 3.0f), Mat(TEXT("MI_StoneCounter")), 0.0f, false);
	for (float Side : {-1.0f, 1.0f}) AddBox(FVector(MaxX - 30.0f, Side * 135.0f, Zf(F, 130.0f)), FVector(12.0f, 40.0f, 250.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
	AddBox(FVector(MaxX - 90.0f, 0.0f, Zf(F, 22.0f)), FVector(45.0f, 140.0f, 44.0f), Mat(TEXT("MI_Leather")));
	// Fixtures: downlights, detectors, cameras 14-02 and 14-03 (14-03 covers 1412), hanging exit signs.
	for (float X = 300.0f; X < MaxX; X += 600.0f)
	{
		if (X > CoreX0 - 50.0f && X < CoreX1 + 50.0f) continue;
		CeilingLight(F, FVector(X, 0.0f, Zf(F, Ceil14)), DownLumens * 0.7f, Warm, 600.0f);
	}
	for (float X : {450.0f, 1350.0f, 3450.0f, 4350.0f}) SmokeDetector(F, FVector2D(X, 40.0f), Ceil14);
	CctvDome(F, FVector(900.0f, 0.0f, Zf(F, Ceil14)), TEXT("14-03"));
	CctvDome(F, FVector(3600.0f, 0.0f, Zf(F, Ceil14)), TEXT("14-02"));
	for (float X : {1100.0f, 3500.0f})
	{
		AddInstance(Cylinder, FVector(X, 0.0f, Zf(F, Ceil14 - 6.0f)), 0.0f, FVector(0.01f, 0.01f, 0.12f), Mat(TEXT("MI_Steel")), false);
		const float Yaw = X < CoreX0 ? 0.0f : 180.0f; // the running figure faces away from Stair A
		ExitSign(F, FVector(X, 0.0f, Zf(F, Ceil14 - 20.0f)), Yaw);
		ExitSign(F, FVector(X, 0.0f, Zf(F, Ceil14 - 20.0f)), Yaw + 180.0f);
	}
	FireExtinguisher(F, FVector(1550.0f, CorrY - Wall * 0.5f, 0.0f), -90.0f);
	FireExtinguisher(F, FVector(4330.0f, -CorrY + Wall * 0.5f, 0.0f), 90.0f);
	// A room service tray left out by 1409, and a shoe-shine bag on 1402's handle.
	const FVector Tray(RoomDoorX(2700.0f + 3 * 420.0f) + 85.0f, CorrY - 30.0f, Zf(F, 1.5f));
	AddBox(Tray, FVector(46.0f, 34.0f, 2.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
	AddInstance(Cylinder, Tray + FVector(-8.0f, 0, 1.5f), 0.0f, FVector(0.26f, 0.26f, 0.01f), Mat(TEXT("MI_Ceramic")), false);
	AddInstance(Sphere, Tray + FVector(-8.0f, 0, 2.0f), 0.0f, FVector(0.22f, 0.22f, 0.12f), Mat(TEXT("MI_Steel")), false);
	AddInstance(Cylinder, Tray + FVector(14.0f, 9.0f, 6.0f), 0.0f, FVector(0.06f, 0.06f, 0.1f), Mat(TEXT("MI_Glass")), false);
	AddBox(Tray + FVector(14.0f, -8.0f, 1.5f), FVector(14.0f, 10.0f, 1.0f), Mat(TEXT("MI_Linen")), 20.0f, false);
}

void AKcHotel::GuestRoom(const FString& Number, const FBox2D& Area, bool bNorth, bool bWalkable)
{
	const int32 F = TopFloor;
	const float X0 = Area.Min.X;
	const FVector Door(RoomDoorX(X0), bNorth ? CorrY : -CorrY, 0.0f);
	const float Yaw = bNorth ? -90.0f : 90.0f; // plate side: the corridor
	// Hinges on the east side of every door, so the open leaf clears the wardrobe.
	const bool bMirror = bNorth;
	if (Number == TEXT("1412") || Number == TEXT("1406"))
	{
		DynamicDoor(Number, F, Door, Yaw, GuestDoorW, Number, bMirror);
	}
	else if (Number == TEXT("1418"))
	{
		DoorLeaf(F, Door, Yaw, GuestDoorW, Number, true, false, FString(), FString(), FString(), bMirror);
	}
	else
	{
		const int32 H = FCString::Atoi(*Number);
		const bool bDnd = H == 1403 || H == 1416;
		const FString Msg = bDnd ? FString::Printf(TEXT("Room %s: Do Not Disturb is on. Nothing tonight needs this door."), *Number)
			: (H % 3 == 0 ? FString::Printf(TEXT("Room %s: vacant, cleaned and inspected, ready for tomorrow's arrival."), *Number)
				: FString::Printf(TEXT("Room %s: occupied. The guest is asleep, and nothing tonight needs this door."), *Number));
		DoorLeaf(F, Door, Yaw, GuestDoorW, Number, true, false, TEXT("Room ") + Number, Msg, TEXT("f14_corridor"), bMirror);
		if (bDnd)
		{
			// The hanger on the corridor lever, latch side.
			const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
			const FVector Right = FRotator(0.0f, Yaw + (bMirror ? -90.0f : 90.0f), 0.0f).Vector();
			AddBox(FVector(Door.X, Door.Y, Zf(F, 0.0f)) + Right * (GuestDoorW * 0.5f - 18.0f) + Out * 6.4f + FVector(0, 0, 88.0f), FVector(0.4f, 9.0f, 22.0f),
				Mat(TEXT("MI_DndTag")), Yaw, false);
		}
	}
	if (bWalkable) FurnishRoom(Number, Area, bNorth, FCString::Atoi(*Number));
}


void AKcHotel::FurnishRoom(const FString& Number, const FBox2D& Area, bool bNorth, int32 Seed)
{
	// Layout (U along the corridor from the west wall, V from the corridor wall, faces at V 8 and 825):
	//   entry U 0..150, V 8..250 with the wardrobe and safe; bathroom U 150..W, V 8..250;
	//   bedroom V 250..825: bed headboard on the east wall, desk and TV on the west wall, the window.
	const int32 F = TopFloor;
	FRoomFrame R;
	R.X0 = Area.Min.X;
	R.W = Area.GetSize().X;
	R.bNorth = bNorth;
	R.Zb = FloorZ(F);
	const float W = R.W;
	const float VIn = Wall * 0.5f;       // the corridor wall's room face
	const float VOut = RoomDepth - 15.0f; // the facade's inner face
	const bool b1412 = Number == TEXT("1412");
	(void)Seed;
	auto Box2 = [&](float U0, float V0, float U1, float V1)
	{
		const FVector2D A = R.P2(U0, V0), B = R.P2(U1, V1);
		return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
	};
	// A box sized along the room's axes (U is world X, V is world Y either way).
	auto LBox = [&](float U, float V, float Z, float SU, float SV, float SZ, const TCHAR* M, bool bCollide = false)
	{
		AddBox(R.P(U, V, Z), FVector(SU, SV, SZ), Mat(M), 0.0f, bCollide);
	};
	auto YRange = [&](float V0, float V1) { const float A = R.P2(0, V0).Y, B = R.P2(0, V1).Y; return FVector2D(FMath::Min(A, B), FMath::Max(A, B)); };
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));

	// Shell: party walls, the room face of the corridor wall, floors, ceilings.
	const FVector2D Full = YRange(0.0f, VOut);
	WallY(F, R.X0, Full.X, Full.Y, {}, Paint, Ceil14);
	WallY(F, R.X0 + W, Full.X, Full.Y, {}, Paint, Ceil14);
	LBox(12.0f, VIn + 0.5f, Ceil14 * 0.5f, 24.0f, 1.0f, Ceil14, TEXT("MI_WallPaint"));
	LBox((126.0f + W) * 0.5f, VIn + 0.5f, Ceil14 * 0.5f, W - 126.0f, 1.0f, Ceil14, TEXT("MI_WallPaint"));
	LBox(75.0f, VIn + 0.5f, (DoorH + 6.0f + Ceil14) * 0.5f, 102.0f, 1.0f, Ceil14 - DoorH - 6.0f, TEXT("MI_WallPaint"));
	FloorFinish(F, Box2(0.0f, VIn, 150.0f, 250.0f), Mat(TEXT("MI_FloorMarble")));
	FloorFinish(F, Box2(150.0f, VIn, W, 250.0f), Mat(TEXT("MI_FloorTile")));
	FloorFinish(F, Box2(0.0f, 250.0f, W, VOut), Mat(TEXT("MI_RoomCarpet")));
	Ceiling(F, Box2(0.0f, 0.0f, W, VOut), Ceil14, Mat(TEXT("MI_CeilingWhite")));
	Ceiling(F, Box2(150.0f, 0.0f, W, 250.0f), 240.0f, Mat(TEXT("MI_CeilingWhite")));

	// Bathroom: walls with the door off the entry, vanity and mirror, WC, shower, towels.
	const float VBath = R.P2(0, 250.0f).Y;
	WallX(F, VBath, R.X0 + 150.0f, R.X0 + W, {}, Mat(TEXT("MI_Tile")), Ceil14);
	LBox((150.0f + W) * 0.5f, 250.0f + Wall * 0.5f + 0.5f, Ceil14 * 0.5f, W - 150.0f, 1.0f, Ceil14, TEXT("MI_WallPaint")); // bedroom face
	const FVector2D BathY = YRange(VIn, 250.0f);
	WallY(F, R.X0 + 150.0f, BathY.X, BathY.Y, {FVector2D(R.P2(0, 171.25f).Y, 82.5f)}, Paint, Ceil14); // 75 cm less the leaf left no room for the player
	for (const FVector2D& Seg : {FVector2D(VIn, 130.0f), FVector2D(212.5f, 250.0f)})
	{
		LBox(150.0f + Wall * 0.5f + 0.5f, (Seg.X + Seg.Y) * 0.5f, Ceil14 * 0.5f, 1.0f, Seg.Y - Seg.X, Ceil14, TEXT("MI_Tile")); // bathroom face
	}
	// The bathroom door, as tall as its opening (a 210 leaf left a band of tile above it, walk 14): open past the jamb in
	// 1412, shut in 1406, where the open doorway framed a wall of tile from the corridor (walk 17).
	if (b1412)
	{
		LBox(192.0f, 216.0f, 108.0f, 80.0f, 4.0f, 213.0f, TEXT("MI_DoorPaint"), true);
	}
	else
	{
		LBox(150.0f, 171.25f, 108.0f, 4.0f, 80.5f, 213.0f, TEXT("MI_DoorPaint"), true);
		LBox(147.5f, 136.0f, 104.0f, 1.0f, 5.0f, 5.0f, TEXT("MI_Steel"));
		LBox(146.5f, 141.0f, 104.0f, 2.0f, 14.0f, 1.8f, TEXT("MI_Steel"));
	}
	LBox(W - 8.5f, (VIn + 250.0f) * 0.5f, Ceil14 * 0.5f, 1.0f, 250.0f - VIn, Ceil14, TEXT("MI_Tile")); // tile on the party wall too
	LBox(230.0f, VIn + 27.0f, 42.0f, 120.0f, 50.0f, 84.0f, TEXT("MI_WoodWalnut"), true);
	LBox(230.0f, VIn + 27.0f, 86.0f, 124.0f, 54.0f, 4.0f, TEXT("MI_StoneCounter"));
	// A vessel basin on the counter with a tall mixer, rolled hand towels and an amenity tray. A flat disc and a post did
	// not read as a basin at all (walk 14), a 12 cm drum read as a bucket (walk 16) and a 6 cm one as a plate (walk 17):
	// an oval bowl with a thin rim and a drain, and a matte black mixer reaching over it (chrome and steel read white).
	{
		const float BU = 230.0f, BV = VIn + 30.0f, RimA = 22.5f, RimB = 17.5f; // the rim's centre line
		const int32 N = 24;
		for (int32 k = 0; k < N; ++k)
		{
			const float T0 = 2.0f * PI * k / N, T1 = 2.0f * PI * (k + 1) / N;
			const FVector2D P0(RimA * FMath::Cos(T0), RimB * FMath::Sin(T0)), P1(RimA * FMath::Cos(T1), RimB * FMath::Sin(T1));
			const FVector2D D = P1 - P0, M = (P0 + P1) * 0.5f;
			const float SegYaw = FMath::RadiansToDegrees(FMath::Atan2(bNorth ? D.Y : -D.Y, D.X));
			AddBox(R.P(BU + M.X, BV + M.Y, 94.0f), FVector(D.Size() + 0.4f, 1.0f, 12.0f), Mat(TEXT("MI_Ceramic")), SegYaw, false);
		}
		AddInstance(Cylinder, R.P(BU, BV, 88.5f), 0.0f, FVector(0.45f, 0.35f, 0.01f), Mat(TEXT("MI_Ceramic")), false);
		UMaterialInterface* Black = Mat(TEXT("MI_Graphite"));
		AddInstance(Cylinder, R.P(BU, BV, 89.2f), 0.0f, FVector(0.05f, 0.05f, 0.004f), Black, false);
		AddInstance(Cylinder, R.P(BU, VIn + 8.0f, 88.5f), 0.0f, FVector(0.05f, 0.05f, 0.01f), Black, false);
		AddInstance(Cylinder, R.P(BU, VIn + 8.0f, 101.0f), 0.0f, FVector(0.026f, 0.026f, 0.26f), Black, false);
		AddInstance(Cylinder, FTransform(FRotator(90.0f, R.Yaw(90.0f), 0.0f), R.P(BU, VIn + 16.0f, 112.5f), FVector(0.022f, 0.022f, 0.16f)), Black, false, false);
		AddInstance(Cylinder, R.P(BU, VIn + 23.0f, 111.0f), 0.0f, FVector(0.022f, 0.022f, 0.04f), Black, false);
		LBox(BU + 4.5f, VIn + 8.0f, 110.0f, 7.0f, 1.2f, 1.2f, TEXT("MI_Graphite"));
	}
	for (const FVector2D& T : {FVector2D(266.0f, 89.5f), FVector2D(278.0f, 89.5f), FVector2D(272.0f, 99.9f)}) // hand towels, 12 cm rolls (18 cm read oversized, walk 18)
	{
		AddInstance(Cylinder, FTransform(FRotator(90.0f, R.Yaw(90.0f), 0.0f), R.P(T.X, VIn + 30.0f, T.Y), FVector(0.06f, 0.06f, 0.22f)), Mat(TEXT("MI_Linen")), false, false);
	}
	LBox(186.0f, VIn + 26.0f, 88.6f, 22.0f, 12.0f, 1.2f, TEXT("MI_Brass"));
	for (int32 k = 0; k < 3; ++k) AddInstance(Cylinder, R.P(180.0f + k * 6.0f, VIn + 26.0f, 95.0f), 0.0f, FVector(0.025f, 0.025f, 0.11f), Mat(k == 1 ? TEXT("MI_BottleAmber") : TEXT("MI_Ceramic")), false);
	LBox(230.0f, VIn + 1.0f, 160.0f, 120.0f, 1.0f, 90.0f, TEXT("MI_Mirror"));
	for (float Du : {-61.0f, 61.0f}) LBox(230.0f + Du, VIn + 1.5f, 160.0f, 2.0f, 2.0f, 92.0f, TEXT("MI_Graphite")); // a slim frame, so it reads as a mirror
	for (float Dz : {-46.0f, 46.0f}) LBox(230.0f, VIn + 1.5f, 160.0f + Dz, 124.0f, 2.0f, 2.0f, TEXT("MI_Graphite"));
	LBox(230.0f, VIn + 2.0f, 210.0f, 100.0f, 3.0f, 3.0f, TEXT("MI_LedStrip"));
	AddInstance(Cylinder, R.P(W - 150.0f, 225.0f, 20.0f), 0.0f, FVector(0.36f, 0.44f, 0.4f), Mat(TEXT("MI_Ceramic")), true);
	LBox(W - 150.0f, 241.0f, 60.0f, 40.0f, 14.0f, 40.0f, TEXT("MI_Ceramic"));
	LBox(W - 60.0f, VIn + 55.0f, 2.0f, 100.0f, 95.0f, 4.0f, TEXT("MI_Ceramic"));
	LBox(W - 110.0f, VIn + 55.0f, 105.0f, 1.0f, 95.0f, 200.0f, TEXT("MI_Glass"), true);
	LBox(W - 60.0f, VIn + 104.0f, 105.0f, 100.0f, 1.0f, 200.0f, TEXT("MI_Glass"), true);
	LBox(W - 60.0f, VIn + 4.0f, 195.0f, 22.0f, 6.0f, 2.0f, TEXT("MI_Chrome"));
	LBox(W - 9.0f, 180.0f, 120.0f, 3.0f, 60.0f, 2.0f, TEXT("MI_Chrome"));
	LBox(W - 12.0f, 180.0f, 100.0f, 5.0f, 50.0f, 40.0f, TEXT("MI_Linen"));
	CeilingLight(F, R.P(W * 0.5f + 75.0f, 130.0f, 240.0f), 500.0f, Neutral, 320.0f);
	// A narrow downlight over the basin: one downlight left the tiles black, and a point light by the mirror lit the tiles
	// above it white (walk 17).
	CeilingLight(F, R.P(230.0f, VIn + 45.0f, 240.0f), 400.0f, Warm, 300.0f, 60.0f);

	// Entry: wardrobe with the in-room safe, key-card power slot, thermostat.
	// The wardrobe (50 deep, V 150..245) stands clear of the open door's swing (V 8..97): at V 70, and
	// even at V 105, the leaf's edge and the wardrobe's corner left less than the player's 68 cm.
	LBox(2.0f, 197.5f, 115.0f, 2.0f, 95.0f, 230.0f, TEXT("MI_WoodOak"));
	for (float V : {150.0f, 245.0f}) LBox(26.0f, V, 115.0f, 50.0f, 2.0f, 230.0f, TEXT("MI_WoodOak"), true);
	LBox(26.0f, 197.5f, 229.0f, 50.0f, 95.0f, 2.0f, TEXT("MI_WoodOak"));
	LBox(26.0f, 197.5f, 76.0f, 50.0f, 91.0f, 2.0f, TEXT("MI_WoodOak"));
	LBox(26.0f, 197.5f, 4.0f, 48.0f, 93.0f, 8.0f, TEXT("MI_Skirting")); // a plinth to the floor: without it the wardrobe seemed to float (walk 13)
	LBox(26.0f, 197.5f, 180.0f, 50.0f, 91.0f, 2.0f, TEXT("MI_WoodOak"));
	LBox(26.0f, 197.5f, 170.0f, 1.0f, 85.0f, 1.0f, TEXT("MI_Chrome"));
	for (int32 i = 0; i < 4; ++i) LBox(26.0f, 200.0f + i * 12.0f, 160.0f, 36.0f, 1.0f, 18.0f, TEXT("MI_WoodWalnut"));
	LBox(51.0f, 222.0f, 115.0f, 2.0f, 46.0f, 226.0f, TEXT("MI_WoodOak"), true); // sliding door, half open
	const FVector SafeC = R.P(26.0f, 172.0f, 92.0f);
	if (b1412)
	{
		// Open and empty, as the guest left it: a brushed steel door swung 55 degrees on its hinge at the far edge, the
		// empty grey inside showing from the entry. A thin black plate at an angle read as a loose panel (walk 17), a
		// black door at 80 degrees as a drawer pulled out into the passage (walk 18).
		Safe1412 = SafeC;
		SpawnCasePoint(TEXT("room_1412"), TEXT("room_1412_safe"), FString(), SafeC, 0.0f, Cube, FVector(0.36f, 0.4f, 0.28f), Mat(TEXT("MI_DeviceBlack")),
			EKcUseStyle::Screen, SafeC + FVector(0, 0, 4.0f));
		LBox(44.2f, 172.0f, 92.0f, 0.4f, 34.0f, 22.0f, TEXT("MI_PlasticGrey"));
		// Hinge at (44, 192); the door's centre is 20 cm along it and 2 cm off its face.
		const float SafeDoorYaw = R.Yaw(55.0f);
		const FVector Face = FRotator(0.0f, SafeDoorYaw, 0.0f).Vector();
		const FVector Free = (R.P(0.819f, -0.574f, 0.0f) - R.P(0.0f, 0.0f, 0.0f)).GetSafeNormal();
		const FVector SafeDoor = R.P(61.53f, 182.17f, 92.0f);
		AddBox(SafeDoor, FVector(4.0f, 40.0f, 28.0f), Mat(TEXT("MI_Steel")), SafeDoorYaw, false);
		AddBox(SafeDoor + Face * 2.3f - Free * 4.0f + FVector(0, 0, 4.0f), FVector(0.6f, 12.0f, 9.0f), Mat(TEXT("MI_DeviceBlack")), SafeDoorYaw, false);
		AddBox(SafeDoor + Face * 2.7f - Free * 4.0f + FVector(0, 0, 6.5f), FVector(0.3f, 8.0f, 2.0f), Mat(TEXT("MI_LedGreen")), SafeDoorYaw, false);
		AddBox(SafeDoor + Face * 3.0f + Free * 14.0f, FVector(1.2f, 2.0f, 10.0f), Mat(TEXT("MI_Chrome")), SafeDoorYaw, false);
	}
	else
	{
		AddBox(SafeC, FVector(36.0f, 40.0f, 28.0f), Mat(TEXT("MI_DeviceBlack")));
		LBox(44.3f, 180.0f, 96.0f, 0.6f, 12.0f, 9.0f, TEXT("MI_Graphite")); // keypad and handle, or it read as a black box
		LBox(44.6f, 180.0f, 98.0f, 0.3f, 8.0f, 2.0f, TEXT("MI_LedGreen"));
		LBox(44.6f, 160.0f, 92.0f, 1.2f, 2.0f, 10.0f, TEXT("MI_Chrome"));
		LBox(48.5f, 110.0f, 98.0f, 0.4f, 8.0f, 3.0f, TEXT("MI_LedRed"));
	}
	// Power slot and thermostat on the latch-side wall (the open leaf hides the hinge side).
	// (The party wall is centred on U 0, so its room face is at U 8: fixtures sit on that, not in the wall.)
	LBox(9.0f, 45.0f, 120.0f, 2.0f, 9.0f, 12.0f, TEXT("MI_PlasticGrey"));
	LBox(10.2f, 45.0f, 126.0f, 0.4f, 1.0f, 0.6f, TEXT("MI_LedGreen"));
	LBox(9.9f, 45.0f, 120.0f, 0.5f, 5.5f, 8.0f, TEXT("MI_Paper")); // a room key in the slot
	LBox(9.0f, 75.0f, 142.0f, 2.0f, 9.0f, 9.0f, TEXT("MI_Paper"));
	CeilingLight(F, R.P(75.0f, 130.0f, Ceil14), 520.0f, Warm, 400.0f);

	// Bedroom: the bed against the east wall, nightstands and lamps, art over the headboard.
	const float BedV = 540.0f;
	Bed(R.P(W - 8.0f, BedV, 0.0f), R.Yaw(180.0f));
	// The headboard's wash from under its cornice: lit only by the nightstand lamps and the room's downlight, the middle of
	// the headboard read black (walk 17).
	AddLight(F, R.P(W - 30.0f, BedV, 137.0f), 70.0f, 260.0f, Warm, false, true, FVector2D(170.0f, 2.0f));
	for (float Dv : {-115.0f, 115.0f})
	{
		LBox(W - 30.0f, BedV + Dv, 27.5f, 42.0f, 46.0f, 55.0f, TEXT("MI_WoodWalnut"), true);
		Lamp(F, R.P(W - 30.0f, BedV + Dv, 55.0f), false);
	}
	LBox(W - 42.0f, BedV + 128.0f, 58.0f, 9.0f, 14.0f, 6.0f, TEXT("MI_DeviceBlack"));
	LBox(W - 46.6f, BedV + 128.0f, 58.5f, 0.3f, 9.0f, 2.5f, TEXT("MI_LedAmber"));
	LBox(W - 40.0f, BedV - 128.0f, 57.0f, 16.0f, 18.0f, 4.0f, TEXT("MI_DeviceBlack"));
	ArtPrint(R.P(W - 8.0f, BedV, 186.0f), R.Yaw(180.0f), TEXT("MI_ArtHarbourA"), FVector2D(110.0f, 64.0f)); // above the headboard's cornice
	// Desk on the west wall, the TV and dresser beside it, a chair.
	Desk(R.P(28.0f, 510.0f, 0.0f), 90.0f, FVector(160.0f, 55.0f, DeskH), Mat(TEXT("MI_WoodWalnut"))); // oak read as orange pine at this size // modesty panel to the wall
	Chair(R.P(76.0f, 505.0f, 0.0f), R.Yaw(194.0f), TEXT("dining_chair_02")); // tucked in and turned a little: from the doorway it overlapped the armchair (walks 16-17)
	LBox(30.0f, 455.0f, 63.0f, 50.0f, 40.0f, 16.0f, TEXT("MI_WoodWalnut")); // a drawer under the top: it read as a plank on legs (walk 14)
	LBox(55.6f, 455.0f, 63.0f, 1.2f, 14.0f, 1.2f, TEXT("MI_Brass"));
	Lamp(F, R.P(22.0f, 448.0f, DeskH), false);
	LBox(28.0f, 545.0f, DeskH + 0.8f, 22.0f, 30.0f, 1.6f, TEXT("MI_SuitNavy"));  // compendium
	LBox(28.0f, 470.0f, DeskH + 6.0f, 2.0f, 10.0f, 12.0f, TEXT("MI_Paper"));     // menu tent
	Desk1412 = R.P(30.0f, 515.0f, DeskH);
	LBox(28.0f, 705.0f, 40.0f, 55.0f, 130.0f, 80.0f, TEXT("MI_WoodWalnut"), true); // 130 long: at 150 the floor lamp stood in it (walk 13)
	LBox(30.0f, 705.0f, 81.0f, 52.0f, 126.0f, 2.0f, TEXT("MI_StoneCounter"));
	LBox(11.0f, 720.0f, 152.0f, 4.0f, 112.0f, 66.0f, TEXT("MI_DeviceBlack"));
	AddInstance(Cylinder, R.P(30.0f, 680.0f, 91.0f), 0.0f, FVector(0.14f, 0.14f, 0.2f), Mat(TEXT("MI_Steel")), false);
	for (float Dv : {700.0f, 714.0f}) AddInstance(Cylinder, R.P(36.0f, Dv, 86.0f), 0.0f, FVector(0.08f, 0.08f, 0.09f), Mat(TEXT("MI_Ceramic")), false);
	LBox(30.0f, 760.0f, 83.0f, 30.0f, 20.0f, 2.0f, TEXT("MI_Graphite"));
	// Window: sill, fan-coil unit, blackout curtains and sheers on the track, an armchair and floor lamp.
	const float SillEnd = b1412 ? 280.0f : W - 8.0f; // 1412's sill stops at the balcony door (it ran across it, walk 13)
	LBox((8.0f + SillEnd) * 0.5f, VOut - 7.0f, 61.5f, SillEnd - 8.0f, 14.0f, 3.0f, TEXT("MI_StoneCounter"));
	LBox(W * 0.5f - 40.0f, VOut - 18.0f, 25.0f, 100.0f, 24.0f, 50.0f, TEXT("MI_Paper"), true);
	LBox(W * 0.5f - 40.0f, VOut - 18.0f, 50.5f, 90.0f, 18.0f, 1.0f, TEXT("MI_Graphite"));
	LBox(W * 0.5f, VOut - 12.0f, 258.0f, W - 16.0f, 4.0f, 4.0f, TEXT("MI_Graphite"));
	for (float U : {42.0f, W - 42.0f}) LBox(U, VOut - 16.0f, 130.0f, 58.0f, 10.0f, 252.0f, TEXT("MI_FabricAccent"));
	const float SheerEnd = b1412 ? 280.0f : W - 72.0f; // 1412's sheer is drawn back off the balcony door
	LBox((72.0f + SheerEnd) * 0.5f, VOut - 22.0f, 130.0f, SheerEnd - 72.0f, 1.0f, 250.0f, TEXT("MI_Sheer"));
	Chair(R.P(150.0f, VOut - 75.0f, 0.0f), R.Yaw(-45.0f)); // at U 95 the desk chair ran into it (walk 14)
	// The floor lamp beside the armchair (in the corner, its pole seemed to end in the dresser top, walk 14) and the side
	// table clear of the fan-coil unit.
	Lamp(F, R.P(268.0f, VOut - 50.0f, 0.0f), true);
	AddInstance(Cylinder, R.P(225.0f, VOut - 60.0f, 52.0f), 0.0f, FVector(0.45f, 0.45f, 0.03f), Mat(TEXT("MI_WoodWalnut")), true);
	AddInstance(Cylinder, R.P(225.0f, VOut - 60.0f, 26.0f), 0.0f, FVector(0.05f, 0.05f, 0.5f), Mat(TEXT("MI_Brass")), true);
	AddInstance(Cylinder, R.P(225.0f, VOut - 60.0f, 1.0f), 0.0f, FVector(0.3f, 0.3f, 0.02f), Mat(TEXT("MI_Brass")), false);
	// Luggage rack by the bathroom wall.
	LBox(250.0f, 290.0f, 45.0f, 80.0f, 45.0f, 4.0f, TEXT("MI_WoodWalnut"));
	for (float Du : {-36.0f, 36.0f}) LBox(250.0f + Du, 290.0f, 22.0f, 3.0f, 45.0f, 44.0f, TEXT("MI_WoodWalnut"), true);
	const FVector Detector = b1412 ? R.P(225.0f, 520.0f, 0.0f) : R.P(75.0f, 300.0f, 0.0f);
	SmokeDetector(F, FVector2D(Detector.X, Detector.Y), Ceil14);
	if (!b1412) Detector1406 = FVector(Detector.X, Detector.Y, Zf(F, Ceil14 - 3.0f));
	CeilingLight(F, R.P(150.0f, 560.0f, Ceil14), DownLumens * 0.8f, Warm, 550.0f);

	if (b1412)
	{
		// Mr Whitlock's room: the bed slept in, the turndown chocolate wrapper, his suitcase on the rack,
		// the laptop charger still plugged in under the desk (M001: look and photograph before you touch).
		// The bed slept in: the duvet's head half thrown back on itself, showing its pale grey-blue underside in a wide
		// band with a rolled ridge at the fold, all clear of the runner. White rumples on the
		// white duvet vanished and read as extra pillows (walk 18). Bed-local X runs from the headboard to the foot.
		{
			const float BedYaw = R.Yaw(180.0f);
			auto BedP = [&](float X, float Y, float Z) { return R.P(W - 8.0f - X, BedV + (bNorth ? -Y : Y), Z); };
			AddBox(BedP(99.0f, 4.0f, 61.6f), FVector(46.0f, 150.0f, 1.2f), Mat(TEXT("MI_DuvetBack")), BedYaw + 4.0f, false);
			AddInstance(Cylinder, FTransform(FRotator(0.0f, BedYaw + 4.0f, 90.0f), BedP(123.0f, 4.0f, 64.0f), FVector(0.07f, 0.07f, 1.5f)), Mat(TEXT("MI_DuvetBack")), false, false);
			AddInstance(Cylinder, FTransform(FRotator(0.0f, BedYaw + 4.0f, 90.0f), BedP(126.0f, 4.0f, 65.5f), FVector(0.06f, 0.06f, 1.46f)), Mat(TEXT("MI_Linen")), false, false);
		}
		LBox(W - 32.0f, BedV - 120.0f, 55.6f, 4.0f, 4.0f, 0.4f, TEXT("MI_Brass"));
		LBox(250.0f, 290.0f, 61.0f, 70.0f, 42.0f, 26.0f, TEXT("MI_Leather"));
		LBox(8.6f, 575.0f, 30.0f, 1.0f, 8.0f, 8.0f, TEXT("MI_Paper"));
		LBox(16.0f, 575.0f, 3.0f, 12.0f, 7.0f, 3.0f, TEXT("MI_DeviceBlack"));
		LBox(10.0f, 575.0f, 52.0f, 0.6f, 0.6f, 44.0f, TEXT("MI_DeviceBlack"));
		LBox(18.0f, 562.0f, DeskH + 0.4f, 24.0f, 0.6f, 0.6f, TEXT("MI_DeviceBlack"));
		LBox(30.0f, 562.0f, DeskH + 0.6f, 2.0f, 1.2f, 1.2f, TEXT("MI_Chrome"));
		SpawnCasePoint(TEXT("room_1412"), TEXT("room_1412_desk"), FString(), R.P(40.0f, 520.0f, DeskH + 0.6f), 0.0f, Cube, FVector(0.3f, 0.36f, 0.01f),
			Mat(TEXT("MI_WoodOak")), EKcUseStyle::Screen, R.P(25.0f, 545.0f, DeskH + 5.0f));
		// The room's own case point, for work with no prop: the entry, where you stand to take it in.
		SpawnCasePoint(TEXT("room_1412"), FString(), TEXT("Room 1412"), R.P(120.0f, 300.0f, 1.0f), 0.0f, Cube, FVector(0.6f, 0.4f, 0.01f), Mat(TEXT("MI_Rug")),
			EKcUseStyle::None, R.P(250.0f, 600.0f, 90.0f));
		// The balcony door (locked) and the balcony.
		const float BX = 1085.0f;
		AddBox(FVector(BX, -HalfY, Zf(F, DoorH * 0.5f)), FVector(96.0f, 10.0f, DoorH), Mat(TEXT("MI_Graphite")), 0.0f, false);
		SpawnSign(TEXT("room_1412"), TEXT("Balcony door"),
			TEXT("The balcony door. Leave it as it is until the room has been looked at and photographed: record what you find before you touch anything."),
			FVector(BX, -HalfY, Zf(F, DoorH * 0.5f)), 90.0f, FVector(3.0f, 86.0f, DoorH - 8.0f), Mat(TEXT("MI_Glass")));
		AddBox(FVector(BX + 34.0f, -HalfY + 4.0f, Zf(F, 105.0f)), FVector(2.0f, 3.0f, 16.0f), Mat(TEXT("MI_Chrome")), 0.0f, false);
		AddBox(FVector(BX, -HalfY - 80.0f, Zf(F, -10.0f)), FVector(260.0f, 140.0f, 20.0f), Mat(TEXT("MI_Concrete")));
		AddBox(FVector(BX, -HalfY - 148.0f, Zf(F, 55.0f)), FVector(260.0f, 2.0f, 110.0f), Mat(TEXT("MI_Glass")));
		AddBox(FVector(BX, -HalfY - 148.0f, Zf(F, 111.0f)), FVector(262.0f, 5.0f, 4.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
		for (float Side : {-1.0f, 1.0f}) AddBox(FVector(BX + Side * 129.0f, -HalfY - 80.0f, Zf(F, 55.0f)), FVector(2.0f, 136.0f, 110.0f), Mat(TEXT("MI_Glass")));
		Chair(FVector(BX - 60.0f, -HalfY - 90.0f, Zf(F, 0.0f)), 60.0f, TEXT("dining_chair_02"));
		AddInstance(Cylinder, FVector(BX + 20.0f, -HalfY - 100.0f, Zf(F, 30.0f)), 0.0f, FVector(0.5f, 0.5f, 0.6f), Mat(TEXT("MI_Steel")), true);
	}
	else
	{
		// The 1406 doorway: what you do there (enter, or photograph from the threshold) is a choice.
		const FVector Threshold = R.P(75.0f, VIn + 22.0f, 0.8f);
		SpawnCasePoint(TEXT("guest_room"), TEXT("room_1406_threshold"), TEXT("Doorway of 1406"), Threshold, 0.0f, Cube, FVector(0.86f, 0.4f, 0.016f), Mat(TEXT("MI_Rug")),
			EKcUseStyle::None, Detector1406);
		// Nobody walks past the guest into the room: the doorway is held, the work is offered at its threshold.
		AddCollider(R.P(75.0f, VIn + 60.0f, 110.0f), FVector(100.0f, 10.0f, 220.0f), FRotator::ZeroRotator);
		// Just inside, on the latch-side wall: the light switch bank over the keycard slot, for work done
		// from the doorway that has no prop of its own. On the wall's face (U 8; at U 3.5 it was inside the wall).
		SpawnCasePoint(TEXT("guest_room"), FString(), TEXT("Inside 1406, by the door"), R.P(9.2f, 40.0f, 146.0f), 0.0f, Cube,
			FVector(0.012f, 0.24f, 0.12f), Mat(TEXT("MI_Ceramic")), EKcUseStyle::Screen, R.P(75.0f, VIn + 30.0f, 150.0f));
	}
}

void AKcHotel::BuildServiceCloset()
{
	const int32 F = TopFloor;
	const float X0 = 15.0f, X1 = 300.0f - Wall * 0.5f, Y0 = -HalfY + 15.0f, Y1 = -CorrY - Wall * 0.5f;
	UMaterialInterface* Paint = Mat(TEXT("MI_WallPaint"));
	DynamicDoor(TEXT("closet"), F, FVector(150.0f, -CorrY, 0.0f), 90.0f, 90.0f, TEXT("HOUSEKEEPING"));
	WallY(F, 300.0f, Y0, Y1, {}, Paint, Ceil14);
	AddBox(FVector((X0 + X1) * 0.5f, Y1 - 0.5f, Zf(F, Ceil14 * 0.5f)), FVector(X1 - X0, 1.0f, Ceil14), Paint, 0.0f, false);
	AddBox(FVector(X0 + 0.5f, (Y0 + Y1) * 0.5f, Zf(F, Ceil14 * 0.5f)), FVector(1.0f, Y1 - Y0, Ceil14), Paint, 0.0f, false);
	FloorFinish(F, FBox2D(FVector2D(X0, Y0), FVector2D(X1, Y1)), Mat(TEXT("MI_FloorVinyl")));
	Ceiling(F, FBox2D(FVector2D(0.0f, -HalfY), FVector2D(300.0f, -CorrY)), Ceil14, Mat(TEXT("MI_CeilingWhite")));
	// Linen and amenities on steel shelving along the west wall, folded piece by piece (blocks read as placeholders), each
	// piece with a rounded fold to the front (+X) and a grey core behind showing through the gaps as shadow (as in
	// housekeeping, walk 17).
	FRandomStream LinenRand(1414);
	auto Folded = [&](const FVector& Base, float SX, float SY, float Height, const TCHAR* Material)
	{
		float Z = 0.0f;
		while (Z + 4.0f <= Height)
		{
			const float T = LinenRand.FRandRange(4.5f, 7.5f);
			const FVector Off(LinenRand.FRandRange(-1.5f, 1.5f), LinenRand.FRandRange(-1.5f, 1.5f), Z + T * 0.5f);
			AddBox(Base + Off, FVector(SX, SY, T - 1.2f), Mat(Material), LinenRand.FRandRange(-2.0f, 2.0f), false);
			const float Rad = (T - 1.2f) * 0.5f;
			AddInstance(Cylinder, FTransform(FRotator(90.0f, 90.0f, 0.0f), Base + Off + FVector(SX * 0.5f - Rad * 0.5f, 0, 0), FVector(Rad / 50.0f, Rad / 50.0f, SY / 100.0f)), Mat(Material), false, false);
			Z += T;
		}
		if (Z > 0.0f) AddBox(Base + FVector(0, 0, Z * 0.5f), FVector(SX - 5.0f, SY - 5.0f, Z - 1.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	};
	for (int32 L = 0; L < 5; ++L)
	{
		const float Z = 10.0f + L * 45.0f;
		AddBox(FVector(X0 + 25.0f, -680.0f, Zf(F, Z)), FVector(46.0f, 440.0f, 2.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
		if (L == 4) break;
		for (float Y = -880.0f; Y < -480.0f; Y += 48.0f)
		{
			const bool bTowels = L < 2;
			const float H = bTowels ? 32.0f : 22.0f;
			// The bath towels ran out on this shelf (M001: why the attendant left 1412's door on her trolley).
			if (L == 1 && Y > -700.0f) continue;
			if (bTowels || L == 2) Folded(FVector(X0 + 25.0f, Y, Zf(F, Z + 1.0f)), 36.0f, 38.0f, H, TEXT("MI_Linen"));
			else for (int32 k = 0; k < 3; ++k) AddBox(FVector(X0 + 25.0f, Y - 12.0f + k * 12.0f, Zf(F, Z + 1.0f + H * 0.5f)), FVector(30.0f, 10.0f, H), Mat(k == 1 ? TEXT("MI_Cardboard") : TEXT("MI_Paper")), 0.0f, false); // amenity boxes
		}
	}
	for (float Y : {-900.0f, -460.0f}) AddBox(FVector(X0 + 25.0f, Y, Zf(F, 100.0f)), FVector(46.0f, 3.0f, 200.0f), Mat(TEXT("MI_Steel")), 0.0f, true);
	// The attendant's trolley: linen bag, caddy of amenities, waste bag.
	const FVector Cart(200.0f, -600.0f, Zf(F, 0.0f));
	// The case point is the trolley's top deck (its focus point sits inside it); the frame, open shelves of
	// linen, the bags and the caddy are built round it (a solid grey box read as a placeholder, walk 12).
	SpawnCasePoint(TEXT("f14_service"), TEXT("service_cart"), TEXT("Housekeeping trolley"), Cart + FVector(0, 0, 100.0f), 0.0f, Cube, FVector(0.6f, 1.3f, 0.04f),
		Mat(TEXT("MI_PlasticGrey")), EKcUseStyle::Screen, Cart + FVector(0, 0, 100.0f));
	AddCollider(Cart + FVector(0, 0, 50.0f), FVector(60.0f, 130.0f, 96.0f), FRotator::ZeroRotator);
	for (float Dy : {-63.0f, 63.0f}) AddBox(Cart + FVector(0, Dy, 52.0f), FVector(58.0f, 4.0f, 92.0f), Mat(TEXT("MI_Graphite")), 0.0f, false);
	for (float Z : {14.0f, 54.0f}) AddBox(Cart + FVector(0, 0, Z), FVector(58.0f, 126.0f, 3.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);
	for (int32 k = 0; k < 3; ++k)
	{
		Folded(Cart + FVector(0, -40.0f + k * 40.0f, 16.0f), 44.0f, 34.0f, 34.0f, TEXT("MI_Linen"));
		if (k != 1) Folded(Cart + FVector(0, -40.0f + k * 40.0f, 56.0f), 44.0f, 34.0f, 36.0f, TEXT("MI_Linen")); // the gap: the bath towels it ran out of
	}
	AddInstance(Sphere, Cart + FVector(0, 88.0f, 62.0f), 0.0f, FVector(0.5f, 0.36f, 0.75f), Mat(TEXT("MI_Linen")), false);
	AddInstance(Sphere, Cart + FVector(0, -86.0f, 55.0f), 0.0f, FVector(0.44f, 0.32f, 0.6f), Mat(TEXT("MI_DeviceBlack")), false);
	AddBox(Cart + FVector(0, 0, 108.0f), FVector(40.0f, 50.0f, 12.0f), Mat(TEXT("MI_FabricAccent")), 0.0f, false);
	for (int32 i = 0; i < 6; ++i)
	{
		AddInstance(Cylinder, Cart + FVector(-12.0f + (i % 3) * 12.0f, -14.0f + (i / 3) * 24.0f, 119.0f), 0.0f, FVector(0.05f, 0.05f, 0.14f), Mat(i % 2 ? TEXT("MI_Ceramic") : TEXT("MI_BottleAmber")), false);
	}
	for (float Dx : {-24.0f, 24.0f})
	{
		for (float Dy : {-55.0f, 55.0f}) AddInstance(Cylinder, FTransform(FRotator(90.0f, 0.0f, 0.0f), Cart + FVector(Dx, Dy, 5.0f), FVector(0.1f, 0.1f, 0.03f)), Mat(TEXT("MI_DeviceBlack")), false, false);
	}
	// Janitor's sink, the locked chemical cabinet, the house phone.
	AddBox(FVector(230.0f, Y0 + 30.0f, Zf(F, 40.0f)), FVector(60.0f, 50.0f, 80.0f), Mat(TEXT("MI_Steel")), 0.0f, true);
	AddBox(FVector(230.0f, Y0 + 6.0f, Zf(F, 110.0f)), FVector(4.0f, 4.0f, 30.0f), Mat(TEXT("MI_Chrome")), 0.0f, false);
	// The far wall, framed by the doorway (bare, it read as an unfinished room, walk 16-17): the dilution chart over the
	// sink, a mop and broom rack, and the wet-floor sign folded against the wall.
	AddBox(FVector(230.0f, Y0 + 0.6f, Zf(F, 165.0f)), FVector(40.0f, 1.2f, 50.0f), Mat(TEXT("MI_Paper")), 0.0f, false);
	AddLabel(TEXT("DILUTION CHART"), FVector(230.0f, Y0 + 1.4f, Zf(F, 184.0f)), 90.0f, 2.6f, Ink, F);
	AddLabel(TEXT("Floor cleaner  1:50"), FVector(230.0f, Y0 + 1.4f, Zf(F, 174.0f)), 90.0f, 1.9f, Ink, F);
	AddLabel(TEXT("Glass  1:20"), FVector(230.0f, Y0 + 1.4f, Zf(F, 167.0f)), 90.0f, 1.9f, Ink, F);
	AddLabel(TEXT("Disinfectant  1:100"), FVector(230.0f, Y0 + 1.4f, Zf(F, 160.0f)), 90.0f, 1.9f, Ink, F);
	AddLabel(TEXT("Gloves and goggles on"), FVector(230.0f, Y0 + 1.4f, Zf(F, 149.0f)), 90.0f, 1.9f, FColor(170, 30, 30), F);
	AddBox(FVector(130.0f, Y0 + 2.5f, Zf(F, 150.0f)), FVector(80.0f, 5.0f, 4.0f), Mat(TEXT("MI_Steel")), 0.0f, false);
	for (int32 i = 0; i < 3; ++i)
	{
		const float X = 105.0f + i * 25.0f;
		AddInstance(Cylinder, FVector(X, Y0 + 6.0f, Zf(F, 90.0f)), 0.0f, FVector(0.024f, 0.024f, 1.16f), Mat(i == 1 ? TEXT("MI_WoodOak") : TEXT("MI_Steel")), false);
	}
	AddInstance(Sphere, FVector(105.0f, Y0 + 7.0f, Zf(F, 26.0f)), 0.0f, FVector(0.17f, 0.15f, 0.26f), Mat(TEXT("MI_Linen")), false); // mop head
	AddBox(FVector(130.0f, Y0 + 7.0f, Zf(F, 28.0f)), FVector(30.0f, 6.0f, 8.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f, false);        // broom head
	AddBox(FVector(155.0f, Y0 + 5.0f, Zf(F, 24.0f)), FVector(26.0f, 3.0f, 22.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false);       // dustpan
	for (float Side : {-1.0f, 1.0f})
	{
		AddInstance(Cube, FTransform(FRotator(0.0f, 0.0f, Side * -6.0f), FVector(60.0f, Y0 + 9.0f + Side * 3.2f, Zf(F, 31.0f)), FVector(0.3f, 0.012f, 0.62f)), Mat(TEXT("MI_SafetyYellow")), false, false);
	}
	AddLabel(TEXT("CAUTION"), FVector(60.0f, Y0 + 11.6f, Zf(F, 46.0f)), 90.0f, 3.0f, Ink, F);
	AddLabel(TEXT("WET FLOOR"), FVector(60.0f, Y0 + 12.3f, Zf(F, 40.0f)), 90.0f, 2.6f, Ink, F);
	AddBox(FVector(X0 + 25.0f, -330.0f, Zf(F, 60.0f)), FVector(44.0f, 70.0f, 120.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, true);
	AddLabel(TEXT("CHEMICALS: KEEP LOCKED"), FVector(X0 + 47.4f, -330.0f, Zf(F, 100.0f)), 0.0f, 2.4f, Ink, F);
	AddBox(FVector(X1 - 3.0f, -300.0f, Zf(F, 140.0f)), FVector(6.0f, 18.0f, 22.0f), Mat(TEXT("MI_Ceramic")), 0.0f, false);
	AddLabel(TEXT("HOUSE PHONE  0"), FVector(X1 - 0.4f, -300.0f, Zf(F, 156.0f)), 180.0f, 1.6f, Ink, F); // on the wall over the phone
	AddBox(FVector(150.0f, -560.0f, Zf(F, Ceil14 - 3.0f)), FVector(14.0f, 124.0f, 6.0f), Mat(TEXT("MI_LedStrip")), 0.0f, false);
	AddLight(F, FVector(150.0f, -560.0f, Zf(F, Ceil14 - 30.0f)), 650.0f, 520.0f, Cool);
	SmokeDetector(F, FVector2D(150.0f, -760.0f), Ceil14);
	SpawnCasePoint(TEXT("f14_service"), FString(), TEXT("Floor closet"), FVector(150.0f, -260.0f, Zf(F, 1.0f)), 0.0f, Cube, FVector(0.4f, 0.4f, 0.01f), Mat(TEXT("MI_Rug")),
		EKcUseStyle::None, FVector(150.0f, -700.0f, Zf(F, 90.0f)));
}

// ---------------------------------------------------------------------------- furniture

void AKcHotel::Desk(const FVector& Center, float Yaw, const FVector& Size, UMaterialInterface* Top)
{
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return Center + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	AddBox(At(0, 0, Size.Z - 1.5f), FVector(Size.X, Size.Y, 3.0f), Top, Yaw);
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(At(Side * (Size.X * 0.5f - 3.0f), 0, (Size.Z - 3.0f) * 0.5f), FVector(4.0f, Size.Y - 6.0f, Size.Z - 3.0f), Top, Yaw);
	}
	AddBox(At(0, Size.Y * 0.5f - 6.0f, Size.Z * 0.6f), FVector(Size.X - 10.0f, 1.5f, Size.Z * 0.45f), Top, Yaw, false);
}

void AKcHotel::Monitor(const FVector& Base, float Yaw, UMaterialInterface* Screen)
{
	// Screen faces local +X; Base is the point on the desk under it. A slim arm on a weighted foot.
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return Base + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	AddBox(At(-14.0f, 0, 1.0f), FVector(12.0f, 12.0f, 2.0f), Mat(TEXT("MI_Graphite")), Yaw, false);
	AddBox(At(-14.0f, 0, 18.0f), FVector(3.0f, 3.0f, 34.0f), Mat(TEXT("MI_Chrome")), Yaw, false);
	AddBox(At(-7.0f, 0, 34.0f), FVector(14.0f, 3.0f, 3.0f), Mat(TEXT("MI_Chrome")), Yaw, false);
	AddBox(At(0, 0, 38.0f), FVector(2.5f, 62.0f, 36.0f), Mat(TEXT("MI_DeviceBlack")), Yaw);
	if (Screen) AddBox(At(1.4f, 0, 38.5f), FVector(0.3f, 59.0f, 33.0f), Screen, Yaw, false);
}

void AKcHotel::Chair(const FVector& Location, float Yaw, const TCHAR* ModelId)
{
	// The arm chair model's forward axis is opposite to the dining chair's.
	const float Extra = FCString::Strcmp(ModelId, TEXT("modern_arm_chair_01")) == 0 ? 180.0f : 0.0f;
	if (!AddInstance(Model(ModelId), Location, Yaw + ModelYawOffset + Extra, FVector(1.0f)))
	{
		// Upholstered chair from boxes, facing local +X.
		const FRotator R(0.0f, Yaw, 0.0f);
		auto At = [&](float X, float Y, float Z) { return Location + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
		AddBox(At(0, 0, 22.0f), FVector(52.0f, 54.0f, 44.0f), Mat(TEXT("MI_FabricDark")), Yaw);
		AddBox(At(-23.0f, 0, 62.0f), FVector(8.0f, 54.0f, 40.0f), Mat(TEXT("MI_FabricDark")), Yaw);
	}
}

void AKcHotel::TaskChair(const FVector& Location, float Yaw)
{
	// Five-star base on twin-wheel casters, gas lift, upholstered seat and back; faces local +X.
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return Location + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	for (int32 i = 0; i < 5; ++i)
	{
		const float A = Yaw + i * 72.0f;
		const FVector Dir = FRotator(0.0f, A, 0.0f).Vector();
		AddBox(Location + Dir * 15.0f + FVector(0, 0, 6.0f), FVector(30.0f, 4.0f, 3.0f), Mat(TEXT("MI_Graphite")), A, false);
		// Wheels on an axle across the leg and a stem up into it (5 cm cubes read as nubs, walk 17).
		AddInstance(Cylinder, FTransform(FRotator(0.0f, A, 90.0f), Location + Dir * 29.0f + FVector(0, 0, 2.6f), FVector(0.05f, 0.05f, 0.045f)), Mat(TEXT("MI_DeviceBlack")), false, false);
		AddBox(Location + Dir * 29.0f + FVector(0, 0, 5.5f), FVector(2.0f, 2.0f, 3.0f), Mat(TEXT("MI_Graphite")), A, false);
	}
	AddInstance(Cylinder, At(0, 0, 23.0f), 0.0f, FVector(0.05f, 0.05f, 0.34f), Mat(TEXT("MI_Chrome")), false);
	// A padded seat (a plain box read as a blocky placeholder, walk 14).
	AddBox(At(0, 0, 43.0f), FVector(46.0f, 46.0f, 5.0f), Mat(TEXT("MI_Graphite")), Yaw);
	AddInstance(Sphere, FTransform(FRotator(0.0f, Yaw, 0.0f), At(1.0f, 0, 46.5f), FVector(0.48f, 0.48f, 0.1f)), Mat(TEXT("MI_FabricDark")), false, false);
	// The back: a fabric panel with a lumbar pad, in a grey frame with rounded corners, raked back 8 degrees on a spine. An
	// oval shell read as a balloon on a stick (walk 16), a box with side rolls as a board with tabs at its corners (walk 17).
	// The collider stays where the old back box was, 5 cm deep at -22 (6 cm at -24 closed the banquet office's way
	// through to the bar store, walk 16).
	AddCollider(At(-22.0f, 0, 79.0f), FVector(5.0f, 38.0f, 38.0f), R);
	AddBox(At(-21.0f, 0, 52.0f), FVector(3.0f, 5.0f, 16.0f), Mat(TEXT("MI_Graphite")), Yaw, false);
	const FVector Pivot = At(-22.0f, 0, 79.0f);
	const FQuat Back(FRotator(8.0f, Yaw, 0.0f));
	auto BackAt = [&](float X, float Y, float Z) { return Pivot + Back.RotateVector(FVector(X, Y, Z)); };
	AddInstance(Cube, FTransform(Back, Pivot, FVector(0.045f, 0.4f, 0.4f)), Mat(TEXT("MI_FabricDark")), false, false);
	AddInstance(Cube, FTransform(Back, BackAt(1.8f, 0, -9.0f), FVector(0.02f, 0.38f, 0.07f)), Mat(TEXT("MI_FabricDark")), false, false);
	UMaterialInterface* Frame = Mat(TEXT("MI_PlasticGrey"));
	const FQuat Across = Back * FQuat(FRotator(0.0f, 0.0f, 90.0f));
	for (float S : {-20.5f, 20.5f})
	{
		AddInstance(Cylinder, FTransform(Back, BackAt(0, S, 0), FVector(0.05f, 0.05f, 0.41f)), Frame, false, false);
		AddInstance(Cylinder, FTransform(Across, BackAt(0, 0, S), FVector(0.05f, 0.05f, 0.41f)), Frame, false, false);
		for (float T : {-20.5f, 20.5f}) AddInstance(Sphere, BackAt(0, S, T), 0.0f, FVector(0.05f), Frame, false);
	}
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(At(0, Side * 25.0f, 58.0f), FVector(26.0f, 4.0f, 3.0f), Mat(TEXT("MI_Graphite")), Yaw, false);
		AddBox(At(0, Side * 25.0f, 52.0f), FVector(3.0f, 3.0f, 10.0f), Mat(TEXT("MI_Graphite")), Yaw, false);
	}
}

void AKcHotel::Sofa(const FVector& Location, float Yaw)
{
	if (AddInstance(Model(TEXT("sofa_02")), Location, Yaw + ModelYawOffset, FVector(1.0f))) return;
	// Three-seat sofa facing local +X: plinth, seat cushions, back, arms.
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return Location + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	AddBox(At(0, 0, 10.0f), FVector(88.0f, 210.0f, 20.0f), Mat(TEXT("MI_WoodWalnut")), Yaw);
	for (int32 i = -1; i <= 1; ++i) AddBox(At(6.0f, i * 64.0f, 32.0f), FVector(70.0f, 62.0f, 22.0f), Mat(TEXT("MI_FabricDark")), Yaw);
	AddBox(At(-36.0f, 0, 60.0f), FVector(16.0f, 210.0f, 60.0f), Mat(TEXT("MI_FabricDark")), Yaw);
	for (float Side : {-1.0f, 1.0f}) AddBox(At(0, Side * 100.0f, 45.0f), FVector(88.0f, 12.0f, 40.0f), Mat(TEXT("MI_FabricDark")), Yaw);
	AddBox(At(-20.0f, 60.0f, 56.0f), FVector(14.0f, 40.0f, 38.0f), Mat(TEXT("MI_FabricAccent")), Yaw + 8.0f, false);
}

void AKcHotel::Plant(const FVector& Location, bool bLarge)
{
	if (AddInstance(Model(bLarge ? TEXT("potted_plant_02") : TEXT("potted_plant_04")), Location, FMath::Fmod(Location.X * 0.37f + Location.Y, 360.0f), FVector(bLarge ? 1.4f : 1.0f)))
	{
		return;
	}
	const float S = bLarge ? 1.0f : 0.5f;
	AddInstance(Cylinder, Location + FVector(0, 0, 25.0f * S), 0.0f, FVector(0.4f, 0.4f, 0.5f) * S, Mat(TEXT("MI_Ceramic")), true);
	AddInstance(Sphere, Location + FVector(0, 0, 85.0f * S), 0.0f, FVector(0.75f, 0.75f, 1.0f) * S, Mat(TEXT("MI_Leaf")), false);
}

void AKcHotel::Bed(const FVector& HeadCenter, float Yaw)
{
	// Queen bed, local +X from the headboard to the foot: upholstered headboard in a walnut frame under a walnut cornice,
	// plinth, mattress, the duvet turned down and falling over the sides, a runner across the foot, soft pillows and
	// cushions. (FurnishRoom puts the wash light under the cornice: Bed has no floor to file a light under.)
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return HeadCenter + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	UMaterialInterface* Linen = Mat(TEXT("MI_Linen"));
	// A walnut panel across the wall behind the bed and both nightstands, and the upholstered headboard standing proud of
	// it in five channels: the old headboard sat mostly inside the party wall and the bed read as a white box (walk 14).
	AddBox(At(1.0f, 0, 78.0f), FVector(2.0f, 330.0f, 116.0f), Mat(TEXT("MI_WoodWalnut")), Yaw, false);
	AddBox(At(6.0f, 0, 84.0f), FVector(8.0f, 176.0f, 108.0f), Mat(TEXT("MI_WoodWalnut")), Yaw);
	// Oatmeal channels, each a padded roll (black flat channels read as slabs, walk 16; taupe ones near black, walk 17).
	for (int32 i = -2; i <= 2; ++i)
	{
		AddBox(At(10.0f, i * 34.0f, 88.0f), FVector(3.0f, 26.0f, 96.0f), Mat(TEXT("MI_Headboard")), Yaw, false);
		for (float E : {-13.0f, 13.0f}) AddInstance(Cylinder, At(10.0f, i * 34.0f + E, 88.0f), Yaw, FVector(0.04f, 0.04f, 0.96f), Mat(TEXT("MI_Headboard")), false);
	}
	// The cornice over it, with the LED strip that washes the channels hidden underneath.
	AddBox(At(14.0f, 0, 140.0f), FVector(24.0f, 184.0f, 4.0f), Mat(TEXT("MI_WoodWalnut")), Yaw, false);
	AddBox(At(23.0f, 0, 137.7f), FVector(1.2f, 170.0f, 0.6f), Mat(TEXT("MI_LedStrip")), Yaw, false);
	AddBox(At(102.5f, 0, 15.0f), FVector(205.0f, 162.0f, 30.0f), Mat(TEXT("MI_WoodWalnut")), Yaw);
	AddBox(At(102.0f, 0, 42.0f), FVector(200.0f, 156.0f, 24.0f), Linen, Yaw);
	// The duvet from the turn-down to the foot, its sides and foot hanging to the plinth, and the folded edge.
	AddBox(At(136.75f, 0, 57.5f), FVector(131.5f, 161.0f, 7.0f), Linen, Yaw, false);
	for (float Side : {-1.0f, 1.0f}) AddBox(At(136.75f, Side * 83.0f, 44.0f), FVector(131.5f, 2.0f, 27.0f), Linen, Yaw, false);
	AddBox(At(205.0f, 0, 44.0f), FVector(2.0f, 166.0f, 27.0f), Linen, Yaw, false);
	AddBox(At(74.0f, 0, 62.0f), FVector(14.0f, 162.0f, 5.0f), Linen, Yaw + 0.5f, false);
	AddInstance(Cylinder, FTransform(FRotator(0.0f, Yaw + 0.5f, 90.0f), At(67.0f, 0, 62.0f), FVector(0.05f, 0.05f, 1.62f)), Linen, false, false);
	// Rolled edges along the duvet's sides and foot (square edges were what made it read as a box), turned round the
	// corners: the rolls' flat ends and the drapes' square ends stepped there (walk 17).
	for (float Side : {-1.0f, 1.0f})
	{
		AddInstance(Cylinder, FTransform(FRotator(90.0f, Yaw, 0.0f), At(136.75f, Side * 80.5f, 57.5f), FVector(0.07f, 0.07f, 1.315f)), Linen, false, false);
		AddInstance(Sphere, At(202.5f, Side * 80.5f, 57.5f), 0.0f, FVector(0.07f), Linen, false);
		AddInstance(Cylinder, At(203.5f, Side * 82.5f, 44.0f), 0.0f, FVector(0.05f, 0.05f, 0.27f), Linen, false);
	}
	AddInstance(Cylinder, FTransform(FRotator(0.0f, Yaw, 90.0f), At(202.5f, 0, 57.5f), FVector(0.07f, 0.07f, 1.62f)), Linen, false, false);
	// Runner over the foot, draped down both sides, its folds rounded.
	// Taupe, not the harbour blue: blue on white read as a teal wedge (walk 14).
	AddBox(At(172.0f, 0, 62.0f), FVector(50.0f, 172.0f, 2.0f), Mat(TEXT("MI_FabricDark")), Yaw, false); // charcoal on white: taupe was lost (walk 16)
	for (float Side : {-1.0f, 1.0f})
	{
		AddBox(At(172.0f, Side * 85.5f, 50.0f), FVector(50.0f, 1.2f, 25.0f), Mat(TEXT("MI_FabricDark")), Yaw, false);
		AddInstance(Cylinder, FTransform(FRotator(90.0f, Yaw, 0.0f), At(172.0f, Side * 85.0f, 61.8f), FVector(0.03f, 0.03f, 0.5f)), Mat(TEXT("MI_FabricDark")), false, false);
	}
	// Pillows: two sleeping pillows against the headboard, two in front, a bolster between two soft cushions.
	for (float Side : {-1.0f, 1.0f})
	{
		AddInstance(Sphere, At(20.0f, Side * 38.0f, 66.0f), Yaw, FVector(0.3f, 0.68f, 0.22f), Linen, false);
		AddInstance(Sphere, FTransform(FRotator(25.0f, Yaw - Side * 4.0f, 0.0f), At(40.0f, Side * 36.0f, 70.0f), FVector(0.26f, 0.62f, 0.2f)), Linen, false);
	}
	AddInstance(Sphere, FTransform(FRotator(30.0f, Yaw, 0.0f), At(56.0f, 0, 70.0f), FVector(0.16f, 0.5f, 0.28f)), Mat(TEXT("MI_FabricDark")), false);
	// Harbour-blue cushions in front of the pillows: white on white read as one box (walk 13), dark boxes as black squares
	// (walk 17).
	for (float Side : {-1.0f, 1.0f})
	{
		AddInstance(Sphere, FTransform(FRotator(20.0f, Yaw + Side * 6.0f, 0.0f), At(52.0f, Side * 42.0f, 75.0f), FVector(0.22f, 0.44f, 0.4f)), Mat(TEXT("MI_FabricAccent")), false); // 14 cm deep read as flat plates (walk 18)
	}
}

void AKcHotel::Lamp(int32 Floor, const FVector& Base, bool bFloorLamp)
{
	// Brass stem, a linen drum shade glowing from inside, a warm pool of light. The light sits inside the shade, where it
	// cannot light the shade's own faces; the brighter diffuser is the opening you look into, the top of a table lamp and
	// the underside of a floor lamp. (Shades glowing as bright as a lens read as white drums, walk 17.)
	const float Stem = bFloorLamp ? 150.0f : 36.0f;
	const float ShadeD = bFloorLamp ? 0.42f : 0.3f;
	const float ShadeH = bFloorLamp ? 30.0f : 22.0f;
	const FVector Shade = Base + FVector(0, 0, Stem + 8.0f);
	AddInstance(Cylinder, Base + FVector(0, 0, 1.0f), 0.0f, FVector(bFloorLamp ? 0.3f : 0.14f, bFloorLamp ? 0.3f : 0.14f, 0.02f), Mat(TEXT("MI_Brass")), bFloorLamp);
	AddInstance(Cylinder, Base + FVector(0, 0, Stem * 0.5f), 0.0f, FVector(0.02f, 0.02f, Stem / 100.0f), Mat(TEXT("MI_Brass")), false);
	AddInstance(Cylinder, Shade, 0.0f, FVector(ShadeD, ShadeD, ShadeH / 100.0f), Mat(TEXT("MI_LampShade")), false);
	AddInstance(Cylinder, Shade + FVector(0, 0, (ShadeH * 0.5f + 0.3f) * (bFloorLamp ? -1.0f : 1.0f)), 0.0f, FVector(ShadeD * 0.45f, ShadeD * 0.45f, 0.004f),
		Mat(TEXT("MI_LampWarm")), false);
	if (!bFloorLamp) AddInstance(Sphere, Shade + FVector(0, 0, ShadeH * 0.5f + 2.2f), 0.0f, FVector(0.025f), Mat(TEXT("MI_Brass")), false); // finial
	AddLight(Floor, Base + FVector(0, 0, Stem + 4.0f), bFloorLamp ? 420.0f : 300.0f, bFloorLamp ? 420.0f : 300.0f, Warm);
}

void AKcHotel::Shelf(const FVector& Center, float Yaw, float Width, int32 Seed)
{
	// Open shelving (back to the wall, front along local +X) with binders: policies, manuals, logs.
	const FRotator R(0.0f, Yaw, 0.0f);
	auto At = [&](float X, float Y, float Z) { return Center + R.RotateVector(FVector(X, Y, 0.0f)) + FVector(0.0f, 0.0f, Z); };
	const float Height = 190.0f;
	for (float Side : {-1.0f, 1.0f}) AddBox(At(0, Side * Width * 0.5f, Height * 0.5f), FVector(36.0f, 3.0f, Height), Mat(TEXT("MI_Graphite")), Yaw);
	const TCHAR* Colors[] = {TEXT("MI_SuitNavy"), TEXT("MI_Graphite"), TEXT("MI_PlasticGrey"), TEXT("MI_Paper"), TEXT("MI_FireRed")};
	FRandomStream Rand(Seed);
	for (int32 Level = 0; Level < 5; ++Level)
	{
		const float Z = 4.0f + Level * 44.0f;
		AddBox(At(0, 0, Z), FVector(36.0f, Width, 2.5f), Mat(TEXT("MI_Graphite")), Yaw, false);
		if (Level == 4) break;
		float Y = -Width * 0.5f + 6.0f;
		while (Y < Width * 0.5f - 12.0f)
		{
			if (Rand.FRand() < 0.18f)
			{
				Y += 14.0f;
				continue;
			}
			const float T = Rand.FRandRange(4.5f, 7.0f);
			const float H = Rand.FRandRange(28.0f, 33.0f);
			AddBox(At(2.0f, Y + T * 0.5f, Z + 1.25f + H * 0.5f), FVector(28.0f, T, H), Mat(Colors[Rand.RandRange(0, 4)]), Yaw, false);
			Y += T + 0.6f;
		}
	}
}

void AKcHotel::ArtPrint(const FVector& WallPoint, float Yaw, const TCHAR* Print, const FVector2D& Size)
{
	// Framed print with its face along local +X.
	const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
	AddBox(WallPoint + Out * 1.5f, FVector(3.0f, Size.X + 8.0f, Size.Y + 8.0f), Mat(TEXT("MI_WoodWalnut")), Yaw, false);
	AddBox(WallPoint + Out * 3.1f, FVector(0.4f, Size.X, Size.Y), Mat(Print), Yaw, false);
}

// ---------------------------------------------------------------------------- casework

AKcCasePoint* AKcHotel::SpawnCasePoint(const FString& Location, const FString& Spot, const FString& Title, const FVector& At, float Yaw, UStaticMesh* Mesh,
	const FVector& Scale, UMaterialInterface* Material, EKcUseStyle Style, const FVector& Focus)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AKcCasePoint* P = GetWorld()->SpawnActor<AKcCasePoint>(At, FRotator(0.0f, Yaw, 0.0f), Params);
	if (!P) return nullptr;
	P->Location = Location;
	P->Spot = Spot;
	FString Name = Title;
	if (Name.IsEmpty() && !Spot.IsEmpty())
	{
		if (const kc::SpotDef* S = kc::FindSpot(KcConvert::ToStd(Spot))) Name = KcConvert::ToFString(S->Name);
	}
	P->Title = Name.IsEmpty() ? UKcCampaignSubsystem::LocationName(Location) : Name;
	P->Style = Style;
	P->Focus = Focus;
	P->SetAppearance(Mesh ? Mesh : Cube.Get(), Scale, Material);
	CasePoints.Add(P);
	return P;
}

void AKcHotel::PlaceFixedInteractables()
{
	const int32 F = TopFloor;
	// The door spots on 14 sit on each door's keycard reader; on 1406 and 1412 they swing with the leaf.
	struct FDoorSpot { const TCHAR* Spot; const TCHAR* Room; float X0; bool bNorth; };
	const FDoorSpot DoorSpots[] = {
		{TEXT("door_1406"), TEXT("1406"), 2700.0f, true},
		{TEXT("door_1412"), TEXT("1412"), 750.0f, false},
		{TEXT("door_1418"), TEXT("1418"), 3750.0f, false},
		{TEXT("door_1409"), TEXT("1409"), 3960.0f, true},
		{TEXT("door_1416"), TEXT("1416"), 3050.0f, false},
	};
	for (const FDoorSpot& D : DoorSpots)
	{
		const float Yaw = D.bNorth ? -90.0f : 90.0f;
		const FVector Out = FRotator(0.0f, Yaw, 0.0f).Vector();
		const FVector Right = FRotator(0.0f, Yaw + (D.bNorth ? -90.0f : 90.0f), 0.0f).Vector();
		const FVector Base(RoomDoorX(D.X0), D.bNorth ? CorrY : -CorrY, Zf(F, 0.0f));
		const FVector Reader = Base + Right * ((GuestDoorW - 6.0f) * 0.5f - 9.0f) + Out * 3.2f + FVector(0, 0, 128.0f);
		AKcCasePoint* P = SpawnCasePoint(TEXT("f14_corridor"), D.Spot, FString(), Reader, Yaw, Cube, FVector(0.016f, 0.074f, 0.134f), Mat(TEXT("MI_DeviceBlack")),
			EKcUseStyle::Screen, Base + Out * 2.0f + FVector(0, 0, 150.0f));
		if (const FDynDoor* Dyn = Doors.Find(D.Room))
		{
			if (P && Dyn->Pivot.IsValid()) P->AttachToComponent(Dyn->Pivot.Get(), FAttachmentTransformRules::KeepWorldTransform);
		}
	}
	// The floor's sprinkler control valve on the Stair A landing: the red riser, a gauge, the valve
	// wheel with its supervisory switch, and the chained padlock that holds it open (M013).
	{
		const float Vx = StairIn0 + 8.0f, Vy = -420.0f; // the riser and the sign are built with the stair (BuildStairs)
		AddBox(FVector(Vx, Vy + 16.0f, Zf(F, 120.0f)), FVector(12.0f, 14.0f, 16.0f), Mat(TEXT("MI_FireRed")), 0.0f, false); // valve body
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 27.0f, Zf(F, 120.0f)), FVector(0.03f, 0.03f, 0.12f)), Mat(TEXT("MI_Steel")), false, false); // stem
		AddInstance(Cylinder, FVector(Vx + 7.0f, Vy, Zf(F, 160.0f)), 0.0f, FVector(0.06f, 0.06f, 0.02f), Mat(TEXT("MI_Ceramic")), false);
		AddBox(FVector(Vx - 1.0f, Vy - 9.0f, Zf(F, 120.0f)), FVector(6.0f, 8.0f, 10.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f, false); // tamper switch on the riser, opposite the wheel
		// In front of the case point's red disc: a dark centre that leaves a red rim, three red spokes and a hub
		// (as on the other landings, BuildStairs).
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 35.7f, Zf(F, 120.0f)), FVector(0.165f, 0.165f, 0.005f)), Mat(TEXT("MI_Skirting")), false, false);
		for (int32 k = 0; k < 3; ++k)
		{
			AddInstance(Cube, FTransform(FRotator(k * 60.0f, 0.0f, 0.0f), FVector(Vx, Vy + 36.2f, Zf(F, 120.0f)), FVector(0.022f, 0.01f, 0.18f)), Mat(TEXT("MI_FireRed")), false, false);
		}
		AddInstance(Cylinder, FTransform(FRotator(0.0f, 0.0f, 90.0f), FVector(Vx, Vy + 36.6f, Zf(F, 120.0f)), FVector(0.05f, 0.05f, 0.01f)), Mat(TEXT("MI_Steel")), false, false);
		ValvePoint = SpawnCasePoint(TEXT("f14_corridor"), TEXT("valve_14"), FString(), FVector(Vx, Vy + 34.0f, Zf(F, 120.0f)), 90.0f, Cylinder, FVector(0.22f, 0.22f, 0.03f),
			Mat(TEXT("MI_FireRed")), EKcUseStyle::Screen, FVector(Vx + 120.0f, Vy + 60.0f, Zf(F, 150.0f)));
		if (ValvePoint) ValvePoint->SetActorRotation(FRotator(0.0f, 0.0f, 90.0f));
	}
	// Outside 1406 and 1407: where you stand to check the corridor (the floor's house phone).
	SpawnCasePoint(TEXT("f14_corridor"), TEXT("corridor_14"), FString(), FVector(2990.0f, -CorrY + Wall * 0.5f + 3.0f, Zf(F, 135.0f)), 90.0f, Cube,
		FVector(0.06f, 0.18f, 0.22f), Mat(TEXT("MI_Ceramic")), EKcUseStyle::Screen, FVector(3000.0f, 120.0f, Zf(F, 150.0f)));
	AddLabel(TEXT("HOUSE PHONE"), FVector(2990.0f, -CorrY + Wall * 0.5f + 0.4f, Zf(F, 155.0f)), 90.0f, 1.8f, Ink, F); // on the wall over the phone
	// The corridor's own case point, for work with no prop of its own: the lift lobby directory.
	SpawnCasePoint(TEXT("f14_corridor"), FString(), TEXT("Floor 14 lift lobby"), FVector(2470.0f, -CorrY + Wall * 0.5f + 2.6f, Zf(F, 158.0f)), 90.0f, Cube,
		FVector(0.004f, 0.98f, 0.56f), Mat(TEXT("MI_WoodWalnut")), EKcUseStyle::Screen);
	SpawnCasePoint(TEXT("lobby"), FString(), TEXT("Lobby"), FVector(1030.0f, 50.0f, Zf(0, 43.0f)), 12.0f, Cube, FVector(0.3f, 0.24f, 0.03f), Mat(TEXT("MI_Paper")),
		EKcUseStyle::PickUp);
	SpawnCasePoint(TEXT("back_of_house"), FString(), TEXT("Back-of-house corridor"), FVector(2470.0f, -CorrY + Wall * 0.5f + 2.6f, Zf(-1, 160.0f)), 90.0f, Cube,
		FVector(0.004f, 0.92f, 0.36f), Mat(TEXT("MI_Paper")), EKcUseStyle::Screen);
	Shelf(FVector(3700.0f - Wall * 0.5f - 20.0f, 850.0f, Zf(-1, 0.0f)), 180.0f, 120.0f, 31);
	// Stair A at floor 2, a landing nobody uses: the stair identification sign is where its work is done.
	// The point is the sign board itself, just behind its lettering (a smaller card hid the lower lines).
	SpawnCasePoint(TEXT("stair_a"), FString(), TEXT("Stair A, floor 2 landing"), FVector(StairIn0 + 1.0f, -314.0f, Zf(2, 170.0f)), 0.0f, Cube,
		FVector(0.014f, 0.98f, 1.22f), Mat(TEXT("MI_Paper")), EKcUseStyle::Screen, FVector(StairIn0 + 120.0f, -314.0f, Zf(2, 150.0f)));

	// Every spot the missions can use must have its prop here (KnownSpots).
	for (const kc::SpotDef& S : kc::KnownSpots())
	{
		const FString Id = KcConvert::ToFString(S.Id);
		const bool bFound = CasePoints.ContainsByPredicate([&](const AKcCasePoint* P) { return P && P->Spot == Id; });
		if (!bFound) UE_LOG(LogKeycard, Error, TEXT("Hotel: spot %s (%s) has no case point"), *Id, *KcConvert::ToFString(S.Location));
	}
}

AKcHotel::FPersonSpot AKcHotel::SpotFor(const FString& Location, const FString& PersonId, int32 Index) const
{
	for (const FPost& P : Posts)
	{
		if (PersonId == P.PersonId && Location == P.Location) return {FVector(P.At.X, P.At.Y, Zf(P.Floor, 90.0f)), P.Yaw};
	}
	// Anyone else: near the place's arrival point, side by side, facing it.
	const FTransform A = GetArrival(Location);
	const FVector Fwd = A.GetRotation().Vector();
	const FVector Side = FVector(-Fwd.Y, Fwd.X, 0.0f);
	const FVector At = A.GetLocation() + Fwd * 170.0f + Side * (Index - 1) * 80.0f;
	return {FVector(At.X, At.Y, At.Z - 10.0f), static_cast<float>(A.Rotator().Yaw + 180.0)};
}

static const TCHAR* FallbackOutfit(const FString& Outfit)
{
	if (Outfit == TEXT("lp") || Outfit == TEXT("police") || Outfit == TEXT("contractor")) return TEXT("MI_UniformBlack");
	if (Outfit == TEXT("housekeeping") || Outfit == TEXT("engineering")) return TEXT("MI_UniformGrey");
	if (Outfit == TEXT("guest") || Outfit == TEXT("fnb")) return TEXT("MI_FabricDark");
	return TEXT("MI_SuitNavy");
}

void AKcHotel::RefreshPeople()
{
	const UGameInstance* GI = GetGameInstance();
	const UKcCampaignSubsystem* C = GI ? GI->GetSubsystem<UKcCampaignSubsystem>() : nullptr;
	const UKcGameSubsystem* G = GI ? GI->GetSubsystem<UKcGameSubsystem>() : nullptr;
	if (!C || !G) return;
	const kc::MissionDef* M = G->HasGame() ? C->Active() : nullptr;

	// Who should be where: the night staff at their posts, and the case's people once they can be met.
	TArray<TPair<FString, FString>> Want;
	auto WantPerson = [&](const FString& Person, const FString& Location)
	{
		const TPair<FString, FString> Key(Person, Location);
		if (!Want.Contains(Key)) Want.Add(Key);
	};
	WantPerson(TEXT("night_auditor"), TEXT("front_desk"));
	WantPerson(TEXT("desk_agent"), TEXT("front_desk"));
	bool bPartnerOut = false;
	if (M)
	{
		const kc::CampaignState& CS = G->GetState().Campaign;
		auto Answered = [&](const std::string& QId)
		{
			for (const kc::MissionAnswer& A : CS.FieldAnswers) if (A.QuestionId == QId) return true;
			return false;
		};
		auto Consider = [&](const std::string& P, const std::string& At)
		{
			const FString Person = KcConvert::ToFString(P), Location = KcConvert::ToFString(At);
			// Behind a closed door until they open it: the door offers their work instead.
			for (const FBehindDoor& B : BehindDoors)
			{
				if (Person == B.PersonId && Location == B.PersonLocation && M->FindAction(KcConvert::ToStd(B.OpensWith)) && !C->IsDone(B.OpensWith)) return;
			}
			bool bMet = false;
			for (const kc::ActionDef& A : M->Actions)
			{
				if (A.Person == P && A.At == At && UKcCampaignSubsystem::IsFaceToFace(A) && C->IsDone(KcConvert::ToFString(A.Id))) bMet = true;
			}
			for (const kc::QuestionDef& Q : M->Questions)
			{
				if (Q.Person == P && Q.At == At && Answered(Q.Id)) bMet = true;
			}
			if (bMet || !C->ActionsAt(Location, Person).IsEmpty() || !C->QuestionsAt(Location, Person).IsEmpty()) WantPerson(Person, Location);
		};
		for (const kc::ActionDef& A : M->Actions)
		{
			if (UKcCampaignSubsystem::IsFaceToFace(A)) Consider(A.Person, A.At);
			// Radioed in, your partner meets you where the case is upstairs (M003: the floor 14 lift lobby).
			if (A.Person == "partner" && A.Kind == kc::EActionKind::Call && C->IsDone(KcConvert::ToFString(A.Id))) bPartnerOut = true;
		}
		for (const kc::QuestionDef& Q : M->Questions)
		{
			if (!Q.Person.empty() && !Q.At.empty()) Consider(Q.Person, Q.At);
		}
	}
	bool bCaseOn14 = false;
	if (M)
	{
		for (const kc::ActionDef& A : M->Actions)
		{
			const kc::LocationDef* L = kc::FindLocation(A.At);
			if (L && L->Floor == TopFloor) bCaseOn14 = true;
		}
	}
	WantPerson(TEXT("partner"), bPartnerOut && bCaseOn14 ? TEXT("f14_corridor") : TEXT("cctv_room"));

	FString Key = M ? KcConvert::ToFString(M->Id) : FString();
	for (const TPair<FString, FString>& W : Want) Key += TEXT("|") + W.Key + TEXT("@") + W.Value;
	if (Key == PeopleKey) return;
	PeopleKey = Key;

	// Keep whoever is still wanted where they stand (mid-conversation people never pop); add the rest.
	for (int32 i = People.Num() - 1; i >= 0; --i)
	{
		AKcPersonActor* P = People[i];
		const bool bKeep = IsValid(P) && Want.Contains(TPair<FString, FString>(P->PersonId, P->Location));
		if (!bKeep)
		{
			if (IsValid(P)) P->Destroy();
			People.RemoveAt(i);
		}
	}
	TMap<FString, int32> PerPlace;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (const TPair<FString, FString>& W : Want)
	{
		const int32 Index = PerPlace.FindOrAdd(W.Value)++;
		if (People.ContainsByPredicate([&](const AKcPersonActor* P) { return P->PersonId == W.Key && P->Location == W.Value; })) continue;
		const FPersonSpot S = SpotFor(W.Value, W.Key, Index);
		AKcPersonActor* P = GetWorld()->SpawnActor<AKcPersonActor>(S.At, FRotator(0.0f, S.Yaw, 0.0f), Params);
		if (!P) continue;
		P->SetAppearance(Cylinder, FVector(0.5f, 0.5f, 1.8f), nullptr);
		P->PersonId = W.Key;
		P->Location = W.Value;
		P->DisplayName = C->PersonName(KcConvert::ToStd(W.Key));
		P->Role = C->PersonRole(KcConvert::ToStd(W.Key));
		const bool bStaff = W.Key == TEXT("night_auditor") || W.Key == TEXT("desk_agent") || W.Key == TEXT("partner");
		const KcCast::FLook L = KcCast::ForPerson(W.Key, bStaff ? 2 : 1);
		P->SetBody(L, L.bFallback ? Mat(FallbackOutfit(KcCast::OutfitFor(W.Key))) : nullptr);
		People.Add(P);
	}
}

UKcPersonComponent* AKcHotel::AddExtra(const KcCast::FLook& Look, const FVector& At, float FacingYaw, int32 Seed)
{
	if (!Look.Mesh) return nullptr;
	UKcPersonComponent* C = NewObject<UKcPersonComponent>(this, NAME_None);
	C->SetupAttachment(RootComponent);
	C->SetRelativeLocation(At);
	C->RegisterComponent();
	C->Setup(Look, FString(), FacingYaw, Seed);
	if (Look.bFallback)
	{
		for (int32 m = 0; m < C->GetNumMaterials(); ++m) C->SetMaterial(m, Mat(TEXT("MI_FabricDark")));
	}
	Extras.Add(C);
	return C;
}

void AKcHotel::SceneBox(const FVector& Center, const FVector& Size, UMaterialInterface* Material, float Yaw, UStaticMesh* Mesh)
{
	UStaticMesh* Use = Mesh ? Mesh : Cube.Get();
	if (UStaticMeshComponent* C = AddMesh(Use, Center, Yaw, Size / 100.0f, Material, false))
	{
		C->SetCastShadow(false);
		SceneProps.Add(C);
	}
}

void AKcHotel::RefreshScenes()
{
	const UGameInstance* GI = GetGameInstance();
	const UKcCampaignSubsystem* C = GI ? GI->GetSubsystem<UKcCampaignSubsystem>() : nullptr;
	const UKcGameSubsystem* G = GI ? GI->GetSubsystem<UKcGameSubsystem>() : nullptr;
	const kc::MissionDef* M = (C && G && G->HasGame()) ? C->Active() : nullptr;
	auto Has = [&](const TCHAR* ActionId) { return M && M->FindAction(KcConvert::ToStd(ActionId)) != nullptr; };
	auto Done = [&](const TCHAR* ActionId) { return Has(ActionId) && C->IsDone(ActionId); };
	auto WorkAt = [&](const char* Location)
	{
		if (!M) return false;
		for (const kc::ActionDef& A : M->Actions) if (A.At == Location) return true;
		for (const kc::QuestionDef& Q : M->Questions) if (Q.At == Location) return true;
		return false;
	};
	const bool bParty = Has(TEXT("knock_1406"));
	const bool bKnocked = Done(TEXT("knock_1406"));
	const bool bCapOff = Done(TEXT("detector"));
	const bool bQuiet = Done(TEXT("recheck"));
	const bool bReturned = Done(TEXT("return_laptop"));
	const bool b1412 = WorkAt("room_1412");
	const bool bCloset = WorkAt("f14_service");
	const bool bBag = Has(TEXT("observe_bag")) && !Done(TEXT("secure_bag"));
	const bool bCodeBlue = Has(TEXT("assess")) && !Done(TEXT("handover"));
	const bool bAed = Done(TEXT("use_aed"));
	const bool bPropped = Has(TEXT("respond_alarm")) && !Done(TEXT("respond_alarm"));
	const bool bLuggage = Has(TEXT("check_luggage_room"));
	const bool bFireAlarm = Has(TEXT("read_panel")) && !Done(TEXT("debrief_team"));
	const bool bSilenced = Done(TEXT("silence_panel"));
	const bool bImpaired = Has(TEXT("tag_valve")) && Done(TEXT("tag_valve")) && !Done(TEXT("restore"));
	const bool bSlip = Has(TEXT("cone_area"));
	const bool bConed = Done(TEXT("cone_area")), bMopped = Done(TEXT("clean_floor"));
	const bool bHotWork = Has(TEXT("isolate_zone"));
	const bool bPrepped = Done(TEXT("walk_area"));
	const bool bK2Off = (Done(TEXT("isolate_zone")) || Done(TEXT("isolate_all"))) && !Done(TEXT("restore_zone"));
	const bool bAllOff = Done(TEXT("isolate_all")) && !Done(TEXT("restore_zone"));
	const bool bTheo = Has(TEXT("search_stairs")) && !Done(TEXT("search_stairs"));
	const bool bDeath = Has(TEXT("attend_room"));
	const bool bSecured = Done(TEXT("secure_scene"));
	const bool bChairs = Has(TEXT("clear_landing")) && !Done(TEXT("clear_landing"));
	const bool bOutage = Has(TEXT("call_engineer")) && Has(TEXT("talk_lift")) && !Done(TEXT("restore"));
	SetBlackout(bOutage);
	const bool bThreat = Has(TEXT("search_ballroom")) && Has(TEXT("cordon")) && !Done(TEXT("all_clear"));
	const bool bCordoned = Done(TEXT("cordon")), bOpened = Done(TEXT("open_bag"));
	const bool bRoomFire = Has(TEXT("refuge")) && Has(TEXT("call_fire")) && !Done(TEXT("preserve"));
	const bool bSmokeOut = Done(TEXT("into_smoke"));

	// Guests behind doors: the door offers their work until they open it.
	for (AKcCasePoint* P : CasePoints)
	{
		if (!P) continue;
		P->DoorPersonId.Reset();
		for (const FBehindDoor& B : BehindDoors)
		{
			if (P->Spot == B.Spot && Has(B.OpensWith) && !Done(B.OpensWith))
			{
				P->DoorPersonId = B.PersonId;
				P->DoorPersonLocation = B.PersonLocation;
			}
		}
	}

	const FString Key = FString::Printf(TEXT("%s|%d%d%d%d%d%d%d%d|%d%d%d%d%d|%d%d%d%d%d%d%d%d%d%d|%d%d%d%d%d|%d%d%d%d%d"), M ? *KcConvert::ToFString(M->Id) : TEXT(""), bParty, bKnocked, bCapOff, bQuiet, bReturned,
		b1412, bCloset, G && G->HasGame(), bBag, bCodeBlue, bAed, bPropped, bLuggage, bFireAlarm, bSilenced, bImpaired, bSlip, bConed, bMopped, bHotWork, bPrepped, bK2Off,
		bAllOff, bTheo, bDeath, bSecured, bChairs, bOutage, bThreat, bCordoned, bOpened, bRoomFire, bSmokeOut);
	if (Key == SceneKey) return;
	SceneKey = Key;
	for (UPrimitiveComponent* S : SceneProps)
	{
		if (!IsValid(S)) continue;
		if (UTextRenderComponent* T = Cast<UTextRenderComponent>(S)) Labels.Remove(T);
		S->DestroyComponent();
	}
	SceneProps.Reset();
	for (UKcPersonComponent* E : Extras) if (IsValid(E)) E->DestroyComponent();
	Extras.Reset();

	// Doors: 1412 stands open while its guest waits; the closet while the attendant is in it; 1406 once
	// Mr Pike opens up, until the party has thinned out.
	SetDoorOpen(TEXT("1412"), b1412);
	SetDoorOpen(TEXT("closet"), bCloset);
	SetDoorOpen(TEXT("1406"), (bParty && bKnocked && !bQuiet) || (bDeath && !bSecured));
	SetDoorOpen(TEXT("luggage"), bLuggage);
	SetDoorOpen(TEXT("dock"), bPropped);

	// M006: the holdall by the sofa, until it is taken to the front office safe.
	if (BagPoint)
	{
		BagPoint->SetActorHiddenInGame(!bBag);
		BagPoint->SetActorEnableCollision(bBag);
	}
	// M005: the bar's case point moves to where the guest went down: a stool on its side, his glass,
	// his jacket, and once it is used the AED open beside him. Back on the counter after the handover.
	if (LobbyBarPoint)
	{
		LobbyBarPoint->SetActorLocation(bCodeBlue ? FVector(1560.0f, -760.0f, Zf(0, 1.5f)) : LobbyBarCounter);
		LobbyBarPoint->SetActorRotation(FRotator(0.0f, bCodeBlue ? 20.0f : -84.0f, 0.0f));
		LobbyBarPoint->SetAppearance(Cube, bCodeBlue ? FVector(0.5f, 0.62f, 0.03f) : FVector(0.22f, 0.3f, 0.02f), Mat(bCodeBlue ? TEXT("MI_SuitNavy") : TEXT("MI_Paper")));
	}
	if (bCodeBlue)
	{
		SceneBox(FVector(1640.0f, -700.0f, Zf(0, 20.0f)), FVector(38.0f, 38.0f, 76.0f), Mat(TEXT("MI_Leather")), 0.0f, Cylinder);
		SceneBox(FVector(1520.0f, -700.0f, Zf(0, 1.0f)), FVector(30.0f, 22.0f, 0.4f), Mat(TEXT("MI_Glass")), 0.0f, Cylinder);
		SceneBox(FVector(1505.0f, -690.0f, Zf(0, 4.0f)), FVector(7.0f, 7.0f, 8.0f), Mat(TEXT("MI_Glass")), 0.0f, Cylinder);
		if (bAed)
		{
			SceneBox(FVector(1620.0f, -820.0f, Zf(0, 7.0f)), FVector(30.0f, 26.0f, 12.0f), Mat(TEXT("MI_SafetyYellow")), 10.0f);
			SceneBox(FVector(1620.0f, -820.0f, Zf(0, 13.5f)), FVector(14.0f, 10.0f, 1.0f), Mat(TEXT("MI_ScreenPanel")), 10.0f);
			SceneBox(FVector(1585.0f, -790.0f, Zf(0, 1.2f)), FVector(16.0f, 12.0f, 0.6f), Mat(TEXT("MI_Paper")), 30.0f);
		}
	}
	// The fire panel's screen: a fire on 14 (M012), the floor 14 valve supervisory (M013), zones
	// isolated for hot work (M015), or a quiet night.
	{
		const FVector Screen(3100.0f, HalfY - 15.0f - 6.0f - 6.8f, Zf(0, 168.0f));
		FString Status = TEXT("SYSTEM NORMAL");
		FColor Tint(120, 230, 140);
		if (bFireAlarm)
		{
			Status = bSilenced ? TEXT("FIRE  14 CORRIDOR E  (SILENCED)") : TEXT("FIRE  ZONE 14-3  14 CORRIDOR E");
			Tint = FColor(255, 70, 60);
		}
		else if (bOutage)
		{
			Status = TEXT("MAINS FAIL  ON BATTERY  2 FAULTS");
			Tint = FColor(255, 190, 60);
		}
		else if (bAllOff)
		{
			Status = TEXT("DISABLED  ZONES G-1 TO G-6");
			Tint = FColor(255, 190, 60);
		}
		else if (bK2Off)
		{
			Status = TEXT("DISABLED  ZONE K2  HW-0418");
			Tint = FColor(255, 190, 60);
		}
		else if (bImpaired)
		{
			Status = TEXT("SUPERVISORY  SPRINKLER VALVE 14");
			Tint = FColor(255, 190, 60);
		}
		if (UTextRenderComponent* T = AddLabel(Status, Screen, -90.0f, 2.6f, Tint, 0)) SceneProps.Add(T);
		if (bFireAlarm && !bSilenced) SceneBox(FVector(3055.0f, HalfY - 15.0f - 12.6f, Zf(0, 128.0f)), FVector(8.0f, 0.8f, 5.0f), Mat(TEXT("MI_LedRed")));
	}
	// M013: the red impairment tag on the floor 14 valve while it is shut.
	if (bImpaired && ValvePoint)
	{
		const FVector V = ValvePoint->GetActorLocation();
		SceneBox(V + FVector(0, 6.0f, -22.0f), FVector(1.0f, 9.0f, 15.0f), Mat(TEXT("MI_FireRed")));
		SceneBox(V + FVector(0, 2.0f, -10.0f), FVector(0.4f, 0.4f, 10.0f), Mat(TEXT("MI_Graphite")));
	}
	// M014: the wet patch until it is mopped, cones round it once it is made safe, the mat kicked askew.
	if (WetFloorPoint)
	{
		// Mopped, it is a damp sheen drying on the marble, still where the photos were taken.
		WetFloorPoint->SetActorHiddenInGame(!bSlip);
		WetFloorPoint->SetActorEnableCollision(bSlip);
		WetFloorPoint->SetAppearance(Cylinder, bMopped ? FVector(0.6f, 0.4f, 0.004f) : FVector(1.1f, 0.8f, 0.004f), Mat(bMopped ? TEXT("MI_Glass") : TEXT("MI_Water")));
		if (bSlip && bConed)
		{
			for (int32 i = 0; i < 4; ++i)
			{
				const FVector ConeAt = FVector(420.0f, -45.0f, Zf(0, 0.0f)) + FRotator(0.0f, 45.0f + i * 90.0f, 0.0f).Vector() * 85.0f;
				SceneBox(ConeAt + FVector(0, 0, 1.0f), FVector(30.0f, 30.0f, 2.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f);
				SceneBox(ConeAt + FVector(0, 0, 32.0f), FVector(16.0f, 16.0f, 60.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f, Cylinder);
			}
		}
	}
	// M015: the stepladder at the cracked bracket; the baffle filters on the canopy until the walk-round
	// takes them to the dish area, with the fire blanket over the fryers and a welding blanket on the gully.
	{
		const float CanX0 = 3420.0f, CanY = -HalfY + 15.0f + 65.0f;
		if (!bPrepped || !bHotWork)
		{
			for (float X = CanX0 + 55.0f; X < 4520.0f; X += 110.0f) SceneBox(FVector(X, CanY + 60.0f, Zf(0, 205.0f)), FVector(50.0f, 4.0f, 34.0f), Mat(TEXT("MI_Chrome")));
		}
		else
		{
			for (int32 i = 0; i < 10; ++i) SceneBox(FVector(2780.0f, -360.0f, Zf(0, 2.0f + i * 4.5f)), FVector(50.0f, 34.0f, 4.0f), Mat(TEXT("MI_Chrome")), 3.0f * (i % 3));
			SceneBox(FVector(3555.0f, CanY - 15.0f, Zf(0, 92.0f)), FVector(220.0f, 92.0f, 2.0f), Mat(TEXT("MI_FireRed")));
			SceneBox(FVector(3700.0f, CanY + 105.0f, Zf(0, 1.2f)), FVector(500.0f, 60.0f, 1.0f), Mat(TEXT("MI_Graphite")));
		}
		if (bHotWork)
		{
			const FVector L(CanX0 + 60.0f, CanY + 130.0f, Zf(0, 0.0f));
			for (float Side : {-1.0f, 1.0f}) SceneBox(L + FVector(Side * 22.0f, 0.0f, 85.0f), FVector(4.0f, 8.0f, 170.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f);
			for (int32 r = 0; r < 5; ++r) SceneBox(L + FVector(0.0f, 0.0f, 30.0f + r * 32.0f), FVector(44.0f, 10.0f, 2.0f), Mat(TEXT("MI_Steel")), 0.0f);
			SceneBox(L + FVector(90.0f, 20.0f, 30.0f), FVector(40.0f, 28.0f, 60.0f), Mat(TEXT("MI_FireRed")), 0.0f);
		}
	}
	// M020: Theo on the floor 2 landing of Stair A, sitting with his back to the locked stair door and his
	// knees up: blue waistcoat, white shirt, fair hair, trainers blinking red, until the search reaches him.
	if (bTheo)
	{
		const FVector T(StairDoorX, StairY1 - 16.0f, Zf(2, 0.0f));
		const float Yaw = -90.0f;
		const FRotator R(0.0f, Yaw, 0.0f);
		auto At = [&](float Fwd, float Side, float Up) { return T + R.RotateVector(FVector(Fwd, Side, Up)); };
		SceneBox(At(0.0f, 0.0f, 6.0f), FVector(24.0f, 22.0f, 12.0f), Mat(TEXT("MI_UniformBlack")), Yaw);             // seat
		SceneBox(At(1.0f, 0.0f, 33.0f), FVector(16.0f, 23.0f, 40.0f), Mat(TEXT("MI_SuitNavy")), Yaw, Cylinder);       // waistcoat
		SceneBox(At(1.0f, 0.0f, 51.0f), FVector(8.0f, 8.0f, 4.0f), Mat(TEXT("MI_Linen")), Yaw, Cylinder);             // collar
		SceneBox(At(3.0f, 0.0f, 61.0f), FVector(16.0f, 16.0f, 18.0f), Mat(TEXT("MI_Cork")), Yaw, Sphere);             // head, bowed
		SceneBox(At(0.0f, 0.0f, 66.0f), FVector(17.0f, 17.0f, 11.0f), Mat(TEXT("MI_WoodOak")), Yaw, Sphere);          // fair hair
		for (float Side : {-6.0f, 6.0f})
		{
			SceneBox(At(17.0f, Side, 22.0f), FVector(24.0f, 8.0f, 9.0f), Mat(TEXT("MI_UniformBlack")), Yaw);          // knees up
			SceneBox(At(28.0f, Side, 7.0f), FVector(8.0f, 8.0f, 20.0f), Mat(TEXT("MI_UniformBlack")), Yaw);
			SceneBox(At(32.0f, Side, 2.5f), FVector(15.0f, 7.0f, 5.0f), Mat(TEXT("MI_Ceramic")), Yaw);              // trainers
			SceneBox(At(32.0f, Side, 0.6f), FVector(15.5f, 7.5f, 1.2f), Mat(TEXT("MI_LedRed")), Yaw);
			SceneBox(At(15.0f, Side * 2.2f, 31.0f), FVector(20.0f, 6.0f, 6.0f), Mat(TEXT("MI_Linen")), Yaw);         // arms round the knees
		}
	}
	// M021: the housekeeping cart left outside 1406 where the room attendant stopped; once the scene is
	// secured the door is shut with a notice on it and nobody else goes in.
	if (bDeath)
	{
		SceneBox(FVector(2880.0f, CorrY - 45.0f, Zf(TopFloor, 50.0f)), FVector(110.0f, 48.0f, 100.0f), Mat(TEXT("MI_PlasticGrey")), 0.0f);
		SceneBox(FVector(2880.0f, CorrY - 45.0f, Zf(TopFloor, 101.0f)), FVector(104.0f, 44.0f, 2.0f), Mat(TEXT("MI_Linen")), 0.0f);
		if (bSecured)
		{
			SceneBox(FVector(2775.0f + 40.0f, CorrY - Wall * 0.5f - 1.0f, Zf(TopFloor, 150.0f)), FVector(21.0f, 0.6f, 30.0f), Mat(TEXT("MI_Paper")), 0.0f);
			if (UTextRenderComponent* L = AddLabel(TEXT("NO ENTRY  LP"), FVector(2775.0f + 40.0f, CorrY - Wall * 0.5f - 1.5f, Zf(TopFloor, 155.0f)), -90.0f, 2.0f, Ink, TopFloor)) SceneProps.Add(L);
		}
	}
	// M022: banquet chairs stacked on the Stair A floor 2 landing, narrowing the exit route.
	if (bChairs)
	{
		for (int32 s = 0; s < 3; ++s)
		{
			const FVector B(StairIn0 + 40.0f + s * 46.0f, StairY0 + 40.0f, Zf(2, 0.0f));
			for (int32 c = 0; c < 6; ++c) SceneBox(B + FVector(0, 0, 45.0f + c * 8.0f), FVector(42.0f, 42.0f, 4.0f), Mat(TEXT("MI_Brass")), 4.0f * (c % 2));
			SceneBox(B + FVector(0, -18.0f, 95.0f + 40.0f), FVector(40.0f, 4.0f, 50.0f), Mat(TEXT("MI_FabricAccent")), 0.0f);
		}
	}
	// C002: the wedding fair's cake table by the stage, skirted to the floor, and under it the grey
	// holdall nobody claims; tape across the ballroom doors once it is cordoned.
	if (bThreat)
	{
		const int32 F1 = 1;
		const FVector T(4480.0f, -300.0f, Zf(F1, 0.0f));
		SceneBox(T + FVector(0, 0, 76.0f), FVector(90.0f, 180.0f, 2.0f), Mat(TEXT("MI_Linen")), 0.0f);
		SceneBox(T + FVector(-44.0f, 0, 38.0f), FVector(1.0f, 180.0f, 74.0f), Mat(TEXT("MI_Linen")), 0.0f);
		for (int32 t = 0; t < 3; ++t) SceneBox(T + FVector(0, 40.0f, 90.0f + t * 18.0f), FVector(40.0f - t * 10.0f, 40.0f - t * 10.0f, 18.0f), Mat(TEXT("MI_CeilingWhite")), 0.0f, Cylinder);
		const FVector Bag = T + FVector(10.0f, -50.0f, 0.0f);
		SceneBox(Bag + FVector(0, 0, 17.0f), FVector(34.0f, 60.0f, 32.0f), Mat(TEXT("MI_UniformGrey")), 8.0f, Cylinder);
		SceneBox(Bag + FVector(0, 0, 34.0f), FVector(4.0f, 30.0f, 3.0f), Mat(TEXT("MI_Graphite")), 8.0f);
		if (bOpened)
		{
			SceneBox(Bag + FVector(0, 0, 38.0f), FVector(22.0f, 28.0f, 10.0f), Mat(TEXT("MI_Cardboard")), 8.0f);
			SceneBox(Bag + FVector(0, 4.0f, 45.0f), FVector(14.0f, 8.0f, 5.0f), Mat(TEXT("MI_DeviceBlack")), 8.0f);
			SceneBox(Bag + FVector(5.0f, 4.0f, 47.6f), FVector(5.0f, 0.4f, 0.4f), Mat(TEXT("MI_LedRed")), 8.0f);
		}
		if (bCordoned)
		{
			for (float H : {95.0f, 125.0f})
			{
				SceneBox(FVector(CoreX1 - 6.0f, 0.0f, Zf(F1, H)), FVector(1.0f, 230.0f, 6.0f), Mat(TEXT("MI_SafetyYellow")), 0.0f);
			}
		}
	}
	// C003: smoke from 1409 lying under the corridor ceiling, thicker by the door, and the glow and
	// haze under the door; a dense bank down to head height if the door was opened.
	if (bRoomFire)
	{
		const float DoorX = 3960.0f;
		const int32 SmokeLayers = bSmokeOut ? 6 : 3;
		for (int32 i = 0; i < SmokeLayers; ++i)
		{
			const float Spread = 260.0f + i * 220.0f;
			SceneBox(FVector(DoorX, 20.0f, Zf(TopFloor, Ceil14 - 18.0f - i * (bSmokeOut ? 22.0f : 8.0f))), FVector(Spread, 2.0f * CorrY - 20.0f, 30.0f), Mat(TEXT("MI_Sheer")), 0.0f);
		}
		SceneBox(FVector(DoorX, CorrY - Wall * 0.5f - 1.5f, Zf(TopFloor, 1.0f)), FVector(90.0f, 2.0f, 1.5f), Mat(TEXT("MI_LedAmber")), 0.0f);
	}
	// M009: the dock door wedged open with the pallet jack; otherwise the jack is parked by the pallets.
	{
		const FVector Jack = bPropped ? FVector(DockDoorX - 20.0f, -HalfY + 40.0f, Zf(-1, 0.0f)) : FVector(3650.0f, -640.0f, Zf(-1, 0.0f));
		const float JackYaw = bPropped ? 75.0f : 0.0f;
		const FRotator Turn(0.0f, JackYaw, 0.0f);
		auto Part = [&](const FVector& Local, const FVector& Size, const TCHAR* Material, UStaticMesh* Mesh = nullptr)
		{
			SceneBox(Jack + Turn.RotateVector(Local), Size, Mat(Material), JackYaw, Mesh);
		};
		// Two forks with load rollers at the tips, the pump housing and steer wheels at the back, the tiller and its grip.
		for (float Y : {-17.0f, 17.0f})
		{
			Part(FVector(5.0f, Y, 6.0f), FVector(110.0f, 16.0f, 5.0f), TEXT("MI_FireRed"));
			Part(FVector(5.0f, Y + (Y > 0 ? 7.0f : -7.0f), 4.5f), FVector(110.0f, 2.0f, 6.0f), TEXT("MI_FireRed"));
			Part(FVector(54.0f, Y, 3.5f), FVector(7.0f, 7.0f, 7.0f), TEXT("MI_Graphite"));
		}
		Part(FVector(-55.0f, 0.0f, 12.0f), FVector(16.0f, 52.0f, 16.0f), TEXT("MI_FireRed"));
		Part(FVector(-64.0f, 0.0f, 24.0f), FVector(14.0f, 14.0f, 28.0f), TEXT("MI_Graphite"));
		Part(FVector(-64.0f, 0.0f, 9.0f), FVector(18.0f, 24.0f, 18.0f), TEXT("MI_DeviceBlack"));
		Part(FVector(-72.0f, 0.0f, 76.0f), FVector(4.0f, 4.0f, 90.0f), TEXT("MI_Graphite"));
		Part(FVector(-72.0f, 0.0f, 124.0f), FVector(5.0f, 34.0f, 5.0f), TEXT("MI_DeviceBlack"));
		Part(FVector(-72.0f, 0.0f, 112.0f), FVector(3.0f, 26.0f, 22.0f), TEXT("MI_Graphite"));
	}

	const int32 F = TopFloor;
	if (bReturned && !Desk1412.IsZero())
	{
		// The laptop back on the desk, closed, its black bag on the chair.
		SceneBox(Desk1412 + FVector(10.0f, 0.0f, 1.2f), FVector(24.0f, 33.0f, 1.8f), Mat(TEXT("MI_PlasticGrey")), 4.0f);
		SceneBox(Desk1412 + FVector(70.0f, -5.0f, -24.0f), FVector(12.0f, 40.0f, 30.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f);
	}
	if (bParty)
	{
		// 1406, a thirtieth birthday: nine people in a room for two, bottles and cups, a speaker,
		// pizza boxes, balloons, someone smoking by the window, a shower cap over the detector.
		FRoomFrame R;
		R.X0 = 2700.0f;
		R.W = 420.0f;
		R.bNorth = true;
		R.Zb = FloorZ(F);
		struct FGuest { float U, V, Face; };
		const FGuest Crowd[] = {{110.0f, 330.0f, -80.0f}, {190.0f, 300.0f, -110.0f}, {260.0f, 370.0f, 200.0f}, {130.0f, 420.0f, 20.0f}, {300.0f, 690.0f, 230.0f},
			{60.0f, 790.0f, 90.0f}};
		const int32 Count = bQuiet ? 3 : static_cast<int32>(UE_ARRAY_COUNT(Crowd));
		for (int32 i = 0; i < Count; ++i)
		{
			AddExtra(KcCast::ForStaff(40 + i), R.P(Crowd[i].U, Crowd[i].V, 0.0f), Crowd[i].Face, 1406 + i * 17);
		}
		FRandomStream Rand(30);
		static const TCHAR* const Bottles[] = {TEXT("MI_BottleGreen"), TEXT("MI_BottleAmber"), TEXT("MI_Glass")};
		const int32 NBottles = bQuiet ? 4 : 11;
		for (int32 i = 0; i < NBottles; ++i)
		{
			const FVector P = R.P(18.0f + Rand.FRandRange(0.0f, 22.0f), 450.0f + Rand.FRandRange(0.0f, 110.0f), DeskH + 13.0f);
			SceneBox(P, FVector(7.0f, 7.0f, 26.0f), Mat(Bottles[i % 3]), 0.0f, Cylinder);
		}
		for (int32 i = 0; i < 8; ++i)
		{
			SceneBox(R.P(25.0f + Rand.FRandRange(0.0f, 20.0f), 660.0f + Rand.FRandRange(0.0f, 110.0f), 87.0f), FVector(7.0f, 7.0f, 10.0f), Mat(TEXT("MI_FireRed")), 0.0f, Cylinder);
		}
		SceneBox(R.P(30.0f, 780.0f, 97.0f), FVector(18.0f, 18.0f, 30.0f), Mat(TEXT("MI_DeviceBlack")), 0.0f);
		SceneBox(R.P(39.5f, 780.0f, 104.0f), FVector(0.4f, 10.0f, 1.0f), Mat(bQuiet ? TEXT("MI_DeviceBlack") : TEXT("MI_LedGreen")), 0.0f);
		for (int32 i = 0; i < 3; ++i)
		{
			SceneBox(R.P(300.0f + i * 18.0f, 520.0f + i * 22.0f, 66.0f + i * 4.0f), FVector(36.0f, 36.0f, 4.0f), Mat(TEXT("MI_Cardboard")), 12.0f * i);
		}
		for (int32 i = 0; i < 4; ++i)
		{
			const FVector B = R.P(380.0f - i * 30.0f, 780.0f, 200.0f + (i % 2) * 18.0f);
			SceneBox(B, FVector(28.0f, 28.0f, 34.0f), Mat(i % 2 ? TEXT("MI_Brass") : TEXT("MI_FabricAccent")), 0.0f, Sphere);
		}
		SceneBox(R.P(210.0f, 812.0f, 230.0f), FVector(180.0f, 1.0f, 24.0f), Mat(TEXT("MI_Paper")), 0.0f);
		if (UTextRenderComponent* T = AddLabel(TEXT("HAPPY 30TH JORDAN"), R.P(210.0f, 811.0f, 230.0f), -90.0f, 9.0f, FColor(200, 40, 60), F))
		{
			SceneProps.Add(T);
		}
		if (!bCapOff)
		{
			SceneBox(Detector1406 - FVector(0, 0, 3.0f), FVector(30.0f, 30.0f, 9.0f), Mat(TEXT("MI_Sheer")), 0.0f, Sphere);
			SceneBox(R.P(70.0f, 815.0f, 152.0f), FVector(0.8f, 0.8f, 0.8f), Mat(TEXT("MI_LedAmber")), 0.0f, Sphere);
		}
		SceneBox(R.P(120.0f, 806.0f, 64.0f), FVector(12.0f, 12.0f, 2.0f), Mat(TEXT("MI_Glass")), 0.0f, Cylinder);
	}

	// The party, heard through the door; turned down once you have spoken to Mr Pike, off after the recheck.
	if (bParty && !bQuiet)
	{
		if (!PartyMusic)
		{
			if (UKcAudioSubsystem* Audio = GI->GetSubsystem<UKcAudioSubsystem>())
			{
				PartyMusic = Audio->SpawnAmbience(GetWorld(), TEXT("AMB_PartyMusic"), FVector(2910.0f, 560.0f, Zf(F, 120.0f)), 1100.0f, 0.9f);
			}
		}
		if (PartyMusic) PartyMusic->SetVolumeMultiplier(bKnocked ? 0.3f : 0.9f);
	}
	else if (PartyMusic)
	{
		PartyMusic->Stop();
		PartyMusic->DestroyComponent();
		PartyMusic = nullptr;
	}
}

// ---------------------------------------------------------------------------- night and life

void AKcHotel::BuildLighting()
{
	// Night over the harbour: a faint cool moon, a drawn night dome and skyline in the windows, the HDRI as the sky light.
	UDirectionalLightComponent* Moon = NewObject<UDirectionalLightComponent>(this, TEXT("Moon"));
	Moon->SetMobility(EComponentMobility::Movable);
	Moon->SetupAttachment(RootComponent);
	Moon->SetRelativeRotation(FRotator(-38.0f, 140.0f, 0.0f));
	Moon->SetIntensity(0.35f);
	Moon->SetLightColor(FLinearColor(0.62f, 0.72f, 1.0f));
	Moon->SetCastShadows(false);
	Moon->RegisterComponent();
	if (UStaticMeshComponent* Sky = AddMesh(Sphere, FVector(2400.0f, 0.0f, 2000.0f), 0.0f, FVector(3000.0f), Mat(TEXT("MI_Sky")), false)) // 1.5 km: outside the far skyline ring
	{
		if (UMaterialInterface* SkyMat = Mat(TEXT("MI_Sky"))) SkyMaterial = Sky->CreateDynamicMaterialInstance(0, SkyMat);
		Sky->SetCastShadow(false);
		Sky->bAffectDistanceFieldLighting = false;
		Sky->bVisibleInRayTracing = false;
	}
	// The city round the harbour: lit-window skylines, a far ring 1 km out on every side (the far shore north
	// across the water) and nearer low-rise 300 m out on the street sides. From 14 the near roofs are below the
	// eye and the far towers on the horizon; from G both rise above it. Sizes match SKYLINES in GenerateTextures.py.
	struct FSkyline { FVector2D At; float Yaw; float Width; float Height; const TCHAR* Material; };
	for (const FSkyline& K : {FSkyline{FVector2D(2400.0f, 100000.0f), -90.0f, 200000.0f, 12000.0f, TEXT("MI_SkylineFarN")},
			 FSkyline{FVector2D(2400.0f, -100000.0f), 90.0f, 200000.0f, 12000.0f, TEXT("MI_SkylineFarS")},
			 FSkyline{FVector2D(-97600.0f, 0.0f), 0.0f, 200000.0f, 12000.0f, TEXT("MI_SkylineFarW")},
			 FSkyline{FVector2D(102400.0f, 0.0f), 180.0f, 200000.0f, 12000.0f, TEXT("MI_SkylineFarE")},
			 FSkyline{FVector2D(2400.0f, -30000.0f), 90.0f, 70000.0f, 4000.0f, TEXT("MI_SkylineNearS")},
			 FSkyline{FVector2D(-30000.0f, -10000.0f), 0.0f, 40000.0f, 4000.0f, TEXT("MI_SkylineNearW")},
			 FSkyline{FVector2D(35000.0f, -10000.0f), 180.0f, 40000.0f, 4000.0f, TEXT("MI_SkylineNearE")}})
	{
		// Thin along local X, which faces the hotel, so the drawing reads unmirrored (as the screens do).
		UMaterialInterface* M = Mat(K.Material);
		if (!M) continue;
		if (UStaticMeshComponent* C = AddMesh(Cube, FVector(K.At.X, K.At.Y, K.Height * 0.5f - 200.0f), K.Yaw, FVector(0.1f, K.Width / 100.0f, K.Height / 100.0f), M, false))
		{
			C->SetCastShadow(false);
			C->bAffectDistanceFieldLighting = false;
			C->bVisibleInRayTracing = false;
		}
	}
	USkyLightComponent* SkyLight = NewObject<USkyLightComponent>(this, TEXT("SkyLight"));
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetupAttachment(RootComponent);
	if (UTextureCube* SkyCube = LoadObject<UTextureCube>(nullptr, TEXT("/Game/Keycard/HDRI/T_NightCity.T_NightCity"), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		SkyLight->SourceType = ESkyLightSourceType::SLS_SpecifiedCubemap;
		SkyLight->SetCubemap(SkyCube);
	}
	SkyLight->SetIntensity(1.2f);
	SkyLight->RegisterComponent();
	SkyLight->RecaptureSky();

	// Post process: exposure held in a night band, soft bloom on the lamps, a light vignette.
	UPostProcessComponent* Post = NewObject<UPostProcessComponent>(this, TEXT("Post"));
	Post->SetupAttachment(RootComponent);
	Post->bUnbound = true;
	FPostProcessSettings& P = Post->Settings;
	P.bOverride_AutoExposureMethod = true;
	P.AutoExposureMethod = EAutoExposureMethod::AEM_Histogram;
	P.bOverride_AutoExposureMinBrightness = true;
	P.AutoExposureMinBrightness = ExposureMinEV100;
	P.bOverride_AutoExposureMaxBrightness = true;
	P.AutoExposureMaxBrightness = ExposureMaxEV100;
	P.bOverride_AutoExposureSpeedUp = true;
	P.AutoExposureSpeedUp = 1.6f;
	P.bOverride_AutoExposureSpeedDown = true;
	P.AutoExposureSpeedDown = 1.2f;
	P.bOverride_LumenMaxRoughnessToTraceReflections = true;
	P.LumenMaxRoughnessToTraceReflections = 0.35f;
	P.bOverride_LumenReflectionQuality = true;
	P.LumenReflectionQuality = 0.75f;
	// No screen-space AO: it turned the dim upper walls of the B1 rooms black (walks 9-10; see DefaultEngine.ini).
	P.bOverride_AmbientOcclusionIntensity = true;
	P.AmbientOcclusionIntensity = 0.0f;
	P.bOverride_IndirectLightingIntensity = true;
	P.IndirectLightingIntensity = 1.25f;
	P.bOverride_BloomIntensity = true;
	P.BloomIntensity = 0.55f;
	P.bOverride_VignetteIntensity = true;
	P.VignetteIntensity = 0.32f;
	P.bOverride_LensFlareIntensity = true;
	P.LensFlareIntensity = 0.0f;
	P.bOverride_MotionBlurAmount = true;
	P.MotionBlurAmount = 0.15f;
	P.bOverride_WhiteTemp = true;
	P.WhiteTemp = 5600.0f; // lamps read warm, screens read cool
	Post->RegisterComponent();
}

void AKcHotel::BuildAmbience()
{
	UGameInstance* GI = GetGameInstance();
	UKcAudioSubsystem* Audio = GI ? GI->GetSubsystem<UKcAudioSubsystem>() : nullptr;
	if (!Audio) return;
	UWorld* W = GetWorld();
	// Radii stay inside a floor (400 cm slab to slab) so each floor sounds like itself.
	Audio->SpawnAmbience(W, TEXT("AMB_Hvac"), FVector(1200.0f, 0.0f, Zf(-1, 250.0f)), 1800.0f, 0.4f);
	Audio->SpawnAmbience(W, TEXT("AMB_Fluorescent"), FVector(3200.0f, 0.0f, Zf(-1, 250.0f)), 900.0f, 0.25f);
	Audio->SpawnAmbience(W, TEXT("AMB_Servers"), FVector(MaxX - 40.0f, 300.0f, Zf(-1, 120.0f)), 700.0f, 0.45f);
	Audio->SpawnAmbience(W, TEXT("AMB_LobbyNight"), FVector(1050.0f, 100.0f, Zf(0, 200.0f)), 1600.0f, 0.4f);
	Audio->SpawnAmbience(W, TEXT("AMB_Harbour"), FVector(-300.0f, 0.0f, Zf(0, 150.0f)), 1200.0f, 0.3f);
	Audio->SpawnAmbience(W, TEXT("AMB_Hvac"), FVector(2400.0f, 0.0f, Zf(14, 240.0f)), 2600.0f, 0.3f);
	Audio->SpawnAmbience(W, TEXT("AMB_StairEcho"), FVector(2400.0f, -320.0f, Zf(7, 200.0f)), 3600.0f, 0.2f);
}
