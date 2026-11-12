#include "Bots/ScriptedBot.h"
#include "Camera/CameraComponent.h"
#include "Character/BeanAIController.h"
#include "Character/HealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/RifleComponent.h"

AScriptedBot::AScriptedBot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AutoPossessAI = EAutoPossessAI::Spawned;
	AIControllerClass = ABeanAIController::StaticClass();
	Camera->SetAutoActivate(false);
}

void AScriptedBot::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	EnsureDiedBound();
}

void AScriptedBot::EnsureDiedBound()
{
	// Not BeginPlay: the automation test world never BeginPlays, and (as it never initializes actors) never calls
	// PostInitializeComponents either, so Activate()/Deactivate() bind lazily too.
	if (!bDiedBound)
	{
		bDiedBound = true;
		GetHealth()->OnDied.AddUObject(this, &AScriptedBot::HandleDied);
	}
}

void AScriptedBot::Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed)
{
	EnsureDiedBound();
	bActive = true;
	const FRotator Facing(0.0, YawDeg, 0.0);
	SetActorLocationAndRotation(Location, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = GetController())
	{
		C->SetControlRotation(Facing);
	}
	GetHealth()->ResetToFull();
	GetRifle()->ResetWeapon();
	GetRifle()->SeedRandom(Seed);
	SetDeadState(false);
	GetCharacterMovement()->StopMovementImmediately();
	if (bCrouched) { Crouch(); } else { UnCrouch(); }
}

void AScriptedBot::Deactivate()
{
	EnsureDiedBound();
	bActive = false;
	SetDeadState(true);
}

bool AScriptedBot::IsAlive() const
{
	return bActive && !GetHealth()->IsDead();
}

void AScriptedBot::HandleDied(UHealthComponent* Victim, AActor* Killer)
{
	SetDeadState(true); // stays dead until the next Activate (spec §4)
}
