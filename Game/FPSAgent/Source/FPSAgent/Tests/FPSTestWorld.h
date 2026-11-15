#pragma once

#if WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Weapons/RifleComponent.h"

namespace FPSTest
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
			Bean->GetCharacterMovement()->DisableMovement(); // no gravity-driven movement in tests
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

	inline void AimAt(ABeanCharacter* Shooter, const FVector& Target)
	{
		FVector Eye; FRotator Unused;
		Shooter->GetActorEyesViewPoint(Eye, Unused);
		Shooter->SetActorRotation((Target - Eye).Rotation());
	}
}

#endif
