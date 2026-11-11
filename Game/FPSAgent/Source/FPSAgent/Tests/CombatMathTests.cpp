#include "Misc/AutomationTest.h"
#include "Common/FPSAgentSettings.h"
#include "Weapons/CombatMath.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSDamageForZoneTest, "FPSAgent.Combat.DamageForZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSDamageForZoneTest::RunTest(const FString& Parameters)
{
	const FRifleStats R;
	TestEqual(TEXT("head"), FPSCombat::DamageForZone(R, EHitZone::Head), 160.f);
	TestEqual(TEXT("body"), FPSCombat::DamageForZone(R, EHitZone::Body), 40.f);
	TestEqual(TEXT("legs"), FPSCombat::DamageForZone(R, EHitZone::Legs), 34.f);
	TestEqual(TEXT("none"), FPSCombat::DamageForZone(R, EHitZone::None), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSApplyDamageTest, "FPSAgent.Combat.ApplyDamage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSApplyDamageTest::RunTest(const FString& Parameters)
{
	using namespace FPSCombat;
	FDamageResult H = ApplyDamage(100.f, 50.f, 160.f, 1.f);
	TestEqual(TEXT("headshot strips armor"), H.Armor, 0.f);
	TestEqual(TEXT("headshot kills"), H.Health, 0.f);
	TestTrue(TEXT("headshot bKilled"), H.bKilled);

	FDamageResult B = ApplyDamage(100.f, 50.f, 40.f, 1.f);
	TestEqual(TEXT("body hits armor first"), B.Armor, 10.f);
	TestEqual(TEXT("health untouched"), B.Health, 100.f);
	TestFalse(TEXT("alive"), B.bKilled);

	FDamageResult NoArmor = ApplyDamage(100.f, 0.f, 40.f, 1.f);
	TestEqual(TEXT("no armor -> health"), NoArmor.Health, 60.f);

	FDamageResult Partial = ApplyDamage(100.f, 50.f, 40.f, 0.66f);
	TestEqual(TEXT("partial absorb armor"), Partial.Armor, 50.f - 26.4f, 0.001f);
	TestEqual(TEXT("partial absorb health"), Partial.Health, 100.f - 13.6f, 0.001f);

	FDamageResult Dead = ApplyDamage(0.f, 0.f, 40.f, 1.f);
	TestFalse(TEXT("dead target not killed again"), Dead.bKilled);
	TestEqual(TEXT("dead target no damage"), Dead.DamageToHealth, 0.f);
	return true;
}

#endif
