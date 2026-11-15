#include "Misc/AutomationTest.h"
#include "Arena/MapBuilder.h"
#include "Arena/MapSpec.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FString Quote(const TArray<FString>& Rows)
	{
		TArray<FString> Q;
		for (const FString& R : Rows) { Q.Add(TEXT("\"") + R + TEXT("\"")); }
		return TEXT("[") + FString::Join(Q, TEXT(",")) + TEXT("]");
	}

	FString SpecJson(const TArray<FString>& Tiles, TArray<FString> Heights = {}, TArray<FString> Zones = {})
	{
		if (Heights.Num() == 0) { for (const FString& R : Tiles) { Heights.Add(FString::ChrN(R.Len(), TEXT('0'))); } }
		if (Zones.Num() == 0) { for (const FString& R : Tiles) { Zones.Add(FString::ChrN(R.Len(), TEXT('.'))); } }
		return FString::Printf(TEXT("{\"format\":1,\"name\":\"t\",\"tile_cm\":400,\"level_cm\":100,\"wall_cm\":600,\"tiles\":%s,\"heights\":%s,\"zones\":%s}"),
			*Quote(Tiles), *Quote(Heights), *Quote(Zones));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapParseTest, "FPSAgent.Map.Parse",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapParseTest::RunTest(const FString& Parameters)
{
	FMapSpec S; FString Err;
	TestTrue(TEXT("parses"), FMapSpec::Parse(SpecJson({ TEXT("####"), TEXT("#..#"), TEXT("#..#"), TEXT("####") }), S, Err));
	TestEqual(TEXT("rows"), S.Rows(), 4);
	TestEqual(TEXT("cols"), S.Cols(), 4);
	TestEqual(TEXT("north-west tile center"), S.TileCenter(0, 0), FVector(1400, 200, 0));
	TestEqual(TEXT("south-east tile center"), S.TileCenter(3, 3), FVector(200, 1400, 0));

	FMapSpec Bad;
	TestFalse(TEXT("ragged rows rejected"), FMapSpec::Parse(SpecJson({ TEXT("####"), TEXT("#..") , TEXT("####") }), Bad, Err));
	TestTrue(TEXT("error mentions dims"), Err.Contains(TEXT("dims")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapFloorTopTest, "FPSAgent.Map.FloorTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapFloorTopTest::RunTest(const FString& Parameters)
{
	FMapSpec S; FString Err;
	TestTrue(TEXT("parses"), FMapSpec::Parse(SpecJson({ TEXT("#####"), TEXT("#.^.#"), TEXT("#####") },
		{ TEXT("00000"), TEXT("00010"), TEXT("00000") }), S, Err));
	TestEqual(TEXT("ramp is half a level up"), S.FloorTopCm(1, 2), 50.f);
	TestEqual(TEXT("level 1 floor"), S.FloorTopCm(1, 3), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapBlocksTest, "FPSAgent.Map.Blocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapBlocksTest::RunTest(const FString& Parameters)
{
	FMapSpec S; FString Err;
	TestTrue(TEXT("parses"), FMapSpec::Parse(SpecJson({ TEXT("#######"), TEXT("#..h..#"), TEXT("#######") }), S, Err));
	const TArray<FMapBlock> Blocks = FPSArena::ComputeMapBlocks(S);
	double ColumnArea = 0.0;
	float CoverTop = -1.f;
	for (const FMapBlock& B : Blocks)
	{
		if (B.bColumn)
		{
			TestEqual(TEXT("column bottom at -50"), static_cast<float>(B.Center.Z - 0.5 * B.Size.Z), -50.f, 0.01f);
			ColumnArea += B.Size.X * B.Size.Y;
		}
		else
		{
			CoverTop = static_cast<float>(B.Center.Z + 0.5 * B.Size.Z);
		}
	}
	TestEqual(TEXT("columns cover all 21 tiles once"), static_cast<float>(ColumnArea), 21.f * 400.f * 400.f, 1.f);
	TestEqual(TEXT("high cover top"), CoverTop, 250.f, 0.01f);
	TestTrue(TEXT("runs are merged"), Blocks.Num() <= 8);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapRampBlockTest, "FPSAgent.Map.RampBlock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapRampBlockTest::RunTest(const FString& Parameters)
{
	FMapSpec S; FString Err;
	TestTrue(TEXT("parses"), FMapSpec::Parse(SpecJson({ TEXT("######"), TEXT("#.>..#"), TEXT("######") },
		{ TEXT("000000"), TEXT("000110"), TEXT("000000") }), S, Err));
	int32 Ramps = 0;
	for (const FMapBlock& B : FPSArena::ComputeMapBlocks(S))
	{
		if (B.bColumn || FMath::IsNearlyZero(B.Rotation.Pitch)) { continue; }
		++Ramps;
		TestEqual(TEXT("ramp slope"), static_cast<float>(FMath::Abs(B.Rotation.Pitch)), FMath::RadiansToDegrees(FMath::Atan(100.f / 400.f)), 0.1f);
		const FVector TopAtCenter = B.Center + FRotationMatrix(B.Rotation).GetUnitAxis(EAxis::Z) * (0.5 * B.Size.Z);
		TestEqual(TEXT("top surface through tile center at half a level"), static_cast<float>(TopAtCenter.Z), 50.f, 1.f);
		const FVector TileC = S.TileCenter(1, 2);
		TestTrue(TEXT("ramp sits on its tile"), FVector::Dist2D(TopAtCenter, TileC) < 1.0);
	}
	TestEqual(TEXT("one ramp slab"), Ramps, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapLoadMap01Test, "FPSAgent.Map.LoadMap01",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapLoadMap01Test::RunTest(const FString& Parameters)
{
	FMapSpec S; FString Err;
	TestTrue(*FString::Printf(TEXT("map01 loads (%s)"), *Err), FMapSpec::LoadFromContent(TEXT("map01"), S, Err));
	const FMapLayout L = FPSArena::ComputeMapLayout(S, FVector::ZeroVector, 90.f);
	TestTrue(TEXT("attacker spawns"), L.AttackerSpawns.Num() >= 4);
	TestTrue(TEXT("defender spawns"), L.DefenderSpawns.Num() >= 4);
	TestTrue(TEXT("bot spots"), L.BotSpots.Num() >= 20);
	return true;
}

#endif
