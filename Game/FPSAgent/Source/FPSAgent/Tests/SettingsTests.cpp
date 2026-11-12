#include "Misc/AutomationTest.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSettingsDefaultsTest, "FPSAgent.Settings.Defaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSettingsDefaultsTest::RunTest(const FString& Parameters)
{
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	TestTrue(TEXT("one headshot kills from full HP + armor"), S.Rifle.DamageHead >= S.Health.MaxHealth + S.Health.MaxArmor);
	TestTrue(TEXT("fire rate positive"), S.Rifle.FireRate > 0.f);
	TestTrue(TEXT("walk slower than run"), S.Movement.WalkSpeed < S.Movement.RunSpeed);
	TestTrue(TEXT("crouch capsule smaller"), S.Body.CrouchedHalfHeight < S.Body.StandingHalfHeight);
	TestTrue(TEXT("range has lanes"), S.Range.Lanes.Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRunArgsTest, "FPSAgent.Settings.RunArgs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRunArgsTest::RunTest(const FString& Parameters)
{
	const FFPSRunArgs A = FFPSRunArgs::Parse(TEXT("-nullrhi -Stage=S2 -Seed=42 -Arenas=16"));
	TestEqual(TEXT("stage"), A.Stage, FString(TEXT("S2")));
	TestEqual(TEXT("seed"), A.Seed, 42);
	TestEqual(TEXT("arenas"), A.Arenas, 16);
	const FFPSRunArgs D = FFPSRunArgs::Parse(TEXT("-nullrhi"));
	TestEqual(TEXT("default stage"), D.Stage, FString(TEXT("S0")));
	TestEqual(TEXT("default seed"), D.Seed, 0);
	TestEqual(TEXT("default arenas"), D.Arenas, 1);
	TestEqual(TEXT("arenas clamped"), FFPSRunArgs::Parse(TEXT("-Arenas=0")).Arenas, 1);
	TestEqual(TEXT("map"), FFPSRunArgs::Parse(TEXT("-nullrhi -Map=map01 -Seed=3")).Map, FString(TEXT("map01")));
	TestTrue(TEXT("map unset by default"), D.Map.IsEmpty());

	const FFPSRunArgs L = FFPSRunArgs::Parse(TEXT("-nullrhi -Stage=SpikeA -Infer -ResumeFrom=\"/w/runs/1/trainer/FPSAgent0/Snapshots\" -Iterations=200"));
	TestTrue(TEXT("spike A stage"), L.IsSpikeA());
	TestTrue(TEXT("infer flag"), L.bInfer);
	TestEqual(TEXT("resume dir (quoted)"), L.ResumeFrom, FString(TEXT("/w/runs/1/trainer/FPSAgent0/Snapshots")));
	TestEqual(TEXT("iterations"), L.Iterations, 200);
	TestTrue(TEXT("stage match is case-insensitive"), FFPSRunArgs::Parse(TEXT("-Stage=spikea")).IsSpikeA());
	TestFalse(TEXT("S0 is not spike A"), D.IsSpikeA());
	TestFalse(TEXT("infer off by default"), D.bInfer);
	TestFalse(TEXT("scripted off by default"), D.bScripted);
	const FFPSRunArgs Sc = FFPSRunArgs::Parse(TEXT("-Stage=SpikeA -Scripted"));
	TestTrue(TEXT("scripted"), Sc.bScripted);
	TestTrue(TEXT("scripted implies infer"), Sc.bInfer);
	TestTrue(TEXT("resume unset by default"), D.ResumeFrom.IsEmpty());
	TestEqual(TEXT("iterations 0 = settings"), D.Iterations, 0);
	return true;
}

#endif
