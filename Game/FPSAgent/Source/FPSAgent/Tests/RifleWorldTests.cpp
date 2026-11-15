#include "Misc/AutomationTest.h"
#include "Arena/ArenaBuilder.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

using FPSTest::FTestWorld;
using FPSTest::AimAt;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRifleHeadshotTest, "FPSAgent.Rifle.World.HeadshotKills",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRifleHeadshotTest::RunTest(const FString& Parameters)
{
	FTestWorld W;
	ABeanCharacter* Shooter = W.SpawnBean(FVector(0, 0, 100), FRotator::ZeroRotator);
	ABeanCharacter* Target = W.SpawnBean(FVector(1000, 0, 100), FRotator(0, 180, 0));
	W.Step();
	AimAt(Shooter, Target->GetHitbox(EHitZone::Head)->GetComponentLocation());
	const FShotResult Shot = Shooter->GetRifle()->FireShot(0);
	TestTrue(TEXT("hit the head"), Shot.Zone == EHitZone::Head);
	TestEqual(TEXT("head damage"), Shot.Damage, 160.f);
	TestTrue(TEXT("one-tap kill"), Shot.bKill && Target->GetHealth()->IsDead());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRifleLegshotTest, "FPSAgent.Rifle.World.LegShot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRifleLegshotTest::RunTest(const FString& Parameters)
{
	FTestWorld W;
	ABeanCharacter* Shooter = W.SpawnBean(FVector(0, 0, 100), FRotator::ZeroRotator);
	ABeanCharacter* Target = W.SpawnBean(FVector(1000, 0, 100), FRotator(0, 180, 0));
	W.Step();
	AimAt(Shooter, Target->GetHitbox(EHitZone::Legs)->GetComponentLocation());
	const FShotResult Shot = Shooter->GetRifle()->FireShot(0);
	TestTrue(TEXT("hit the legs"), Shot.Zone == EHitZone::Legs);
	TestEqual(TEXT("leg damage"), Shot.Damage, 34.f);
	TestEqual(TEXT("armor took it"), Target->GetHealth()->GetArmor(), 16.f);
	TestEqual(TEXT("health intact"), Target->GetHealth()->GetHealth(), 100.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSRifleWallTest, "FPSAgent.Rifle.World.WallBlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSRifleWallTest::RunTest(const FString& Parameters)
{
	FTestWorld W;
	ABeanCharacter* Shooter = W.SpawnBean(FVector(0, 0, 100), FRotator::ZeroRotator);
	ABeanCharacter* Target = W.SpawnBean(FVector(1000, 0, 100), FRotator(0, 180, 0));
	FPSArena::SpawnBlock(W.World, FVector(500, 0, 150), FVector(50, 400, 400));
	W.Step();
	AimAt(Shooter, Target->GetHitbox(EHitZone::Head)->GetComponentLocation());
	const FShotResult Shot = Shooter->GetRifle()->FireShot(0);
	TestTrue(TEXT("wall stopped the bullet"), Shot.bHitWorld && Shot.Zone == EHitZone::None);
	TestEqual(TEXT("target untouched"), Target->GetHealth()->GetArmor(), 50.f);
	TestTrue(TEXT("impact near the wall"), FMath::Abs(Shot.End.X - 475.0) < 5.0);
	return true;
}

#endif
