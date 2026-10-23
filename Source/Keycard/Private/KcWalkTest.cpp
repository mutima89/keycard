#include "KcWalkTest.h"

#include "Algo/Reverse.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Containers/Ticker.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "ImageCore.h"
#include "ImageUtils.h"
#include "KcCharacter.h"
#include "KcHUD.h"
#include "KcHotel.h"
#include "KcInteractable.h"
#include "KcInteractionComponent.h"
#include "KcLog.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

namespace KcWalkTest
{
namespace
{
// The grid covers the footprint (X 0..4800, Y -1000..1000) with a margin.
constexpr float Cell = 20.0f;
constexpr float GridX0 = -40.0f;
constexpr float GridY0 = -1040.0f;
constexpr int32 NX = 244;
constexpr int32 NY = 104;
constexpr int32 MapScale = 3;
constexpr float EyeAboveCentre = 70.0f; // AKcCharacter's camera
// Stair A (KcHotel.cpp): the floor landing X 2120..2250, flights north (Y -305..-168) and south
// (Y -460..-315) of the spine, the mid landing X 2558..2680.
const FVector2D StairLanding(2195.0f, -314.0f);
const FVector2D StairDoorIn(2190.0f, -240.0f);
const FVector2D StairDoorOut(2185.0f, -60.0f);
const FVector2D LiftLobby(2400.0f, -40.0f);

enum class ECell : uint8 { Void, Blocked, Stand };

struct FGrid
{
	int32 Floor = 0;
	TArray<ECell> Kind;
	TArray<float> Height; // standing height above the floor's slab
	TArray<int32> Region; // 0: reached from the seed; >0: islands; -1: not standable
	int32 Islands = 0;
	TArray<FString> UseFails;

	static int32 Index(int32 X, int32 Y) { return Y * NX + X; }
	static FVector2D Centre(int32 X, int32 Y) { return FVector2D(GridX0 + (X + 0.5f) * Cell, GridY0 + (Y + 0.5f) * Cell); }
	static bool CellOf(const FVector2D& P, int32& X, int32& Y)
	{
		X = FMath::FloorToInt((P.X - GridX0) / Cell);
		Y = FMath::FloorToInt((P.Y - GridY0) / Cell);
		return X >= 0 && Y >= 0 && X < NX && Y < NY;
	}
	bool Reached(int32 X, int32 Y) const { return X >= 0 && Y >= 0 && X < NX && Y < NY && Region[Index(X, Y)] == 0; }
	// The reached cell nearest to P within Radius cells, or INDEX_NONE.
	int32 NearestReached(const FVector2D& P, int32 Radius) const
	{
		int32 CX, CY;
		CellOf(P, CX, CY);
		int32 Best = INDEX_NONE;
		float BestD = TNumericLimits<float>::Max();
		for (int32 Y = CY - Radius; Y <= CY + Radius; ++Y)
		{
			for (int32 X = CX - Radius; X <= CX + Radius; ++X)
			{
				if (!Reached(X, Y)) continue;
				const float D = FVector2D::DistSquared(Centre(X, Y), P);
				if (D < BestD) { BestD = D; Best = Index(X, Y); }
			}
		}
		return Best;
	}
};

struct FStep
{
	enum class EKind : uint8 { Walk, Path, Teleport, Lift, Shot };
	EKind Kind = EKind::Walk;
	FVector Target = FVector::ZeroVector; // X, Y and the floor surface Z
	int32 Floor = 0;
	float Yaw = 0.0f;
	bool bCheckZ = true;
	FString Label;
};

struct FState
{
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AKcHUD> Hud;
	TWeakObjectPtr<APlayerController> PC;
	TWeakObjectPtr<ACharacter> Me;
	TMap<int32, FGrid> Grids;
	TArray<FStep> Steps;
	int32 Index = 0;
	bool bStarted = false;
	double StepStart = 0.0;
	double LastProgress = 0.0;
	float BestDist = 0.0f;
	float FallTime = 0.0f;
	int32 ShotPhase = 0;
	int32 ShotCount = 0;
	// Legs: from one labelled target to the next.
	double LegStart = 0.0;
	float LegDistance = 0.0f;
	FVector LastPos = FVector::ZeroVector;
	TArray<FString> Lines;
	bool bPass = true;
	int32 Stuck = 0;
	int32 Falls = 0;
	int32 ScreensClosed = 0;
	float Radius = 34.0f, HalfHeight = 90.0f, StepUp = 45.0f, WalkableZ = 0.71f;

