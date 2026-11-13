#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Bots/BotBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSBots;

namespace
{
	/** Moves a point along the brain's intent at a fixed speed (no collision: a path must never need it). */
	struct FSim
	{
		FVector2D Pos;
		double Now = 0.0;
		TArray<FIntPoint> Tiles;
		TArray<FVector2D> Positions;
		TArray<FBotIntent> Intents;
		void Run(FBotBrain& Brain, const FMapSpec& Map, float Seconds, float Speed, const FBotBehaviourSettings& B)
		{
			const float Dt = 1.f / 60.f;
			for (int32 i = 0; i < FMath::RoundToInt(Seconds / Dt); ++i)
			{
				Now += Dt;
				const FBotIntent I = Brain.Update(Pos, Now, B);
				Pos += I.MoveDir * Speed * Dt;
				Positions.Add(Pos);
				Tiles.Add(FPSNav::TileAt(Map, Pos));
				Intents.Add(I);
			}
		}
	};

	FBotSetup Setup(EBotBehaviour Behaviour, float Yaw = 0.f)
	{
		FBotSetup S;
		S.Behaviour = Behaviour;
		S.FacingYawDeg = Yaw;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainStationaryTest, "FPSAgent.Bots.Brain.Stationary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainStationaryTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	const FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("brain"));
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(6, 6));

	FBotBrain Static;
	FBotSetup S = Setup(EBotBehaviour::Static);
	S.bCrouched = true;
	Static.Start(S, Map, G, 1, Start, 0.0, B);
	FSim A{ Start };
	A.Run(Static, Map, 1.f, 300.f, B);
	TestTrue(TEXT("static: never moves"), A.Pos.Equals(Start, 1e-3));
	TestTrue(TEXT("static: keeps its crouch"), A.Intents.Last().bCrouch);

	FBotBrain Crouch;
	Crouch.Start(Setup(EBotBehaviour::Crouch), Map, G, 2, Start, 0.0, B);
	FSim C{ Start };
	C.Run(Crouch, Map, 3.f, 300.f, B);
	int32 Toggles = 0;
	for (int32 i = 1; i < C.Intents.Num(); ++i) { Toggles += C.Intents[i].bCrouch != C.Intents[i - 1].bCrouch ? 1 : 0; }
	TestTrue(TEXT("crouch: toggles at least twice in 3 s"), Toggles >= 2);
	TestTrue(TEXT("crouch: never moves"), C.Pos.Equals(Start, 1e-3));

	FBotBrain Strafe;
	Strafe.Start(Setup(EBotBehaviour::Strafe, 0.f), Map, G, 3, Start, 0.0, B);
	FSim R{ Start };
	R.Run(Strafe, Map, 4.f, 300.f, B);
	bool bLeft = false, bRight = false, bAlongFacing = false;
	double MaxLateral = 0.0;
	for (const FBotIntent& I : R.Intents)
	{
		bRight |= I.MoveDir.Y > 0.5;
		bLeft |= I.MoveDir.Y < -0.5;
		bAlongFacing |= FMath::Abs(I.MoveDir.X) > 1e-3;
	}
	for (const FVector2D& P : R.Positions) { MaxLateral = FMath::Max(MaxLateral, FMath::Abs(P.Y - Start.Y)); }
	TestTrue(TEXT("strafe: both ways"), bLeft && bRight);
	TestFalse(TEXT("strafe: only sideways to its facing"), bAlongFacing);
	TestTrue(TEXT("strafe: stays near its spot (half-width + one tick)"), MaxLateral <= B.StrafeHalfWidthCm + 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainPeekTest, "FPSAgent.Bots.Brain.Peek",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainPeekTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("peek"));
	Map.Tiles[5][5] = TEXT('h');
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	FBotSetup S = Setup(EBotBehaviour::Peek);
	S.PeekHidden = FIntPoint(4, 5);   // north of the cover
	S.PeekExposed = FIntPoint(4, 6);  // one tile east of it
	const FVector2D Hidden = FPSNav::TileCenterLocal(Map, S.PeekHidden);
	const FVector2D Exposed = FPSNav::TileCenterLocal(Map, S.PeekExposed);
	FBotBrain Peek;
	Peek.Start(S, Map, G, 4, Hidden, 0.0, B);
	FSim P{ Hidden };
	double OutAt = -1.0, BackAt = -1.0;
	for (int32 Step = 0; Step < 60 * 10 && BackAt < 0.0; ++Step)
	{
		P.Run(Peek, Map, 1.f / 60.f, 300.f, B);
		if (OutAt < 0.0 && FVector2D::Distance(P.Pos, Exposed) <= B.WaypointToleranceCm) { OutAt = P.Now; }
		if (OutAt >= 0.0 && FVector2D::Distance(P.Pos, Hidden) <= B.WaypointToleranceCm) { BackAt = P.Now; }
	}
	TestTrue(TEXT("peek: steps out"), OutAt >= 0.0);
	TestTrue(TEXT("peek: and back behind cover"), BackAt > OutAt);
	for (const FIntPoint& T : P.Tiles) { TestTrue(TEXT("peek: never inside cover"), G.IsWalkable(T)); }

