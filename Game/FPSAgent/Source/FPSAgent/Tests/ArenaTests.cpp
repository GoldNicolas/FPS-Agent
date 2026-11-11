#include "Misc/AutomationTest.h"
#include "Arena/ArenaBuilder.h"
#include "Common/FPSAgentSettings.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRangeLayoutTest, "FPSAgent.Arena.RangeLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRangeLayoutTest::RunTest(const FString& Parameters)
{
	const FRangeSettings& Range = UFPSAgentSettings::Get().Range;
	const FVector Origin(100.f, 200.f, 0.f);
	const FPSArena::FRangeLayout L = FPSArena::ComputeRangeLayout(Range, Origin, 90.f);
	TestEqual(TEXT("player at origin, raised"), L.PlayerStart.GetLocation(), Origin + FVector(0, 0, 90.f));
	TestEqual(TEXT("player faces +X"), static_cast<float>(L.PlayerStart.Rotator().Yaw), 0.f, 1e-3f);
	TestEqual(TEXT("one bot per lane"), L.BotSpawns.Num(), Range.Lanes.Num());
	for (int32 i = 0; i < Range.Lanes.Num(); ++i)
	{
		const FVector Expected = Origin + FVector(Range.Lanes[i].DistanceCm, Range.Lanes[i].LateralCm, 90.f);
		TestEqual(*FString::Printf(TEXT("lane %d position"), i), L.BotSpawns[i].GetLocation(), Expected);
		TestEqual(*FString::Printf(TEXT("lane %d faces player"), i), static_cast<float>(FMath::Abs(L.BotSpawns[i].Rotator().Yaw)), 180.f, 1e-3f);
	}
	return true;
}

#endif