	void Check(const FString& Name, bool bOk, const FString& Detail)
	{
		bPass &= bOk;
		Lines.Add(FString::Printf(TEXT("%s %s: %s"), bOk ? TEXT("ok  ") : TEXT("FAIL"), *Name, *Detail));
	}
	void Note(const FString& Line) { Lines.Add(TEXT("     ") + Line); }
};

FString Where(const FVector& P)
{
	return FString::Printf(TEXT("(%.0f, %.0f, %.0f) floor %s"), P.X, P.Y, P.Z, *AKcHotel::FloorLabel(AKcHotel::FloorAt(P.Z)));
}

FString ZoneAt(int32 Floor, const FVector2D& P)
{
	for (const AKcHotel::FZone& Z : AKcHotel::Zones())
	{
		if (Z.Floor == Floor && Z.Bounds.IsInside(P)) return Z.Location;
	}
	return TEXT("-");
}

FString NameOf(const AKcInteractable* A)
{
	if (const AKcPersonActor* P = Cast<AKcPersonActor>(A)) return TEXT("person ") + P->PersonId;
	if (const AKcCasePoint* C = Cast<AKcCasePoint>(A)) return TEXT("case point '") + (C->Title.IsEmpty() ? C->Spot : C->Title) + TEXT("'");
	if (const AKcSign* S = Cast<AKcSign>(A)) return TEXT("sign '") + S->Title + TEXT("'");
	if (Cast<AKcLiftPanel>(A)) return TEXT("lift panel");
	if (Cast<AKcReportTerminal>(A)) return TEXT("LP terminal");
	return A->GetName();
}

// ---------------------------------------------------------------- reach

void BuildGrid(FState& S, UWorld* World, ACharacter* Me, int32 Floor, const FVector2D& Seed)
{
	FGrid& G = S.Grids.Add(Floor);
	G.Floor = Floor;
	G.Kind.Init(ECell::Void, NX * NY);
	G.Height.Init(0.0f, NX * NY);
	G.Region.Init(-1, NX * NY);
	UCapsuleComponent* Cap = Me->GetCapsuleComponent();
	FCollisionQueryParams Query(SCENE_QUERY_STAT(KcWalkReach), false, Me);
	FCollisionResponseParams Response;
	Cap->InitSweepCollisionParams(Query, Response);
	const ECollisionChannel Channel = Cap->GetCollisionObjectType();
	const FCollisionShape Body = FCollisionShape::MakeCapsule(S.Radius, S.HalfHeight);
	const float Base = AKcHotel::FloorZ(Floor);
	for (int32 Y = 0; Y < NY; ++Y)
	{
		for (int32 X = 0; X < NX; ++X)
		{
			const FVector2D C = FGrid::Centre(X, Y);
			// The floor under the centre, from step height down; then room for the whole body above it.
			FHitResult Hit;
			if (!World->LineTraceSingleByChannel(Hit, FVector(C.X, C.Y, Base + S.StepUp), FVector(C.X, C.Y, Base - 30.0f), Channel, Query, Response)) continue;
			const int32 I = FGrid::Index(X, Y);
			if (Hit.bStartPenetrating || Hit.ImpactNormal.Z < S.WalkableZ)
			{
				G.Kind[I] = ECell::Blocked;
				continue;
			}
			const float H = Hit.ImpactPoint.Z - Base;
			const FVector Body0(C.X, C.Y, Hit.ImpactPoint.Z + S.HalfHeight + 2.5f);
			G.Kind[I] = World->OverlapBlockingTestByChannel(Body0, FQuat::Identity, Channel, Body, Query, Response) ? ECell::Blocked : ECell::Stand;
			G.Height[I] = H;
		}
	}
	// Flood fill: neighbours both standable and within a step of each other. Two standable capsules
	// 20 cm apart cover the gap between them, so no wall fits between connected cells.
	auto Fill = [&G, &S](int32 Start, int32 Region)
	{
		int32 N = 0;
		TArray<int32> Open;
		Open.Add(Start);
		G.Region[Start] = Region;
		while (!Open.IsEmpty())
		{
			const int32 I = Open.Pop(EAllowShrinking::No);
			++N;
			const int32 X = I % NX, Y = I / NX;
			const int32 Next[4][2] = {{X + 1, Y}, {X - 1, Y}, {X, Y + 1}, {X, Y - 1}};
			for (const auto& Nb : Next)
			{
				if (Nb[0] < 0 || Nb[1] < 0 || Nb[0] >= NX || Nb[1] >= NY) continue;
				const int32 J = FGrid::Index(Nb[0], Nb[1]);
				if (G.Region[J] != -1 || G.Kind[J] != ECell::Stand || FMath::Abs(G.Height[J] - G.Height[I]) > S.StepUp) continue;
				G.Region[J] = Region;
				Open.Add(J);
			}
		}
		return N;
	};
	int32 SX, SY;
	FGrid::CellOf(Seed, SX, SY);
	int32 SeedCell = INDEX_NONE;
	for (int32 R = 0; R <= 4 && SeedCell == INDEX_NONE; ++R)
	{
		for (int32 Y = SY - R; Y <= SY + R && SeedCell == INDEX_NONE; ++Y)
		{
			for (int32 X = SX - R; X <= SX + R; ++X)
			{
				if (X >= 0 && Y >= 0 && X < NX && Y < NY && G.Kind[FGrid::Index(X, Y)] == ECell::Stand) { SeedCell = FGrid::Index(X, Y); break; }
			}
		}
	}
	const FString Label = AKcHotel::FloorLabel(Floor);
	S.Check(TEXT("seed ") + Label, SeedCell != INDEX_NONE, FString::Printf(TEXT("standing room at (%.0f, %.0f)"), Seed.X, Seed.Y));
	if (SeedCell == INDEX_NONE) return;
	const int32 Reached = Fill(SeedCell, 0);
	// The rest: islands of floor the player cannot walk to (closed rooms, or a blocked way in).
	int32 Standable = Reached;
	for (int32 I = 0; I < NX * NY; ++I)
	{
		if (G.Kind[I] != ECell::Stand || G.Region[I] != -1) continue;
		const int32 N = Fill(I, ++G.Islands);
		Standable += N;
		if (N * Cell * Cell >= 2.0e4f) // 2 m2 and more
		{
			FBox2D Box(ForceInit);
			for (int32 J = 0; J < NX * NY; ++J) if (G.Region[J] == G.Islands) Box += FGrid::Centre(J % NX, J / NX);
			S.Note(FString::Printf(TEXT("floor %s island %.1f m2, X %.0f..%.0f Y %.0f..%.0f (%s): standable but not reachable on foot"), *Label, N * Cell * Cell / 1.0e4f,
				Box.Min.X, Box.Max.X, Box.Min.Y, Box.Max.Y, *ZoneAt(Floor, Box.GetCenter())));
		}
	}
	S.Note(FString::Printf(TEXT("floor %s: %.0f m2 reachable on foot, %.0f m2 standable"), *Label, Reached * Cell * Cell / 1.0e4f, Standable * Cell * Cell / 1.0e4f));
	// Every place on this floor: walkable area.
	TMap<FString, int32> PerZone;
	for (int32 I = 0; I < NX * NY; ++I)
	{
		if (G.Region[I] == 0) PerZone.FindOrAdd(ZoneAt(Floor, FGrid::Centre(I % NX, I / NX)))++;
	}
	for (const AKcHotel::FZone& Z : AKcHotel::Zones())
	{
		if (Z.Floor != Floor) continue;
		const int32 N = PerZone.FindRef(Z.Location);
		S.Check(TEXT("walkable ") + Z.Location, N > 0, FString::Printf(TEXT("%.1f m2 reachable on foot on floor %s"), N * Cell * Cell / 1.0e4f, *Label));
	}
}

// Usable from somewhere reachable: in range and in sight of the eye of a player standing there.
bool CanUse(const FState& S, UWorld* World, ACharacter* Me, const FGrid& G, AKcInteractable* A, float Range, FVector& OutFrom)
{
	FBox Box = A->GetComponentsBoundingBox(false);
	if (!Box.IsValid) Box = FBox(A->GetActorLocation() - FVector(5.0f), A->GetActorLocation() + FVector(5.0f));
	const FVector Aim = Box.GetCenter();
	const float Reach2D = Range + Box.GetExtent().Size2D();
	TArray<TPair<float, int32>> Cells;
	int32 X0, Y0, X1, Y1;
	FGrid::CellOf(FVector2D(Aim) - FVector2D(Reach2D), X0, Y0);
	FGrid::CellOf(FVector2D(Aim) + FVector2D(Reach2D), X1, Y1);
	for (int32 Y = FMath::Max(0, Y0); Y <= FMath::Min(NY - 1, Y1); ++Y)
	{
		for (int32 X = FMath::Max(0, X0); X <= FMath::Min(NX - 1, X1); ++X)
		{
			if (!G.Reached(X, Y)) continue;
			const float D = FVector2D::Distance(FGrid::Centre(X, Y), FVector2D(Aim));
			if (D <= Reach2D) Cells.Add({D, FGrid::Index(X, Y)});
		}
	}
	Cells.Sort([](const TPair<float, int32>& L, const TPair<float, int32>& R) { return L.Key < R.Key; });
	FCollisionQueryParams Query(SCENE_QUERY_STAT(KcWalkUse), false, Me);
	int32 Tries = 0;
	for (const TPair<float, int32>& C : Cells)
	{
		if (++Tries > 400) break;
		const FVector2D P = FGrid::Centre(C.Value % NX, C.Value / NX);
		const FVector Eye(P.X, P.Y, AKcHotel::FloorZ(G.Floor) + G.Height[C.Value] + S.HalfHeight + 2.5f + EyeAboveCentre);
		const FVector Dir = (Aim - Eye).GetSafeNormal();
		FHitResult Hit;
		if (World->SweepSingleByChannel(Hit, Eye, Eye + Dir * Range, FQuat::Identity, ECC_Visibility, FCollisionShape::MakeSphere(9.0f), Query) && Hit.GetActor() == A)
		{
			OutFrom = Eye;
			return true;
		}
	}
	return false;
}

void SaveMap(const FState& S, const FGrid& G, const TArray<TPair<FVector, bool>>& Uses, const TArray<FVector>& Arrivals)
{
	const int32 W = NX * MapScale, H = NY * MapScale;
	TArray<FColor> Px;
	Px.Init(FColor(16, 16, 18), W * H);
	auto Put = [&](int32 X, int32 Y, const FColor& C)
	{
		if (X >= 0 && Y >= 0 && X < W && Y < H) Px[(H - 1 - Y) * W + X] = C; // north up
	};
	for (int32 I = 0; I < NX * NY; ++I)
	{
		FColor C(16, 16, 18);
		if (G.Kind[I] == ECell::Blocked) C = FColor(70, 70, 76);
		else if (G.Kind[I] == ECell::Stand) C = G.Region[I] == 0 ? FColor(70, 170, 90) : FColor(220, 150, 40);
		for (int32 dy = 0; dy < MapScale; ++dy)
			for (int32 dx = 0; dx < MapScale; ++dx) Put((I % NX) * MapScale + dx, (I / NX) * MapScale + dy, C);
	}
	auto Dot = [&](const FVector& P, const FColor& C, int32 R)
	{
		const int32 X = FMath::RoundToInt((P.X - GridX0) / Cell * MapScale), Y = FMath::RoundToInt((P.Y - GridY0) / Cell * MapScale);
		for (int32 dy = -R; dy <= R; ++dy)
			for (int32 dx = -R; dx <= R; ++dx) if (dx * dx + dy * dy <= R * R) Put(X + dx, Y + dy, C);
	};
	for (const FVector& A : Arrivals) Dot(A, FColor::White, 4);
	for (const TPair<FVector, bool>& U : Uses) Dot(U.Key, U.Value ? FColor(60, 140, 255) : FColor(255, 40, 40), 3);
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("WalkMaps") / FString::Printf(TEXT("Floor_%s.png"), *AKcHotel::FloorLabel(G.Floor)));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
	FImageView View(Px.GetData(), W, H);
	FImageUtils::SaveImageByExtension(*Path, View);
}

void CheckReach(FState& S, UWorld* World, AKcHUD* Hud, ACharacter* Me)
{
	AKcHotel* Hotel = Hud->Hotel();
	const float Range = Me->FindComponentByClass<UKcInteractionComponent>() ? Me->FindComponentByClass<UKcInteractionComponent>()->Range : 300.0f;
	for (int32 F = AKcHotel::LowestFloor; F <= AKcHotel::TopFloor; ++F)
	{
		BuildGrid(S, World, Me, F, AKcHotel::IsFittedFloor(F) ? LiftLobby : StairLanding);
		const FGrid& G = S.Grids[F];
		const FString Label = AKcHotel::FloorLabel(F);
		if (AKcHotel::IsFittedFloor(F))
		{
			S.Check(TEXT("stair from lobby ") + Label, G.NearestReached(StairLanding, 2) != INDEX_NONE, TEXT("Stair A landing reachable from the lift lobby"));
		}
		TArray<FVector> Arrivals;
		auto CheckPoint = [&](const FString& Name, const FVector& At)
		{
			Arrivals.Add(At);
			S.Check(Name, G.NearestReached(FVector2D(At), 2) != INDEX_NONE, Where(At));
		};
		for (const AKcHotel::FZone& Z : AKcHotel::Zones())
		{
			if (Z.Floor != F) continue;
			const FVector At = Hotel->GetArrival(Z.Location).GetLocation();
			if (AKcHotel::FloorAt(At.Z - 100.0f) == F) CheckPoint(TEXT("arrival ") + Z.Location, At);
		}
		if (F == AKcHotel::LowestFloor) CheckPoint(TEXT("player start"), Hotel->GetPlayerStart().GetLocation());
		TArray<TPair<FVector, bool>> Uses;
		for (TActorIterator<AKcInteractable> It(World); It; ++It)
		{
			AKcInteractable* A = *It;
			if (A->IsHidden()) continue;
			const FBox Box = A->GetComponentsBoundingBox(false);
			const float Bottom = Box.IsValid ? Box.Min.Z : A->GetActorLocation().Z;
			if (FMath::FloorToInt((Bottom + 30.0f) / AKcHotel::FloorHeight) != F) continue;
			FVector From;
			const bool bOk = CanUse(S, World, Me, G, A, Range, From);
			Uses.Add({Box.IsValid ? Box.GetCenter() : A->GetActorLocation(), bOk});
			S.Check(TEXT("use ") + NameOf(A), bOk, bOk ? FString::Printf(TEXT("from (%.0f, %.0f) on %s"), From.X, From.Y, *Label)
				: TEXT("not usable from anywhere reachable on foot, at ") + Where(Box.IsValid ? Box.GetCenter() : A->GetActorLocation()));
		}
		SaveMap(S, G, Uses, Arrivals);
	}
}

// ---------------------------------------------------------------- walking

// A path through reached cells, pulled tight: corners only.
bool PlanPath(const FGrid& G, const FVector2D& From, const FVector2D& To, TArray<FVector>& Out)
{
	const int32 A = G.NearestReached(From, 4), B = G.NearestReached(To, 4);
	if (A == INDEX_NONE || B == INDEX_NONE) return false;
	TArray<int32> Parent;
	Parent.Init(INDEX_NONE, NX * NY);
	TArray<int32> Open;
	Open.Add(A);
	Parent[A] = A;
	for (int32 Head = 0; Head < Open.Num() && Parent[B] == INDEX_NONE; ++Head)
	{
		const int32 I = Open[Head], X = I % NX, Y = I / NX;
		const int32 Next[4][2] = {{X + 1, Y}, {X - 1, Y}, {X, Y + 1}, {X, Y - 1}};
		for (const auto& Nb : Next)
		{
			if (!G.Reached(Nb[0], Nb[1])) continue;
			const int32 J = FGrid::Index(Nb[0], Nb[1]);
			if (Parent[J] != INDEX_NONE || FMath::Abs(G.Height[J] - G.Height[I]) > 45.0f) continue;
			Parent[J] = I;
			Open.Add(J);
		}
	}
	if (Parent[B] == INDEX_NONE) return false;
	TArray<int32> Cells;
	for (int32 I = B; I != A; I = Parent[I]) Cells.Add(I);
	Cells.Add(A);
	Algo::Reverse(Cells);
	auto Clear = [&G](int32 I, int32 J)
	{
		const FVector2D P = FGrid::Centre(I % NX, I / NX), Q = FGrid::Centre(J % NX, J / NX);
		const int32 N = FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(P, Q) / 8.0f));
		float Prev = G.Height[I];
		for (int32 k = 1; k <= N; ++k)
		{
			int32 X, Y;
			FGrid::CellOf(FMath::Lerp(P, Q, float(k) / N), X, Y);
			if (!G.Reached(X, Y)) return false;
			const float H = G.Height[FGrid::Index(X, Y)];
			if (FMath::Abs(H - Prev) > 45.0f) return false;
			Prev = H;
		}
		return true;
	};
	const float Base = AKcHotel::FloorZ(G.Floor);
	int32 Anchor = 0;
	while (Anchor < Cells.Num() - 1)
	{
		int32 Far = Anchor + 1;
		for (int32 j = Cells.Num() - 1; j > Anchor + 1; --j)
		{
			if (Clear(Cells[Anchor], Cells[j])) { Far = j; break; }
		}
		const int32 I = Cells[Far];
		const FVector2D P = FGrid::Centre(I % NX, I / NX);
		Out.Add(FVector(P.X, P.Y, Base + G.Height[I]));
		Anchor = Far;
	}
	if (!Out.IsEmpty()) Out.Last() = FVector(To.X, To.Y, Out.Last().Z);
	return true;
}

