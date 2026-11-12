#include "Misc/AutomationTest.h"
#include "Learning/Curriculum.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCurriculum;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumControllerTest, "FPSAgent.Combat.Curriculum.Controller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumControllerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C; // window 20, raise > 0.7, lower < 0.4, step 0.05
	FDifficultyController D;
	D.Init(0.f);
	for (int32 i = 0; i < 19; ++i) { TestEqual(TEXT("no change before the window fills"), D.Record(true, C), 0.f); }
	TestEqual(TEXT("20 clears raise the level"), D.Record(true, C), 0.05f, 1e-6f);
	TestEqual(TEXT("level"), D.Level, 0.05f, 1e-6f);
	TestEqual(TEXT("window resets after a change"), D.Window.Num(), 0);

	FDifficultyController Low;
	Low.Init(0.f);
	for (int32 i = 0; i < 25; ++i) { Low.Record(false, C); }
	TestEqual(TEXT("can't go below 0"), Low.Level, 0.f);
	TestEqual(TEXT("at the bound the window keeps rolling"), Low.Window.Num(), 20);

	FDifficultyController Mid;
	Mid.Init(0.5f);
	for (int32 i = 0; i < 20; ++i) { Mid.Record(false, C); }
	TestEqual(TEXT("20 failures lower the level"), Mid.Level, 0.45f, 1e-6f);

	FDifficultyController Even;
	Even.Init(0.5f);
	for (int32 i = 0; i < 30; ++i) { Even.Record(i % 2 == 0, C); }
	TestEqual(TEXT("50% clears: no change"), Even.Level, 0.5f, 1e-6f);
	TestEqual(TEXT("rolling window keeps the last 20"), Even.Window.Num(), 20);

	FDifficultyController Top;
	Top.Init(1.f);
	for (int32 i = 0; i < 20; ++i) { Top.Record(true, C); }
	TestEqual(TEXT("can't go above 1"), Top.Level, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumSamplerTest, "FPSAgent.Combat.Curriculum.SampleS0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumSamplerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C;
	const float MaxRadius = 3700.f;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 0.f, false, C, MaxRadius);
		TestEqual(TEXT("level 0: one bot"), E.BotOffsets.Num(), 1);
		TestEqual(TEXT("crouch flags match"), E.BotCrouched.Num(), E.BotOffsets.Num());
		for (const FVector2D& P : E.BotOffsets)
		{
			const double Dist = P.Size();
			TestTrue(TEXT("level 0: at least the minimum distance"), Dist >= C.S0.BotDistanceMinCm - 1.0);
			TestTrue(TEXT("level 0: at most easy max + jitter (1700)"), Dist <= 1700.0 + 1.0);
			const float Rel = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X)) - E.AgentYawDeg);
			TestTrue(TEXT("level 0: within 45 + jitter (58.5) degrees of the facing"), FMath::Abs(Rel) <= 58.5f + 0.01f);
		}
	}
	for (int32 Seed = 0; Seed < 50; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 1.f, false, C, MaxRadius);
		TestEqual(TEXT("level 1: five bots"), E.BotOffsets.Num(), 5);
		for (const FVector2D& P : E.BotOffsets) { TestTrue(TEXT("inside the radius"), P.Size() <= MaxRadius + 1.0); }
	}

	FRandomStream A(42), B(42);
	const FS0Episode EA = SampleS0(A, 0.7f, true, C, MaxRadius);
	const FS0Episode EB = SampleS0(B, 0.7f, true, C, MaxRadius);
	TestEqual(TEXT("deterministic count"), EA.BotOffsets.Num(), EB.BotOffsets.Num());
	TestEqual(TEXT("deterministic yaw"), EA.AgentYawDeg, EB.AgentYawDeg);
	for (int32 i = 0; i < EA.BotOffsets.Num(); ++i) { TestEqual(TEXT("deterministic offsets"), EA.BotOffsets[i], EB.BotOffsets[i]); }

	int32 Reviews = 0;
	FRandomStream R(7);
	for (int32 i = 0; i < 2000; ++i)
	{
		const FS0Episode E = SampleS0(R, 0.6f, true, C, MaxRadius);
		if (E.bReview)
		{
			++Reviews;
			TestTrue(TEXT("review level is easier"), E.Level <= 0.6f + 1e-6f);
		}
		else
		{
			TestEqual(TEXT("counted episodes use the controller level"), E.Level, 0.6f);
		}
	}
	TestTrue(FString::Printf(TEXT("about 20%% reviews (%d of 2000)"), Reviews), Reviews >= 330 && Reviews <= 470);
	FRandomStream NoReview(7);
	for (int32 i = 0; i < 200; ++i) { TestFalse(TEXT("no reviews when not allowed (-Eval)"), SampleS0(NoReview, 0.6f, false, C, MaxRadius).bReview); }
	return true;
}

#endif
