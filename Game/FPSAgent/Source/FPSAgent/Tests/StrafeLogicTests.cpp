#include "Misc/AutomationTest.h"
#include "Bots/StrafeLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSStrafeLogicTest, "FPSAgent.Bots.Strafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSStrafeLogicTest::RunTest(const FString& Parameters)
{
	FRangeSettings Range; // half width 300, period 0.35..1.1
	const FRandomStream Rng(99);
	FStrafeState S;
	TestEqual(TEXT("starts moving right"), UpdateStrafe(S, 0.0, 0.f, Range, Rng), 1.f);
	TestTrue(TEXT("flip scheduled in range"), S.NextFlipTime >= 0.35 && S.NextFlipTime <= 1.1);
	TestEqual(TEXT("keeps direction before flip"), UpdateStrafe(S, 0.2, 50.f, Range, Rng), 1.f);
	TestEqual(TEXT("flips at schedule"), UpdateStrafe(S, S.NextFlipTime, 60.f, Range, Rng), -1.f);

	FStrafeState Edge;
	UpdateStrafe(Edge, 0.0, 0.f, Range, Rng);
	TestEqual(TEXT("bounces off the right edge"), UpdateStrafe(Edge, 0.1, 350.f, Range, Rng), -1.f);
	TestEqual(TEXT("no double flip while returning"), UpdateStrafe(Edge, 0.15, 340.f, Range, Rng), -1.f);

	const FRandomStream RngA(5), RngB(5);
	FStrafeState A, B;
	UpdateStrafe(A, 0.0, 0.f, Range, RngA);
	UpdateStrafe(B, 0.0, 0.f, Range, RngB);
	TestTrue(TEXT("deterministic schedule"), A.NextFlipTime == B.NextFlipTime);
	return true;
}

#endif
