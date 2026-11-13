#include "Bots/BotBrain.h"

namespace
{
	float YawOf(const FVector2D& D)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	}

	FVector2D RightOf(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg + 90.f);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}
}

namespace FPSBots
{
	const TCHAR* BehaviourName(EBotBehaviour Behaviour)
	{
		switch (Behaviour)
		{
		case EBotBehaviour::Static:  return TEXT("Static");
		case EBotBehaviour::Strafe:  return TEXT("Strafe");
		case EBotBehaviour::Peek:    return TEXT("Peek");
		case EBotBehaviour::Crouch:  return TEXT("Crouch");
		case EBotBehaviour::Patrol:  return TEXT("Patrol");
		case EBotBehaviour::Explore: return TEXT("Explore");
		}
		return TEXT("Unknown");
	}

	void FBotBrain::Start(const FBotSetup& InSetup, const FMapSpec& InMap, const FPSNav::FTileGraph& InGraph, int32 Seed, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		*this = FBotBrain();
		Setup = InSetup;
		Map = &InMap;
		Graph = &InGraph;
		Rng.Initialize(Seed);
		Anchor = Pos;
		FacingYaw = Setup.FacingYawDeg;
		LookYaw = FacingYaw;
		bCrouchNow = Setup.bCrouched;
		LastUpdate = Now;
		VisitTime.Init(-1.0e9, Graph->Rows * Graph->Cols);
		if (Setup.Behaviour == EBotBehaviour::Peek && !(Graph->IsWalkable(Setup.PeekHidden) && Graph->IsWalkable(Setup.PeekExposed)))
		{
			Setup.Behaviour = EBotBehaviour::Strafe; // no cover pair (D-031)
		}
		StrafeSign = Rng.FRand() < 0.5f ? -1.f : 1.f;
		switch (Setup.Behaviour)
		{
		case EBotBehaviour::Strafe: NextToggle = Now + Rng.FRandRange(B.StrafePeriodMinSeconds, B.StrafePeriodMaxSeconds); break;
		case EBotBehaviour::Crouch: NextToggle = Now + Rng.FRandRange(B.CrouchPeriodMinSeconds, B.CrouchPeriodMaxSeconds); break;
		case EBotBehaviour::Patrol: PickPatrolWaypoints(Pos, B); break;
		default: break;
		}
	}

	void FBotBrain::PickPatrolWaypoints(const FVector2D& Pos, const FBotBehaviourSettings& B)
	{
		const FIntPoint Here = FPSNav::TileAt(*Map, Pos);
		const TArray<FIntPoint> Tiles = Graph->WalkableTiles();
		for (int32 Try = 0; Try < B.PatrolWaypoints * 8 && Waypoints.Num() < B.PatrolWaypoints && Tiles.Num() > 1; ++Try)
		{
			const FIntPoint T = Tiles[Rng.RandRange(0, Tiles.Num() - 1)];
			if (T != Here && !Waypoints.Contains(T) && Graph->FindPath(Here, T).Num() > 0)
			{
				Waypoints.Add(T);
			}
		}
	}

