#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Arena/TileGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSNav;

namespace
{
	FMapSpec MakeSpec(const TArray<FString>& Tiles, const TArray<FString>& Heights)
	{
		FMapSpec S;
		S.Name = TEXT("test");
		S.Tiles = Tiles;
		S.Heights = Heights;
		for (const FString& Row : Tiles) { S.Zones.Add(FString::ChrN(Row.Len(), TEXT('.'))); }
		return S;
	}

	/** Row 1: floor (L0), a ramp rising east (L0 -> L1), then L1 floor. Row 2: L0, L0, then L1: a 1 m drop westward between columns 3 and 2. */
	FMapSpec RampSpec(TCHAR RampTile = TEXT('>'))
	{
		FString Row1 = TEXT("#.>..#");
		Row1[2] = RampTile;
		return MakeSpec({ TEXT("######"), Row1, TEXT("#....#"), TEXT("######") },
		                { TEXT("000000"), TEXT("000110"), TEXT("000110"), TEXT("000000") });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphMovesTest, "FPSAgent.Nav.TileGraph.Moves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphMovesTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = RampSpec();
	TestTrue(TEXT("onto the ramp from its low end"), CanMove(S, FIntPoint(1, 1), FIntPoint(1, 2)));
	TestTrue(TEXT("up the ramp"), CanMove(S, FIntPoint(1, 2), FIntPoint(1, 3)));
	TestTrue(TEXT("down the ramp (ramps work both ways)"), CanMove(S, FIntPoint(1, 3), FIntPoint(1, 2)));
	TestTrue(TEXT("off the ramp's low end"), CanMove(S, FIntPoint(1, 2), FIntPoint(1, 1)));
	TestTrue(TEXT("a drop is allowed"), CanMove(S, FIntPoint(2, 3), FIntPoint(2, 2)));
	TestFalse(TEXT("drops are one-way"), CanMove(S, FIntPoint(2, 2), FIntPoint(2, 3)));
	TestFalse(TEXT("no entering a ramp from its side"), CanMove(S, FIntPoint(2, 2), FIntPoint(1, 2)));
	TestFalse(TEXT("walls are not walkable"), CanMove(S, FIntPoint(1, 1), FIntPoint(0, 1)));

	const FTileGraph G = FTileGraph::Build(S);
	TestEqual(TEXT("8 walkable tiles"), G.WalkableTiles().Num(), 8);
	TestTrue(TEXT("graph edge matches CanMove"), G.HasEdge(FIntPoint(2, 3), FIntPoint(2, 2)) && !G.HasEdge(FIntPoint(2, 2), FIntPoint(2, 3)));
	TestEqual(TEXT("ramp map is strongly connected"), G.CountStronglyConnected(), 1);
	TestEqual(TEXT("without the ramp the upper level is a one-way trap"), FTileGraph::Build(RampSpec(TEXT('.'))).CountStronglyConnected(), 2);

	FMapSpec Map01;
	FString Error;
	TestTrue(TEXT("map01 loads"), FMapSpec::LoadFromContent(TEXT("map01"), Map01, Error));
	TestEqual(TEXT("map01 is one strongly connected component (validator rule 6)"), FTileGraph::Build(Map01).CountStronglyConnected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphPathTest, "FPSAgent.Nav.TileGraph.Path",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphPathTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = RampSpec();
	const FTileGraph G = FTileGraph::Build(S);
	const TArray<FIntPoint> Up = G.FindPath(FIntPoint(2, 2), FIntPoint(1, 4));
	TestEqual(TEXT("up: around through the ramp (6 tiles)"), Up.Num(), 6);
	const TArray<FIntPoint> Down = G.FindPath(FIntPoint(1, 4), FIntPoint(2, 2));
	TestEqual(TEXT("down: via the drop (4 tiles)"), Down.Num(), 4);
	for (const TArray<FIntPoint>* P : { &Up, &Down })
	{
		for (int32 i = 1; i < P->Num(); ++i) { TestTrue(TEXT("every step is a legal move"), CanMove(S, (*P)[i - 1], (*P)[i])); }
	}
	TestTrue(TEXT("deterministic"), G.FindPath(FIntPoint(1, 4), FIntPoint(2, 2)) == Down);
	TestEqual(TEXT("start == goal"), G.FindPath(FIntPoint(1, 1), FIntPoint(1, 1)).Num(), 1);
	TestEqual(TEXT("unreachable"), FTileGraph::Build(RampSpec(TEXT('.'))).FindPath(FIntPoint(2, 2), FIntPoint(1, 4)).Num(), 0);

	FMapSpec Map01;
	FString Error;
	FMapSpec::LoadFromContent(TEXT("map01"), Map01, Error);
	const FTileGraph M = FTileGraph::Build(Map01);
	const TArray<FIntPoint> Tiles = M.WalkableTiles();
	const TArray<FIntPoint> Long = M.FindPath(Tiles[0], Tiles.Last());
	TestTrue(TEXT("map01: a path across the map"), Long.Num() > 1);
	for (int32 i = 1; i < Long.Num(); ++i) { TestTrue(TEXT("map01 path steps are legal"), CanMove(Map01, Long[i - 1], Long[i])); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphLosTest, "FPSAgent.Nav.TileGraph.Los",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphLosTest::RunTest(const FString& Parameters)
{
	FMapSpec S = FPSArena::MakeOpenFloorSpec(6, TEXT("los"));
	TestTrue(TEXT("open floor: visible"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	S.Tiles[3][3] = TEXT('h');
	TestFalse(TEXT("high cover blocks standing eyes"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	S.Tiles[3][3] = TEXT('l');
	TestTrue(TEXT("standing eyes see over low cover"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	TestFalse(TEXT("low cover hides a crouched eye (100 cm)"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5), 100.f));

	TestEqual(TEXT("TileAt inverts TileCenterLocal"), TileAt(S, TileCenterLocal(S, FIntPoint(2, 5))), FIntPoint(2, 5));
	TestEqual(TEXT("TileAt clamps to the grid"), TileAt(S, FVector2D(-5000.0, -5000.0)), FIntPoint(S.Rows() - 1, 0));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCoverFloorTest, "FPSAgent.Nav.CoverFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCoverFloorTest::RunTest(const FString& Parameters)
{
	auto Count = [](const FMapSpec& S, TCHAR Ch)
	{
		int32 N = 0;
		for (const FString& Row : S.Tiles) { for (const TCHAR C : Row) { N += C == Ch ? 1 : 0; } }
		return N;
	};
	for (int32 Seed = 0; Seed < 20; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FMapSpec S = FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, Rng, TEXT("cover"));
		TestEqual(TEXT("12% of 400 tiles are cover"), Count(S, TEXT('l')) + Count(S, TEXT('h')), 48);
		TestEqual(TEXT("half of them low"), Count(S, TEXT('l')), 24);
		TestEqual(TEXT("wall ring kept"), S.Tiles[0], FString::ChrN(22, TEXT('#')));
		TestEqual(TEXT("all walkable tiles connected"), FTileGraph::Build(S).CountStronglyConnected(), 1);
	}
	FRandomStream A(7), B(7);
	TestTrue(TEXT("deterministic per seed"), FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, A, TEXT("x")).Tiles == FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, B, TEXT("x")).Tiles);
	FRandomStream Z(1);
	TestTrue(TEXT("density 0 = the open floor"), FPSArena::MakeCoverFloorSpec(20, 0.f, 0.5f, Z, TEXT("o")).Tiles == FPSArena::MakeOpenFloorSpec(20, TEXT("o")).Tiles);
	FRandomStream Dense(3);
	const FMapSpec D = FPSArena::MakeCoverFloorSpec(20, 0.45f, 0.5f, Dense, TEXT("d"));
	TestEqual(TEXT("dense layouts stay connected"), FTileGraph::Build(D).CountStronglyConnected(), 1);
	TestTrue(TEXT("dense layouts really place cover (at least 162 of the 180 asked for)"), Count(D, TEXT('l')) + Count(D, TEXT('h')) >= 162);
	return true;
}

#endif