FStep Walk(const FVector2D& P, float SurfaceZ, const FString& Label = FString(), bool bCheckZ = true)
{
	FStep S;
	S.Kind = FStep::EKind::Walk;
	S.Target = FVector(P.X, P.Y, SurfaceZ);
	S.bCheckZ = bCheckZ;
	S.Label = Label;
	return S;
}

FStep PathTo(int32 Floor, const FVector2D& P, const FString& Label)
{
	FStep S;
	S.Kind = FStep::EKind::Path;
	S.Floor = Floor;
	S.Target = FVector(P.X, P.Y, AKcHotel::FloorZ(Floor));
	S.Label = Label;
	return S;
}

FStep Shot(float Yaw, const FString& Label)
{
	FStep S;
	S.Kind = FStep::EKind::Shot;
	S.Yaw = Yaw;
	S.Label = Label;
	return S;
}

FStep Lift(int32 Floor)
{
	FStep S;
	S.Kind = FStep::EKind::Lift;
	S.Floor = Floor;
	S.Label = TEXT("lift to ") + AKcHotel::FloorLabel(Floor);
	return S;
}

FString FileSafe(const FString& Name)
{
	FString Out;
	for (TCHAR C : Name) Out.AppendChar(FChar::IsAlnum(C) ? C : TEXT('_'));
	return Out;
}

