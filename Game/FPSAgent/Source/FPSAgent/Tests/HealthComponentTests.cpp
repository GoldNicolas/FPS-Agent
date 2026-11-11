#include "Misc/AutomationTest.h"
#include "Character/HealthComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSHealthComponentTest, "FPSAgent.Health.Component",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSHealthComponentTest::RunTest(const FString& Parameters)
{
	UHealthComponent* H = NewObject<UHealthComponent>();
	H->ResetToFull();
	TestEqual(TEXT("full health"), H->GetHealth(), 100.f);
	TestEqual(TEXT("full armor"), H->GetArmor(), 50.f);

	int32 Damaged = 0, Died = 0;
	H->OnDamaged.AddLambda([&Damaged](UHealthComponent*, const FPSCombat::FDamageResult&, AActor*) { ++Damaged; });
	H->OnDied.AddLambda([&Died](UHealthComponent*, AActor*) { ++Died; });

	H->ApplyHit(40.f, nullptr);
	TestEqual(TEXT("armor absorbed body shot"), H->GetArmor(), 10.f);
	TestEqual(TEXT("health intact"), H->GetHealth(), 100.f);
	TestEqual(TEXT("damaged event"), Damaged, 1);

	const FPSCombat::FDamageResult Kill = H->ApplyHit(160.f, nullptr);
	TestTrue(TEXT("killed"), Kill.bKilled && H->IsDead());
	TestEqual(TEXT("died event once"), Died, 1);

	H->ApplyHit(40.f, nullptr);
	TestEqual(TEXT("no events on corpse"), Damaged, 2);
	TestEqual(TEXT("still one death"), Died, 1);

	H->ResetToFull();
	TestFalse(TEXT("revived"), H->IsDead());
	TestEqual(TEXT("armor restored"), H->GetArmor(), 50.f);
	return true;
}

#endif