	bool FBotBrain::PlanTo(const FIntPoint& Goal, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		const TArray<FIntPoint> Tiles = Graph->FindPath(FPSNav::TileAt(*Map, Pos), Goal);
		if (Tiles.Num() == 0)
		{
			return false;
		}
		TArray<FVector2D> NewPoints;
		for (int32 i = 1; i < Tiles.Num(); ++i)
		{
			FVector2D P = FPSNav::TileCenterLocal(*Map, Tiles[i]);
			if (i + 1 < Tiles.Num()) // seeded offsets vary the route; the final point is the exact tile centre
			{
				P += FVector2D(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)) * B.PathOffsetCm;
			}
			NewPoints.Add(P);
		}
		if (NewPoints.Num() == 0)
		{
			NewPoints.Add(FPSNav::TileCenterLocal(*Map, Goal));
		}
		PathPoints = MoveTemp(NewPoints);
		PathIndex = 0;
		BestDist = TNumericLimits<double>::Max();
		ProgressTime = Now;
		return true;
	}

	FVector2D FBotBrain::FollowPath(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B, bool& bArrived)
	{
		bArrived = false;
		auto Advance = [&]()
		{
			++PathIndex;
			BestDist = TNumericLimits<double>::Max();
			ProgressTime = Now;
		};
		while (PathIndex < PathPoints.Num() && FVector2D::Distance(Pos, PathPoints[PathIndex]) <= B.WaypointToleranceCm)
		{
			Advance();
		}
		if (PathIndex < PathPoints.Num())
		{
			const double D = FVector2D::Distance(Pos, PathPoints[PathIndex]);
			if (D < BestDist - 1.0)
			{
				BestDist = D;
				ProgressTime = Now;
			}
			else if (Now - ProgressTime > B.StuckSeconds)
			{
				Advance(); // stuck on something: skip this point
			}
		}
		if (PathIndex >= PathPoints.Num())
		{
			bArrived = true;
			return FVector2D::ZeroVector;
		}
		return (PathPoints[PathIndex] - Pos).GetSafeNormal();
	}

	FIntPoint FBotBrain::PickExploreTarget(const FVector2D& Pos, const FBotBehaviourSettings& B)
	{
		const TArray<FIntPoint> Tiles = Graph->WalkableTiles();
		const FIntPoint Here = FPSNav::TileAt(*Map, Pos);
		FIntPoint Best = Here;
		double BestTime = TNumericLimits<double>::Max();
		for (int32 k = 0; k < FMath::Max(1, B.ExploreCandidates) && Tiles.Num() > 0; ++k)
		{
			const FIntPoint T = Tiles[Rng.RandRange(0, Tiles.Num() - 1)];
			const double Visited = VisitTime[Graph->Index(T)];
			if (T != Here && Visited < BestTime)
			{
				BestTime = Visited;
				Best = T;
			}
		}
		return Best;
	}

	FIntPoint FBotBrain::NearestWalkable(const FIntPoint& Tile, const FVector2D& Target) const
	{
		if (Graph->IsWalkable(Tile))
		{
			return Tile;
		}
		FIntPoint Best(-1, -1);
		double BestD = TNumericLimits<double>::Max();
		for (int32 Dr = -2; Dr <= 2; ++Dr)
		{
			for (int32 Dc = -2; Dc <= 2; ++Dc)
			{
				const FIntPoint T = Tile + FIntPoint(Dr, Dc);
				if (!Graph->IsWalkable(T))
				{
					continue;
				}
				const double D = FVector2D::DistSquared(FPSNav::TileCenterLocal(*Map, T), Target);
				if (D < BestD)
				{
					BestD = D;
					Best = T;
				}
			}
		}
		return Best;
	}

	void FBotBrain::Investigate(const FVector2D& Target, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		if (!Map || !Graph)
		{
			return;
		}
		if (!IsMover())
		{
			FacingYaw = YawOf(Target - Pos); // hold the spot, face the stimulus
			return;
		}
		const FIntPoint Goal = NearestWalkable(FPSNav::TileAt(*Map, Target), Target);
		if (Goal.X >= 0 && PlanTo(Goal, Pos, Now, B))
		{
			Mode = EMode::Investigate;
		}
	}

	FBotIntent FBotBrain::Update(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		const float Dt = static_cast<float>(FMath::Max(0.0, Now - LastUpdate));
		LastUpdate = Now;
		FBotIntent I;
		if (!Map || !Graph)
		{
			return I;
		}
		VisitTime[Graph->Index(FPSNav::TileAt(*Map, Pos))] = Now;
		I.bCrouch = bCrouchNow;
		I.LookYawDeg = FacingYaw;

		if (Mode == EMode::LookAround)
		{
			LookYaw = FRotator::NormalizeAxis(LookYaw + B.InvestigateTurnDegPerSec * Dt);
			I.LookYawDeg = LookYaw;
			I.bCrouch = false;
			if (Now >= LookUntil)
			{
				Mode = EMode::Base;
				PathPoints.Reset();
			}
			return I;
		}
		if (Mode == EMode::Investigate)
		{
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			I.bCrouch = false;
			if (!I.MoveDir.IsNearlyZero())
			{
				FacingYaw = YawOf(I.MoveDir);
			}
			I.LookYawDeg = FacingYaw;
			if (bArrived)
			{
				Mode = EMode::LookAround;
				LookUntil = Now + B.InvestigateLookSeconds;
				LookYaw = FacingYaw;
			}
			return I;
		}

		switch (Setup.Behaviour)
		{
		case EBotBehaviour::Static:
			break;
		case EBotBehaviour::Crouch:
			if (Now >= NextToggle)
			{
				bCrouchNow = !bCrouchNow;
				NextToggle = Now + Rng.FRandRange(B.CrouchPeriodMinSeconds, B.CrouchPeriodMaxSeconds);
			}
			I.bCrouch = bCrouchNow;
			break;
		case EBotBehaviour::Strafe:
		{
			const FVector2D Right = RightOf(FacingYaw);
			const double Lateral = FVector2D::DotProduct(Pos - Anchor, Right);
			const bool bPast = (Lateral > B.StrafeHalfWidthCm && StrafeSign > 0.f) || (Lateral < -B.StrafeHalfWidthCm && StrafeSign < 0.f);
			if (Now >= NextToggle || bPast)
			{
				StrafeSign = -StrafeSign;
				NextToggle = Now + Rng.FRandRange(B.StrafePeriodMinSeconds, B.StrafePeriodMaxSeconds);
			}
			I.MoveDir = Right * StrafeSign;
			break;
		}
		case EBotBehaviour::Peek:
		{
			const FVector2D Goal = FPSNav::TileCenterLocal(*Map, bPeekOut ? Setup.PeekExposed : Setup.PeekHidden);
			if (FVector2D::Distance(Pos, Goal) > B.WaypointToleranceCm)
			{
				I.MoveDir = (Goal - Pos).GetSafeNormal();
				bPeekWaiting = false;
			}
			else if (!bPeekWaiting)
			{
				bPeekWaiting = true; // arrived: the dwell starts now
				NextToggle = Now + Rng.FRandRange(B.PeekPeriodMinSeconds, B.PeekPeriodMaxSeconds);
			}
			else if (Now >= NextToggle)
			{
				bPeekOut = !bPeekOut;
				bPeekWaiting = false;
			}
			break;
		}
		case EBotBehaviour::Patrol:
		{
			if (Now < PauseUntil || Waypoints.Num() == 0)
			{
				break;
			}
			if (PathPoints.Num() == 0 && !PlanTo(Waypoints[WaypointIndex], Pos, Now, B))
			{
				WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
				break;
			}
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			if (bArrived)
			{
				WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
				PathPoints.Reset();
				PauseUntil = Now + Rng.FRandRange(B.PatrolPauseMinSeconds, B.PatrolPauseMaxSeconds);
			}
			break;
		}
		case EBotBehaviour::Explore:
		{
			if (PathPoints.Num() == 0 && !PlanTo(PickExploreTarget(Pos, B), Pos, Now, B))
			{
				break;
			}
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			if (bArrived)
			{
				PathPoints.Reset();
			}
			break;
		}
		}
		if (IsMover() && !I.MoveDir.IsNearlyZero())
		{
			FacingYaw = YawOf(I.MoveDir);
			I.LookYawDeg = FacingYaw;
		}
		return I;
	}
}