void BuildRoute(FState& S, AKcHotel* Hotel)
{
	auto Visit = [&S, Hotel](const FString& Location)
	{
		const FTransform T = Hotel->GetArrival(Location);
		const int32 Floor = AKcHotel::FloorAt(T.GetLocation().Z - 100.0f);
		S.Steps.Add(PathTo(Floor, FVector2D(T.GetLocation()), Location));
		S.Steps.Add(Shot(T.Rotator().Yaw, Location));
	};
	const FTransform Start = Hotel->GetPlayerStart();
	FStep Tp;
	Tp.Kind = FStep::EKind::Teleport;
	Tp.Target = Start.GetLocation() - FVector(0.0f, 0.0f, 100.0f);
	Tp.Yaw = Start.Rotator().Yaw;
	Tp.Label = TEXT("player start");
	S.Steps.Add(Tp);
	S.Steps.Add(Shot(Start.Rotator().Yaw, TEXT("player start")));
	for (const TCHAR* L : {TEXT("cctv_room"), TEXT("loading_dock"), TEXT("housekeeping"), TEXT("back_of_house")}) Visit(L);

	// Up Stair A from B1 to 14 on foot: through the door, up the north flight to the mid landing, back up
	// the south flight to the next floor's landing.
	S.Steps.Add(PathTo(AKcHotel::LowestFloor, StairDoorOut, TEXT("stair door B1")));
	auto Z = [](int32 Floor, float Up) { return AKcHotel::FloorZ(Floor) + Up; };
	S.Steps.Add(Walk(StairDoorIn, Z(AKcHotel::LowestFloor, 0.0f), TEXT("into Stair A at B1")));
	for (int32 F = AKcHotel::LowestFloor; F < AKcHotel::TopFloor; ++F)
	{
		S.Steps.Add(Walk(FVector2D(2620.0f, -236.0f), Z(F, 200.0f)));
		S.Steps.Add(Walk(FVector2D(2620.0f, -387.0f), Z(F, 200.0f)));
		S.Steps.Add(Walk(FVector2D(2190.0f, -387.0f), Z(F + 1, 0.0f)));
		S.Steps.Add(Walk(StairLanding, Z(F + 1, 0.0f), TEXT("Stair A landing ") + AKcHotel::FloorLabel(F + 1)));
		S.Steps.Add(Shot(180.0f, TEXT("Stair A ") + AKcHotel::FloorLabel(F + 1)));
		S.Steps.Add(Walk(StairDoorIn, Z(F + 1, 0.0f)));
	}
	S.Steps.Add(Walk(StairDoorOut, Z(AKcHotel::TopFloor, 0.0f), TEXT("out of Stair A at 14")));
	for (const TCHAR* L : {TEXT("f14_corridor"), TEXT("room_1412"), TEXT("f14_service"), TEXT("guest_room")}) Visit(L);

	// Down again, 14 to B1.
	S.Steps.Add(PathTo(AKcHotel::TopFloor, StairDoorOut, TEXT("stair door 14")));
	S.Steps.Add(Walk(StairDoorIn, Z(AKcHotel::TopFloor, 0.0f), TEXT("into Stair A at 14")));
	for (int32 F = AKcHotel::TopFloor; F > AKcHotel::LowestFloor; --F)
	{
		S.Steps.Add(Walk(FVector2D(2190.0f, -387.0f), Z(F, 0.0f)));
		S.Steps.Add(Walk(FVector2D(2620.0f, -387.0f), Z(F - 1, 200.0f)));
		S.Steps.Add(Walk(FVector2D(2620.0f, -236.0f), Z(F - 1, 200.0f)));
		S.Steps.Add(Walk(StairDoorIn, Z(F - 1, 0.0f), TEXT("Stair A down to ") + AKcHotel::FloorLabel(F - 1)));
	}
	S.Steps.Add(Walk(StairDoorOut, Z(AKcHotel::LowestFloor, 0.0f), TEXT("out of Stair A at B1")));

	// The lift to every fitted floor and on foot to each place there.
	S.Steps.Add(PathTo(AKcHotel::LowestFloor, LiftLobby, TEXT("lift lobby B1")));
	S.Steps.Add(Lift(0));
	for (const TCHAR* L : {TEXT("lobby"), TEXT("front_desk"), TEXT("bell_stand"), TEXT("lobby_bar"), TEXT("fire_command"), TEXT("kitchen")}) Visit(L);
	S.Steps.Add(PathTo(0, LiftLobby, TEXT("lift lobby G")));
	S.Steps.Add(Lift(1));
	Visit(TEXT("ballroom"));
	S.Steps.Add(PathTo(1, LiftLobby, TEXT("lift lobby 1")));
	S.Steps.Add(Lift(4));
	for (const TCHAR* L : {TEXT("exec_offices"), TEXT("gm_office"), TEXT("boardroom")}) Visit(L);
	S.Steps.Add(PathTo(4, LiftLobby, TEXT("lift lobby 4")));
	S.Steps.Add(Lift(14));
	S.Steps.Add(Lift(-1));
	S.Steps.Add(PathTo(AKcHotel::LowestFloor, FVector2D(Start.GetLocation()), TEXT("back to the LP office")));
}

