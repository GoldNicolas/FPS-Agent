#include "Misc/AutomationTest.h"
#include "Learning/CombatObservation.h"
#include "Learning/CombatStats.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatAbilitiesTest, "FPSAgent.Combat.Obs.Abilities",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatAbilitiesTest::RunTest(const FString& Parameters)
{
	FPSCombatObs::FAgentAction A;
	A.Move = FVector2D(1.0, 0.0);
	A.Aim = FVector2D(0.5, -0.25);
	A.bFire = A.bReload = A.bCrouch = A.bWalk = A.bJump = true;

	const FCombatAbilitySettings Full;
	const FPSCombatObs::FAgentAction F = FPSCombatObs::ApplyAbilities(A, Full);
	TestTrue(TEXT("full line keeps crouch"), F.bCrouch);
	TestTrue(TEXT("full line keeps jump"), F.bJump);

	FCombatAbilitySettings Walk;
	Walk.bCrouch = false;
	Walk.bJump = false;
	const FPSCombatObs::FAgentAction W = FPSCombatObs::ApplyAbilities(A, Walk);
	TestFalse(TEXT("walk line drops crouch"), W.bCrouch);
	TestFalse(TEXT("walk line drops jump"), W.bJump);
	TestTrue(TEXT("walk line keeps walk, fire and reload"), W.bWalk && W.bFire && W.bReload);
	TestEqual(TEXT("walk line keeps move"), static_cast<float>(W.Move.X), 1.f);
	TestEqual(TEXT("walk line keeps aim"), static_cast<float>(W.Aim.Y), -0.25f);

	TestEqual(TEXT("tag static"), FString(MobilityTag(false, Full)), FString(TEXT("static")));
	TestEqual(TEXT("tag full"), FString(MobilityTag(true, Full)), FString(TEXT("full")));
	TestEqual(TEXT("tag walk"), FString(MobilityTag(true, Walk)), FString(TEXT("walk")));
	FCombatAbilitySettings CrouchOnly;
	CrouchOnly.bJump = false;
	TestEqual(TEXT("tag mixed"), FString(MobilityTag(true, CrouchOnly)), FString(TEXT("walk+crouch")));

	const FString Json = FPSCombatStats::EvalJson(FPSCombatStats::FWindow(), TEXT("S1"), 1.f, 1000, TEXT("/snap"), 0.7f, TEXT("walk"));
	TestTrue(TEXT("eval JSON records the mobility line"), Json.Contains(TEXT("\"mobility\"")) && Json.Contains(TEXT("\"walk\"")));
	return true;
}

#endif
