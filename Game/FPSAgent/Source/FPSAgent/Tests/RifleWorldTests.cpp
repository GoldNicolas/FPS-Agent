#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Arena/ArenaBuilder.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Weapons/RifleComponent.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Bare game world (no map, no game mode, no renderer). Actors here never BeginPlay, so state is reset by hand. */
	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			FWorldContext& Ctx = GEngine->CreateNewWorldContext(EWorldType::Game);
			Ctx.SetCurrentWorld(World);
		}
		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		ABeanCharacter* SpawnBean(const FVector& Location, const FRotator& Rotation) const
		{
			FActorSpawnParameters P;
			P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			ABeanCharacter* Bean = World->SpawnActor<ABeanCharacter>(Location, Rotation, P);
			Bean->GetCharacterMovement()->DisableMovement(); // no floor here
			Bean->GetHealth()->ResetToFull();
			Bean->GetRifle()->ResetWeapon();
			Bean->GetRifle()->SeedRandom(7);
			return Bean;
		}
		void Step(int32 Frames = 2) const
		{
			for (int32 i = 0; i < Frames; ++i) { World->Tick(LEVELTICK_All, 1.f / 60.f); }
		}
	};

	void AimAt(ABeanCharacter* Shooter, const FVector& Target)
	{
		FVector Eye; FRotator Unused;
		Shooter->GetActorEyesViewPoint(Eye, Unused);
		Shooter->SetActorRotation((Target - Eye).Rotation());
	}
}

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