	FBotBrain NoCover;
	FBotSetup N = Setup(EBotBehaviour::Peek);
	NoCover.Start(N, Map, G, 5, Hidden, 0.0, B);
	TestEqual(TEXT("peek without a cover pair strafes (D-031)"), static_cast<int32>(NoCover.GetBehaviour()), static_cast<int32>(EBotBehaviour::Strafe));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainMoversTest, "FPSAgent.Bots.Brain.Movers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainMoversTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	const FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("movers"));
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(6, 6));

	FBotBrain Patrol;
	Patrol.Start(Setup(EBotBehaviour::Patrol), Map, G, 6, Start, 0.0, B);
	const TArray<FIntPoint> Way = Patrol.GetWaypoints();
	TestEqual(TEXT("patrol: waypoints"), Way.Num(), B.PatrolWaypoints);
	FSim P{ Start };
	P.Run(Patrol, Map, 90.f, 400.f, B);
	int32 Next = 0;
	for (const FIntPoint& T : P.Tiles) { if (Next < Way.Num() && T == Way[Next]) { ++Next; } }
	TestEqual(TEXT("patrol: visits its waypoints in order"), Next, Way.Num());

	FBotBrain Explore;
	Explore.Start(Setup(EBotBehaviour::Explore), Map, G, 7, Start, 0.0, B);
	FSim E{ Start };
	E.Run(Explore, Map, 60.f, 400.f, B);
	TSet<FIntPoint> Distinct(E.Tiles);
	TestTrue(TEXT("explore: covers ground (>= 15 tiles in 60 s)"), Distinct.Num() >= 15);
	FBotBrain Explore2;
	Explore2.Start(Setup(EBotBehaviour::Explore), Map, G, 7, Start, 0.0, B);
	FSim E2{ Start };
	E2.Run(Explore2, Map, 60.f, 400.f, B);
	TestTrue(TEXT("explore: deterministic per seed"), E.Pos.Equals(E2.Pos, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainInvestigateTest, "FPSAgent.Bots.Brain.Investigate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainInvestigateTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	// A high-cover wall across row 6 with one gap at column 10: the path must go around through the gap.
	FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("wall"));
	for (int32 C = 1; C <= 9; ++C) { Map.Tiles[6][C] = TEXT('h'); }
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(8, 2));
	const FVector2D Target = FPSNav::TileCenterLocal(Map, FIntPoint(3, 2));

	FBotBrain Patrol;
	Patrol.Start(Setup(EBotBehaviour::Patrol), Map, G, 8, Start, 0.0, B);
	Patrol.Investigate(Target, Start, 0.0, B);
	TestTrue(TEXT("movers investigate"), Patrol.IsInvestigating());
	FSim P{ Start };
	double ArrivedAt = -1.0;
	for (int32 Step = 0; Step < 60 * 30 && ArrivedAt < 0.0; ++Step)
	{
		P.Run(Patrol, Map, 1.f / 60.f, 400.f, B);
		if (FVector2D::Distance(P.Pos, Target) <= 100.0) { ArrivedAt = P.Now; }
	}
	TestTrue(TEXT("reaches the target around the wall"), ArrivedAt >= 0.0);
	for (const FIntPoint& T : P.Tiles) { TestTrue(TEXT("never walks through cover"), G.IsWalkable(T)); }
	P.Run(Patrol, Map, B.InvestigateLookSeconds + 1.f, 400.f, B);
	TestFalse(TEXT("looks around, then resumes its patrol"), Patrol.IsInvestigating());

	FBotBrain Strafe;
	Strafe.Start(Setup(EBotBehaviour::Strafe, 0.f), Map, G, 9, Start, 0.0, B);
	Strafe.Investigate(Start + FVector2D(0.0, 1000.0), Start, 0.0, B);
	TestFalse(TEXT("non-movers hold their spot (D-031)"), Strafe.IsInvestigating());
	FSim S{ Start };
	S.Run(Strafe, Map, 1.f / 60.f, 300.f, B);
	TestEqual(TEXT("...but turn to face the stimulus (+Y = yaw 90)"), S.Intents.Last().LookYawDeg, 90.f, 0.01f);
	return true;
}

#endif
