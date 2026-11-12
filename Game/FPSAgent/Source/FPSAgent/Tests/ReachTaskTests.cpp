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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachEpisodeOrderTest, "FPSAgent.Learning.Reach.EpisodeOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachEpisodeOrderTest::RunTest(const FString& Parameters)
{
	// C-004: Learning Agents asks for the completion BEFORE the reward on every step (RunTraining: GatherCompletions,
	// GatherRewards, ProcessExperience); playback scores first, then checks. Both orders must end the episode on the
	// reaching step and pay the reach bonus once.
	FReachTaskSettings S; // radius 100, bonus 5, 1 per meter, 0.5 per second
	const float Dt = 1.f / 60.f;
	const TArray<float> Path = { 500.f, 300.f, 90.f, 60.f, 40.f }; // within reach from index 2 on

	FPSReach::FReachEpisode Train;
	Train.Begin(800.f);
	int32 TrainSteps = 0;
	for (const float D : Path)
	{
		const bool bEnds = FPSReach::FReachEpisode::IsTerminal(D, S); // completion first ...
		Train.Score(D, Dt, S);                                          // ... then the reward
		++TrainSteps;
		if (bEnds) { break; }
	}

	FPSReach::FReachEpisode Play;
	Play.Begin(800.f);
	int32 PlaySteps = 0;
	for (const float D : Path)
	{
		Play.Score(D, Dt, S);
		++PlaySteps;
		if (FPSReach::FReachEpisode::IsTerminal(D, S)) { break; }
	}

	const float Expected = (800.f - 90.f) / 100.f + 5.f - 3.f * 0.5f * Dt;
	TestEqual(TEXT("training order ends on the reaching step"), TrainSteps, 3);
	TestEqual(TEXT("playback order ends on the same step"), PlaySteps, 3);
	TestEqual(TEXT("training return has one bonus"), Train.Return, Expected, 1e-4f);
	TestEqual(TEXT("playback return matches training"), Play.Return, Expected, 1e-4f);
	TestTrue(TEXT("reached"), Train.bReached);
	TestEqual(TEXT("steps counted"), Train.Steps, 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSReachBonusOnceTest, "FPSAgent.Learning.Reach.BonusOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSReachBonusOnceTest::RunTest(const FString& Parameters)
{
	FReachTaskSettings S;
	const float Dt = 1.f / 60.f;
	FPSReach::FReachEpisode E;
	E.Begin(150.f);
	const float First = E.Score(90.f, Dt, S);
	const float Second = E.Score(80.f, Dt, S); // still in reach: scored again before a reset
	TestEqual(TEXT("reaching step pays progress + bonus"), First, 0.6f + 5.f - 0.5f * Dt, 1e-5f);
	TestEqual(TEXT("no second bonus while staying in reach"), Second, 0.1f - 0.5f * Dt, 1e-5f);

	E.Begin(700.f);
	TestFalse(TEXT("new episode: not reached"), E.bReached);
	TestEqual(TEXT("new episode: return cleared"), E.Return, 0.f);
	TestEqual(TEXT("new episode: steps cleared"), E.Steps, 0);
	TestEqual(TEXT("progress measured from the new start"), E.Score(600.f, Dt, S), 1.f - 0.5f * Dt, 1e-5f);
	TestFalse(TEXT("terminal is a pure distance check (outside)"), FPSReach::FReachEpisode::IsTerminal(101.f, S));
	TestTrue(TEXT("terminal is a pure distance check (inside)"), FPSReach::FReachEpisode::IsTerminal(100.f, S));
	return true;
}

#endif
