#include "Misc/AutomationTest.h"
#include "Weapons/RifleFireState.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Holds the trigger from Start to End at 60 Hz ticks; returns all fired spray indices. */
	TArray<int32> HoldTrigger(FRifleFireState& S, const FRifleStats& R, double Start, double End)
	{
		TArray<int32> All, Frame;
		for (double T = Start; T < End - 1e-9; T += 1.0 / 60.0)
		{
			S.Update(R, T, true, Frame);
			All.Append(Frame);
		}
		return All;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSFireCadenceTest, "FPSAgent.Rifle.Cadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSFireCadenceTest::RunTest(const FString& Parameters)
{
	FRifleStats R;
	R.MagazineSize = 100;
	FRifleFireState S;
	S.Reset(R);
	const TArray<int32> Shots = HoldTrigger(S, R, 0.0, 3.0);
	TestEqual(TEXT("9.75 rps for 3 s at 60 Hz = 30 shots"), Shots.Num(), 30);
	for (int32 i = 0; i < Shots.Num(); ++i)
	{
		TestEqual(*FString::Printf(TEXT("consecutive spray index %d"), i), Shots[i], i);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSMagazineReloadTest, "FPSAgent.Rifle.MagazineReload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSMagazineReloadTest::RunTest(const FString& Parameters)
{
	const FRifleStats R; // 25 rounds, 2.5 s reload
	FRifleFireState S;
	S.Reset(R);
	TArray<int32> Frame;
	const TArray<int32> Shots = HoldTrigger(S, R, 0.0, 3.0);
	TestEqual(TEXT("empties the magazine"), Shots.Num(), 25);
	TestEqual(TEXT("ammo zero"), S.Ammo, 0);
	TestTrue(TEXT("auto reload started"), S.IsReloading());
	S.Update(R, 5.5, false, Frame);
	TestFalse(TEXT("reload finished"), S.IsReloading());
	TestEqual(TEXT("magazine refilled"), S.Ammo, 25);
	TestEqual(TEXT("spray reset after reload"), S.ShotIndex, 0);
	TestEqual(TEXT("no shot while released"), Frame.Num(), 0);

	FRifleFireState Held;
	Held.Reset(R);
	HoldTrigger(Held, R, 0.0, 3.0);           // empties, reload ends ~4.97 s
	const TArray<int32> After = HoldTrigger(Held, R, 3.0, 5.2);
	TestTrue(TEXT("no burst catch-up after reload while holding"), After.Num() <= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTapCadenceTest, "FPSAgent.Rifle.TapCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTapCadenceTest::RunTest(const FString& Parameters)
{
	const FRifleStats R;
	FRifleFireState S;
	S.Reset(R);
	TArray<int32> F;
	TestEqual(TEXT("first tap fires"), S.Update(R, 0.0, true, F), 1);
	TestEqual(TEXT("release"), S.Update(R, 0.016, false, F), 0);
	TestEqual(TEXT("re-tap too early blocked"), S.Update(R, 0.05, true, F), 0);
	TestEqual(TEXT("fires once interval passed"), S.Update(R, 0.11, true, F), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSprayRecoveryTest, "FPSAgent.Rifle.SprayRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSprayRecoveryTest::RunTest(const FString& Parameters)
{
	const FRifleStats R;
	FRifleFireState S;
	S.Reset(R);
	TArray<int32> F;
	HoldTrigger(S, R, 0.0, 0.45);             // 5 shots: indices 0..4
	S.Update(R, 0.46, false, F);
	S.Update(R, 0.52, true, F);
	TestTrue(TEXT("quick re-press continues spray"), F.Num() == 1 && F[0] == 5);
	S.Update(R, 0.53, false, F);
	S.Update(R, 2.0, true, F);
	TestTrue(TEXT("long pause resets to first-shot accuracy"), F.Num() == 1 && F[0] == 0);
	return true;
}

#endif
