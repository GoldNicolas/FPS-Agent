#include "Misc/AutomationTest.h"
#include "Learning/ReachTaskLogic.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachSeedTest, "FPSAgent.Learning.Reach.EpisodeSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachSeedTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("deterministic"), FPSReach::EpisodeSeed(7, 3, 11), FPSReach::EpisodeSeed(7, 3, 11));
	TestNotEqual(TEXT("arena changes seed"), FPSReach::EpisodeSeed(7, 3, 11), FPSReach::EpisodeSeed(7, 4, 11));
	TestNotEqual(TEXT("episode changes seed"), FPSReach::EpisodeSeed(7, 3, 11), FPSReach::EpisodeSeed(7, 3, 12));
	TestNotEqual(TEXT("base seed changes seed"), FPSReach::EpisodeSeed(7, 3, 11), FPSReach::EpisodeSeed(8, 3, 11));
	TestNotEqual(TEXT("no arena/episode aliasing"), FPSReach::EpisodeSeed(0, 1, 0), FPSReach::EpisodeSeed(0, 0, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachLayoutTest, "FPSAgent.Learning.Reach.ArenaCenters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachLayoutTest::RunTest(const FString& Parameters)
{
	const TArray<FVector> C = FPSReach::ArenaCenters(5, 4000.f, FVector(0.f, 0.f, 100.f));
	TestEqual(TEXT("count"), C.Num(), 5);
	TestEqual(TEXT("first at origin"), C[0], FVector(0.f, 0.f, 100.f));
	double MinGap = TNumericLimits<double>::Max();
	for (int32 i = 0; i < C.Num(); ++i)
	{
		TestEqual(TEXT("same floor height"), static_cast<float>(C[i].Z), 100.f);
		for (int32 j = i + 1; j < C.Num(); ++j) { MinGap = FMath::Min(MinGap, FVector::Dist2D(C[i], C[j])); }
	}
	TestEqual(TEXT("arenas are spacing apart"), static_cast<float>(MinGap), 4000.f);
	// 5 arenas on a ceil(sqrt(5)) = 3 wide grid: rows of 3.
	TestEqual(TEXT("grid wraps after 3"), C[3], FVector(4000.f, 0.f, 100.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachSampleTest, "FPSAgent.Learning.Reach.Sampling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachSampleTest::RunTest(const FString& Parameters)
{
	FReachTaskSettings S; // arena 2000, margin 150, target 300..1200
	const FVector Center(8000.f, -4000.f, 92.f);
	const float Inner = 0.5f * S.ArenaSizeCm - S.WallMarginCm;
	int32 InRange = 0;
	for (int32 i = 0; i < 200; ++i)
	{
		FRandomStream Rng(FPSReach::EpisodeSeed(1, 0, i));
		const FVector Start = FPSReach::SampleStart(Rng, Center, S);
		const FVector Target = FPSReach::SampleTarget(Rng, Center, Start, S);
		const auto Inside = [&](const FVector& P)
		{
			return FMath::Abs(P.X - Center.X) <= Inner + 0.01 && FMath::Abs(P.Y - Center.Y) <= Inner + 0.01;
		};
		TestTrue(TEXT("start inside the walls"), Inside(Start));
		TestTrue(TEXT("target inside the walls"), Inside(Target));
		TestEqual(TEXT("target at agent height"), static_cast<float>(Target.Z), static_cast<float>(Start.Z));
		const double D = FVector::Dist2D(Start, Target);
		if (D >= S.TargetMinDistanceCm - 0.01 && D <= S.TargetMaxDistanceCm + 0.01) { ++InRange; }
		TestTrue(TEXT("never spawns on the target"), D > S.ReachRadiusCm);
	}
	TestTrue(TEXT("almost all targets in the distance band"), InRange >= 190);

	FRandomStream A(42), B(42);
	const FVector SA = FPSReach::SampleStart(A, Center, S), SB = FPSReach::SampleStart(B, Center, S);
	TestEqual(TEXT("deterministic start"), SA, SB);
	TestEqual(TEXT("deterministic target"), FPSReach::SampleTarget(A, Center, SA, S), FPSReach::SampleTarget(B, Center, SB, S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachRewardTest, "FPSAgent.Learning.Reach.Reward",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachRewardTest::RunTest(const FString& Parameters)
{
	FReachTaskSettings S; // 1 per meter, bonus 5, penalty 0.5/s, radius 100
	const float Dt = 1.f / 60.f;
	TestEqual(TEXT("closing 1 m"), FPSReach::StepReward(500.f, 400.f, false, Dt, S), 1.f - 0.5f * Dt, 1e-5f);
	TestEqual(TEXT("moving away 0.5 m"), FPSReach::StepReward(400.f, 450.f, false, Dt, S), -0.5f - 0.5f * Dt, 1e-5f);
	TestEqual(TEXT("standing still costs time"), FPSReach::StepReward(300.f, 300.f, false, Dt, S), -0.5f * Dt, 1e-6f);
	TestEqual(TEXT("reach adds the bonus"), FPSReach::StepReward(150.f, 90.f, true, Dt, S), 0.6f + 5.f - 0.5f * Dt, 1e-5f);
	TestTrue(TEXT("inside radius = reached"), FPSReach::IsReached(99.f, S));
	TestFalse(TEXT("outside radius"), FPSReach::IsReached(101.f, S));
	return true;
}

#endif