void Finish(const TSharedRef<FState>& S)
{
	S->Check(TEXT("stuck"), S->Stuck == 0, FString::Printf(TEXT("%d legs stuck"), S->Stuck));
	S->Check(TEXT("falls"), S->Falls == 0, FString::Printf(TEXT("%d falls"), S->Falls));
	if (S->ScreensClosed > 0) S->Note(FString::Printf(TEXT("closed %d screens that opened during the walk"), S->ScreensClosed));
	FString Text = FString::Printf(TEXT("%s\nmode=walk\n"), S->bPass ? TEXT("PASS") : TEXT("FAIL"));
	for (const FString& L : S->Lines) Text += L + TEXT("\n");
	const FString Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("KcWalk.txt"));
	FFileHelper::SaveStringToFile(Text, *Path, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	UE_LOG(LogKeycard, Display, TEXT("KcSelfTest wrote %s:\n%s"), *Path, *Text);
	FPlatformMisc::RequestExit(false, TEXT("KcSelfTest"));
}

void Teleport(FState& S, const FVector& Surface, float Yaw)
{
	ACharacter* Me = S.Me.Get();
	Me->SetActorLocation(Surface + FVector(0.0f, 0.0f, S.HalfHeight + 3.0f), false, nullptr, ETeleportType::TeleportPhysics);
	Me->GetCharacterMovement()->StopMovementImmediately();
	S.PC->SetControlRotation(FRotator(0.0f, Yaw, 0.0f));
	if (S.PC->PlayerCameraManager) S.PC->PlayerCameraManager->SetGameCameraCutThisFrame();
	S.LastPos = Me->GetActorLocation();
}

