#include "Misc/AutomationTest.h"
#include "Learning/CombatObservation.h"
#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatObs;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsPackTest, "FPSAgent.Combat.Obs.Pack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsPackTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P; // position 3000, velocity 600, memory 2 s, hit 1 s
	FSelfState S;
	S.VelocityView = FVector(600.0, 0.0, 0.0);
	S.bGrounded = true;
	S.Health = 50.f; S.MaxHealth = 100.f; S.Armor = 25.f; S.MaxArmor = 50.f;
	S.Ammo = 5; S.MagazineSize = 25; S.bReloading = false;
	S.SpreadDeg = 2.5f; S.SprayShots = 3; S.SecondsSinceShot = 0.5f; S.AimPitchDeg = -45.f; S.TimeLeftFraction = 0.25f;
	const TArray<float> Self = PackSelf(S, P);
	const TArray<float> Expected = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.5f, 0.5f, 0.2f, 0.f, 0.25f, 0.12f, 0.5f, -0.5f, 0.25f };
	TestEqual(TEXT("self size"), Self.Num(), SelfSize);
	for (int32 i = 0; i < Expected.Num() && i < Self.Num(); ++i) { TestEqual(FString::Printf(TEXT("self[%d]"), i), Self[i], Expected[i], 1e-5f); }

	FEnemyState E;
	E.HeadView = FVector(3000.0, 0.0, 0.0);
	E.AimErrorDeg = FVector2D(90.0, -45.0);
	E.bVisible = true;
	E.SecondsSinceSeen = 1.f;
	const TArray<float> Enemy = PackEnemy(E, P);
	TestEqual(TEXT("enemy size"), Enemy.Num(), EnemySize);
	TestEqual(TEXT("head x"), Enemy[0], 1.f, 1e-5f);
	TestEqual(TEXT("distance"), Enemy[3], 1.f, 1e-5f);
	TestEqual(TEXT("aim yaw"), Enemy[4], 0.5f, 1e-5f);
	TestEqual(TEXT("aim pitch"), Enemy[5], -0.25f, 1e-5f);
	TestEqual(TEXT("visible"), Enemy[11], 1.f);
	TestEqual(TEXT("age / memory"), Enemy[12], 0.5f, 1e-5f);
	TestEqual(TEXT("fine aim yaw clamps"), Enemy[13], 1.f, 1e-5f);
	TestEqual(TEXT("fine aim pitch clamps"), Enemy[14], -1.f, 1e-5f);
	FEnemyState E2 = E;
	E2.AimErrorDeg = FVector2D(2.5, -1.0);
	const TArray<float> Enemy2 = PackEnemy(E2, P);
	TestEqual(TEXT("fine aim yaw"), Enemy2[13], 0.5f, 1e-5f);
	TestEqual(TEXT("fine aim pitch"), Enemy2[14], -0.2f, 1e-5f);

	const TArray<float> Hit = PackHit(FVector(0.0, 1.0, 0.0), 0.5f, P);
	TestEqual(TEXT("hit size"), Hit.Num(), HitSize);
	TestEqual(TEXT("hit dir y"), Hit[1], 1.f, 1e-5f);
	TestEqual(TEXT("hit age"), Hit[3], 0.5f, 1e-5f);

	const TArray<float> Sound = PackSound(FVector(0.0, 2.0, 0.0), 1500.f, 0.5f, P, 1.f);
	TestEqual(TEXT("sound size"), Sound.Num(), SoundSize);
	TestEqual(TEXT("sound direction (unit, view frame)"), Sound[1], 1.f, 1e-5f);
	TestEqual(TEXT("sound distance / position scale"), Sound[3], 0.5f, 1e-5f);
	TestEqual(TEXT("sound age / memory"), Sound[4], 0.5f, 1e-5f);

	const FObservationData Z = FObservationData::Zero(72);
	TestEqual(TEXT("zero self"), Z.Self.Num(), SelfSize);
	TestEqual(TEXT("zero rays"), Z.Rays.Num(), 72);
	TestEqual(TEXT("zero enemies"), Z.Enemies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsActionTest, "FPSAgent.Combat.Obs.Action",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsActionTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P; // 720 deg/s
	FAgentAction A;
	A.Move = FVector2D(3.0, 4.0);
	A.Aim = FVector2D(2.0, -3.0);
	const FAgentAction C = Sanitize(A);
	TestTrue(TEXT("move clamped to length 1"), C.Move.Equals(FVector2D(0.6, 0.8), 1e-5));
	TestTrue(TEXT("aim clamped to [-1, 1]"), C.Aim.Equals(FVector2D(1.0, -1.0), 1e-5));

	const FVector2D Step = AimPerTickDeg(FVector2D(0.5, -1.0), P, 1.f / 60.f);
	TestEqual(TEXT("half speed yaw per tick"), static_cast<float>(Step.X), 6.f, 1e-4f);
	TestEqual(TEXT("full speed pitch per tick"), static_cast<float>(Step.Y), -12.f, 1e-4f);

	TestTrue(TEXT("forward at yaw 90 = world +Y"), MoveWorld(FVector2D(1.0, 0.0), 90.f).Equals(FVector(0.0, 1.0, 0.0), 1e-4));
	TestTrue(TEXT("right at yaw 0 = world +Y"), MoveWorld(FVector2D(0.0, 1.0), 0.f).Equals(FVector(0.0, 1.0, 0.0), 1e-4));

	FAgentAction Bad;
	Bad.Move = FVector2D(std::numeric_limits<double>::quiet_NaN(), 0.5);
	Bad.Aim = FVector2D(-std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN());
	const FAgentAction Clean = Sanitize(Bad);
	TestTrue(TEXT("NaN move component -> 0"), Clean.Move.Equals(FVector2D(0.0, 0.5), 1e-9));
	TestTrue(TEXT("non-finite aim -> 0"), Clean.Aim.Equals(FVector2D::ZeroVector, 1e-9));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsScriptedTest, "FPSAgent.Combat.Obs.Scripted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsScriptedTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P;      // 720 deg/s
	const float Decision = 4.f / 60.f;       // 48 deg per decision
	FScriptedTarget Far;
	Far.bHasTarget = true; Far.AimErrorDeg = FVector2D(10.0, 0.0); Far.DistanceCm = 2000.f;
	const FAgentAction Turn = ScriptedAction(Far, 25, 25, false, 12.f, P, Decision);
	TestEqual(TEXT("turns 10 of 48 degrees"), static_cast<float>(Turn.Aim.X), 10.f / 48.f, 1e-4f);
	TestFalse(TEXT("does not fire off target"), Turn.bFire);

	FScriptedTarget On;
	On.bHasTarget = true; On.AimErrorDeg = FVector2D(0.1, 0.05); On.DistanceCm = 2000.f; // head is 0.34 deg wide at 20 m
	TestTrue(TEXT("fires on target"), ScriptedAction(On, 25, 25, false, 12.f, P, Decision).bFire);
	TestFalse(TEXT("no fire while reloading"), ScriptedAction(On, 25, 25, true, 12.f, P, Decision).bFire);
	const FAgentAction Empty = ScriptedAction(On, 0, 25, false, 12.f, P, Decision);
	TestTrue(TEXT("reloads when empty"), Empty.bReload);
	TestFalse(TEXT("no fire when empty"), Empty.bFire);

	const FAgentAction Scan = ScriptedAction(FScriptedTarget(), 25, 25, false, 12.f, P, Decision);
	TestEqual(TEXT("no target: scan right"), static_cast<float>(Scan.Aim.X), 1.f);
	TestFalse(TEXT("no target: no fire"), Scan.bFire);
	TestTrue(TEXT("no target, half empty: reload"), ScriptedAction(FScriptedTarget(), 10, 25, false, 12.f, P, Decision).bReload);
	return true;
}

#endif
