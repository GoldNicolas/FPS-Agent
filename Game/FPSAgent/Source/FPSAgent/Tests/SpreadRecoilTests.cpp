#include "Misc/AutomationTest.h"
#include "Weapons/CombatMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSpreadTest, "FPSAgent.Combat.Spread",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSpreadTest::RunTest(const FString& Parameters)
{
	using namespace FPSCombat;
	const FRifleStats R;
	const FMovementTuning M;
	const float Threshold = M.AccurateSpeedFraction * M.RunSpeed; // 148.5
	TestEqual(TEXT("standing first shot"), SpreadDegrees(R, M, 0.f, false, false, 0), 0.25f, 1e-4f);
	TestEqual(TEXT("at threshold still accurate"), SpreadDegrees(R, M, Threshold, false, false, 0), 0.25f, 1e-4f);
	TestEqual(TEXT("just above threshold = walk spread"), SpreadDegrees(R, M, Threshold + 0.01f, false, false, 0), 2.5f, 1e-3f);
	TestEqual(TEXT("full run"), SpreadDegrees(R, M, M.RunSpeed, false, false, 0), 5.f, 1e-4f);
	TestEqual(TEXT("airborne"), SpreadDegrees(R, M, 0.f, true, false, 0), 8.f, 1e-4f);
	TestEqual(TEXT("crouched standing"), SpreadDegrees(R, M, 0.f, false, true, 0), 0.25f * 0.85f, 1e-4f);
	TestEqual(TEXT("shot 2 no bloom"), SpreadDegrees(R, M, 0.f, false, false, 2), 0.25f, 1e-4f);
	TestEqual(TEXT("shot 3 first bloom"), SpreadDegrees(R, M, 0.f, false, false, 3), 0.25f + 0.35f, 1e-4f);
	TestEqual(TEXT("bloom capped"), SpreadDegrees(R, M, 0.f, false, false, 100), 0.25f + 3.f, 1e-4f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoilTest, "FPSAgent.Combat.Recoil",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRecoilTest::RunTest(const FString& Parameters)
{
	using namespace FPSCombat;
	const FRifleStats R;
	// FVector2D is double-precision in UE5; compare as float.
	auto Pitch = [&R](int32 i) { return static_cast<float>(RecoilOffsetDegrees(R, i).Y); };
	auto Yaw = [&R](int32 i) { return static_cast<float>(RecoilOffsetDegrees(R, i).X); };
	TestEqual(TEXT("first shot no recoil"), Pitch(0), 0.f, 1e-4f);
	TestEqual(TEXT("shot 1 kick"), Pitch(1), 0.15f, 1e-4f);
	TestEqual(TEXT("shot 2 kick"), Pitch(2), 0.30f, 1e-4f);
	TestEqual(TEXT("shot 3 climbs"), Pitch(3), 0.90f, 1e-4f);
	TestEqual(TEXT("shot 12 capped"), Pitch(12), 6.f, 1e-4f);
	TestEqual(TEXT("shot 30 capped"), Pitch(30), 6.f, 1e-4f);
	float Prev = -1.f;
	for (int32 i = 0; i <= 40; ++i)
	{
		TestTrue(*FString::Printf(TEXT("pitch non-decreasing at %d"), i), Pitch(i) >= Prev - 1e-4f);
		TestTrue(*FString::Printf(TEXT("yaw bounded at %d"), i), FMath::Abs(Yaw(i)) <= R.RecoilSwayAmplitudeDeg + 1e-4f);
		if (i <= 12) { TestEqual(*FString::Printf(TEXT("no sway before cap at %d"), i), Yaw(i), 0.f, 1e-4f); }
		Prev = Pitch(i);
	}
	TestEqual(TEXT("sway starts after cap"), Yaw(13), 1.5f * FMath::Sin(2.f * UE_PI / 6.f), 1e-3f);
	TestEqual(TEXT("same index same recoil"), Yaw(17), Yaw(17));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRecoveryTest, "FPSAgent.Combat.Recovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRecoveryTest::RunTest(const FString& Parameters)
{
	using namespace FPSCombat;
	TestEqual(TEXT("no idle keeps index"), RecoverShotIndex(8, 0.0, 0.04f), 8);
	TestEqual(TEXT("small idle keeps index"), RecoverShotIndex(8, 0.03, 0.04f), 8);
	TestEqual(TEXT("idle recovers steps"), RecoverShotIndex(8, 0.13, 0.04f), 5);
	TestEqual(TEXT("long idle resets"), RecoverShotIndex(8, 10.0, 0.04f), 0);
	TestEqual(TEXT("huge idle (first shot) no overflow"), RecoverShotIndex(0, 1.0e9, 0.04f), 0);
	TestEqual(TEXT("huge idle mid-spray no overflow"), RecoverShotIndex(12, 1.0e9, 0.04f), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSpreadConeTest, "FPSAgent.Combat.SpreadCone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSpreadConeTest::RunTest(const FString& Parameters)
{
	using namespace FPSCombat;
	const FRandomStream RngA(1234);
	const FRandomStream RngB(1234);
	const FVector Forward(1.f, 0.f, 0.f);
	for (int32 i = 0; i < 200; ++i)
	{
		const FVector A = ApplySpreadCone(Forward, 2.f, RngA);
		const FVector B = ApplySpreadCone(Forward, 2.f, RngB);
		const float AngleDeg = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(A, Forward)));
		TestTrue(TEXT("inside cone"), AngleDeg <= 2.f + 1e-3f);
		TestTrue(TEXT("deterministic for same seed"), A.Equals(B, 1e-6f));
	}
	TestTrue(TEXT("zero spread exact"), ApplySpreadCone(Forward, 0.f, RngA).Equals(Forward, 1e-6f));
	return true;
}

#endif