// One frame of the walk. False when finished.
bool Tick(const TSharedRef<FState>& SRef, float DeltaSeconds)
{
	FState& S = *SRef;
	UWorld* World = S.World.Get();
	AKcHUD* Hud = S.Hud.Get();
	ACharacter* Me = S.Me.Get();
	if (!World || !Hud || !Me || !S.PC.IsValid())
	{
		S.Check(TEXT("world"), false, TEXT("the player or the HUD went away"));
		Finish(SRef);
		return false;
	}
	if (Hud->IsScreenOpen())
	{
		Hud->CloseScreen();
		++S.ScreensClosed;
	}
	if (S.Index >= S.Steps.Num())
	{
		Finish(SRef);
		return false;
	}
	const double Now = World->GetTimeSeconds();
	FStep& Step = S.Steps[S.Index];
	const FVector Pos = Me->GetActorLocation();
	S.LegDistance += FVector::Dist2D(Pos, S.LastPos);
	S.LastPos = Pos;
	if (!S.bStarted)
	{
		S.bStarted = true;
		S.StepStart = Now;
		S.LastProgress = Now;
		S.BestDist = TNumericLimits<float>::Max();
		S.ShotPhase = 0;
		S.FallTime = 0.0f;
		if (Step.Kind == FStep::EKind::Path)
		{
			// Expand into walks along the floor's map.
			const FGrid* G = S.Grids.Find(Step.Floor);
			TArray<FVector> Points;
			const bool bOk = G && AKcHotel::FloorAt(Pos.Z - S.HalfHeight) == Step.Floor && PlanPath(*G, FVector2D(Pos), FVector2D(Step.Target), Points);
			S.Check(TEXT("path ") + Step.Label, bOk, bOk ? FString::Printf(TEXT("%d corners"), Points.Num()) : TEXT("no way on foot from ") + Where(Pos));
			const FStep Done = Step;
			if (!bOk)
			{
				// Carry on from there so the rest of the walk still runs.
				Teleport(S, Done.Target, 0.0f);
				++S.Index;
				S.bStarted = false;
				return true;
			}
			TArray<FStep> Walks;
			for (int32 i = 0; i < Points.Num(); ++i) Walks.Add(Walk(FVector2D(Points[i]), Points[i].Z, i == Points.Num() - 1 ? Done.Label : FString(), i == Points.Num() - 1));
			S.Steps.RemoveAt(S.Index);
			S.Steps.Insert(Walks, S.Index);
			S.bStarted = false;
			return true;
		}
		if (Step.Kind == FStep::EKind::Teleport)
		{
			Teleport(S, Step.Target, Step.Yaw);
			S.LegStart = Now;
			S.LegDistance = 0.0f;
		}
		if (Step.Kind == FStep::EKind::Lift)
		{
			Hud->RideLift(Step.Floor);
		}
	}
	const double T = Now - S.StepStart;
	auto Next = [&S]()
	{
		++S.Index;
		S.bStarted = false;
	};
	switch (Step.Kind)
	{
	case FStep::EKind::Teleport:
		if (T > 0.5) Next();
		break;
	case FStep::EKind::Lift:
		if (T > 2.2)
		{
			const int32 Floor = AKcHotel::FloorAt(Pos.Z - S.HalfHeight);
			const bool bOk = Floor == Step.Floor && Me->GetCharacterMovement()->IsMovingOnGround();
			S.Check(Step.Label, bOk, FString::Printf(TEXT("now at %s, %s"), *Where(Pos), Me->GetCharacterMovement()->IsMovingOnGround() ? TEXT("standing") : TEXT("not on the ground")));
			S.LegStart = Now;
			S.LegDistance = 0.0f;
			Next();
		}
		break;
	case FStep::EKind::Shot:
		S.PC->SetControlRotation(FRotator(-6.0f, Step.Yaw, 0.0f));
		if (S.ShotPhase == 0 && T > 0.6)
		{
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Walk_%02d_%s"), ++S.ShotCount, *FileSafe(Step.Label)), false, false);
			S.ShotPhase = 1;
		}
		else if (S.ShotPhase == 1 && T > 1.0)
		{
			Next();
		}
		break;
	case FStep::EKind::Walk:
	{
		const FVector2D To(Step.Target.X - Pos.X, Step.Target.Y - Pos.Y);
		const float Dist = To.Size();
		UCharacterMovementComponent* Move = Me->GetCharacterMovement();
		S.FallTime = Move->IsFalling() ? S.FallTime + DeltaSeconds : 0.0f;
		if (S.FallTime > 0.6f)
		{
			++S.Falls;
			S.Check(TEXT("fall"), false, TEXT("falling on the way to ") + Where(Step.Target) + TEXT(" from ") + Where(Pos));
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Walk_%02d_FALL"), ++S.ShotCount), false, false);
			Teleport(S, Step.Target, S.PC->GetControlRotation().Yaw);
			Next();
			break;
		}
		if (Dist < 20.0f)
		{
			if (Step.bCheckZ)
			{
				const float Feet = Pos.Z - S.HalfHeight;
				const bool bOk = FMath::Abs(Feet - Step.Target.Z) < 12.0f;
				if (!bOk || !Step.Label.IsEmpty())
				{
					S.Check(TEXT("reach ") + (Step.Label.IsEmpty() ? Where(Step.Target) : Step.Label), bOk,
						FString::Printf(TEXT("feet at %.0f, expected %.0f; %.1f m in %.1f s"), Feet, Step.Target.Z, S.LegDistance / 100.0f, Now - S.LegStart));
				}
			}
			if (!Step.Label.IsEmpty())
			{
				S.LegStart = Now;
				S.LegDistance = 0.0f;
			}
			Next();
			break;
		}
		if (Dist < S.BestDist - 10.0f)
		{
			S.BestDist = Dist;
			S.LastProgress = Now;
		}
		if (Now - S.LastProgress > 3.0)
		{
			++S.Stuck;
			S.Check(TEXT("stuck"), false, FString::Printf(TEXT("at %s, %.0f cm short of (%.0f, %.0f)%s"), *Where(Pos), Dist, Step.Target.X, Step.Target.Y,
				Step.Label.IsEmpty() ? TEXT("") : *(TEXT(" on the way to ") + Step.Label)));
			FScreenshotRequest::RequestScreenshot(FString::Printf(TEXT("Walk_%02d_STUCK"), ++S.ShotCount), false, false);
			Teleport(S, Step.Target, S.PC->GetControlRotation().Yaw);
			Next();
			break;
		}
		const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(To.Y, To.X));
		const FRotator Now3 = S.PC->GetControlRotation();
		S.PC->SetControlRotation(FRotator(0.0f, FMath::FixedTurn(Now3.Yaw, Yaw, 360.0f * DeltaSeconds), 0.0f));
		Me->AddMovementInput(FVector(To.X, To.Y, 0.0f).GetSafeNormal(), 1.0f, true);
		break;
	}
	case FStep::EKind::Path:
		Next();
		break;
	}
	return true;
}
} // namespace

