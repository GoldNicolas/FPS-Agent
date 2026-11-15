#include "Misc/AutomationTest.h"
#include "Arena/MapBuilder.h"
#include "Arena/MapSpec.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** The 14x14 toy from tools/mapgen/tests/test_validate.py (level-1 platform in A with a ramp). */
	FMapSpec ToySpec(bool bCoverInLane)
	{
		FMapSpec S;
		S.Name = TEXT("toy");
		S.Tiles = {
			TEXT("##############"), TEXT("#............#"), TEXT("#............#"), TEXT("#...######...#"),
			TEXT("#...######...#"), TEXT("#.^..........#"), TEXT("#............#"),
			bCoverInLane ? TEXT("#......h.....#") : TEXT("#............#"),
			TEXT("#............#"), TEXT("#...######...#"), TEXT("#...######...#"), TEXT("#............#"),
			TEXT("#............#"), TEXT("##############") };
		for (int32 R = 0; R < S.Tiles.Num(); ++R)
		{
			S.Heights.Add((R == 3 || R == 4) ? TEXT("01110000000000") : TEXT("00000000000000"));
			S.Zones.Add(TEXT(".............."));
		}
		return S;
	}

	float FloorHitZ(UWorld* World, const FVector& At)
	{
		FHitResult Hit;
		World->LineTraceSingleByChannel(Hit, At + FVector(0, 0, 2000), At - FVector(0, 0, 2000), ECC_Visibility);
		return Hit.bBlockingHit ? static_cast<float>(Hit.ImpactPoint.Z) : -9999.f;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapElevatedFloorTest, "FPSAgent.Map.World.ElevatedFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapElevatedFloorTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	const FMapSpec S = ToySpec(false);
	const FMapLayout L = FPSArena::BuildMap(W.World, S, FVector::ZeroVector);
	W.Step();
	TestTrue(TEXT("blocks spawned"), L.BlockCount > 0);
	TestEqual(TEXT("platform (3,2) is level 1"), FloorHitZ(W.World, S.TileCenter(3, 2)), 100.f, 2.f);
	TestEqual(TEXT("lane (7,1) is level 0"), FloorHitZ(W.World, S.TileCenter(7, 1)), 0.f, 2.f);
	TestEqual(TEXT("ramp (5,2) midpoint"), FloorHitZ(W.World, S.TileCenter(5, 2)), 50.f, 3.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMapCoverBlocksShotTest, "FPSAgent.Map.World.CoverBlocksShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMapCoverBlocksShotTest::RunTest(const FString& Parameters)
{
	for (const bool bCover : { false, true })
	{
		FPSTest::FTestWorld W;
		const FMapSpec S = ToySpec(bCover);
		FPSArena::BuildMap(W.World, S, FVector::ZeroVector);
		const float Lift = 90.f;
		ABeanCharacter* Shooter = W.SpawnBean(S.TileCenter(7, 4) + FVector(0, 0, Lift), FRotator::ZeroRotator);
		ABeanCharacter* Target = W.SpawnBean(S.TileCenter(7, 10) + FVector(0, 0, Lift), FRotator::ZeroRotator);
		W.Step();
		FPSTest::AimAt(Shooter, Target->GetHitbox(EHitZone::Body)->GetComponentLocation());
		const FShotResult Shot = Shooter->GetRifle()->FireShot(0);
		if (bCover)
		{
			TestTrue(TEXT("high cover stops a body shot"), Shot.bHitWorld && Shot.Zone == EHitZone::None);
		}
		else
		{
			TestTrue(TEXT("clear lane: body shot lands"), Shot.Zone == EHitZone::Body);
		}
	}
	return true;
}

#endif