void Run(UWorld* World, AKcHUD* Hud)
{
	APlayerController* PC = World->GetFirstPlayerController();
	ACharacter* Me = PC ? Cast<ACharacter>(PC->GetPawn()) : nullptr;
	AKcHotel* Hotel = Hud->Hotel();
	TSharedRef<FState> S = MakeShared<FState>();
	if (!Me || !Hotel)
	{
		S->Check(TEXT("setup"), false, TEXT("no player character or hotel"));
		Finish(S);
		return;
	}
	Hud->StartNewGame(EKcDifficulty::Professional);
	if (Hud->IsScreenOpen()) Hud->CloseScreen();
	S->World = World;
	S->Hud = Hud;
	S->PC = PC;
	S->Me = Me;
	UCharacterMovementComponent* Move = Me->GetCharacterMovement();
	S->Radius = Me->GetCapsuleComponent()->GetScaledCapsuleRadius();
	S->HalfHeight = Me->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	S->StepUp = Move->MaxStepHeight;
	S->WalkableZ = Move->GetWalkableFloorZ();
	const FIntPoint Size = World->GetGameViewport() ? World->GetGameViewport()->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	S->Note(FString::Printf(TEXT("resolution=%dx%d capsule r=%.0f h=%.0f eye=%.0f cm walk=%.0f cm/s step=%.0f cm walkable=%.0f deg fov=%.0f"), Size.X, Size.Y, S->Radius,
		S->HalfHeight * 2.0f, S->HalfHeight + EyeAboveCentre, Move->MaxWalkSpeed, S->StepUp, FMath::RadiansToDegrees(FMath::Acos(S->WalkableZ)),
		PC->PlayerCameraManager ? PC->PlayerCameraManager->GetFOVAngle() : 0.0f));
	const double T0 = FPlatformTime::Seconds();
	CheckReach(*S, World, Hud, Me);
	S->Note(FString::Printf(TEXT("reach maps in %.1f s (Saved/WalkMaps)"), FPlatformTime::Seconds() - T0));
	BuildRoute(*S, Hotel);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([S](float Dt) { return Tick(S, Dt); }), 0.0f);
}
} // namespace KcWalkTest
