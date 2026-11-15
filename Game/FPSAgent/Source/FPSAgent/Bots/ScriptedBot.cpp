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

void AScriptedBot::Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed, float InSpeedScale)
{
	EnsureDiedBound();
	bActive = true;
	SetDeadState(false);   // walking mode again, so Crouch/UnCrouch work
	ApplyCrouchNow(false); // stand up where the bot is first: a crouched capsule teleported to a standing spawn height would float
	const FRotator Facing(0.0, YawDeg, 0.0);
	SetActorLocationAndRotation(Location, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = GetController())
	{
		C->SetControlRotation(Facing);
	}
	GetHealth()->ResetToFull();
	GetRifle()->ResetWeapon();
	GetRifle()->SeedRandom(Seed);
	GetCharacterMovement()->StopMovementImmediately();
	SetSpeedScale(InSpeedScale);
	ApplyCrouchNow(bCrouched); // after the teleport: the capsule is resized now and the feet stay on the floor
}

void AScriptedBot::Drive(const FPSBots::FBotIntent& Intent, float DeltaSeconds, float TurnRateDegPerSec)
{
	if (!IsAlive())
	{
		return;
	}
	if (!Intent.MoveDir.IsNearlyZero())
	{
		AddMovementInput(FVector(Intent.MoveDir.X, Intent.MoveDir.Y, 0.0), 1.f);
	}
	if (Intent.bCrouch != GetCharacterMovement()->bWantsToCrouch)
	{
		if (Intent.bCrouch) { Crouch(); } else { UnCrouch(); }
	}
	const float Current = GetActorRotation().Yaw;
	const float Delta = FRotator::NormalizeAxis(Intent.LookYawDeg - Current);
	const float MaxStep = TurnRateDegPerSec * DeltaSeconds;
	const FRotator Facing(0.0, FRotator::NormalizeAxis(Current + FMath::Clamp(Delta, -MaxStep, MaxStep)), 0.0);
	if (AController* C = GetController())
	{
		C->SetControlRotation(Facing); // the bean turns with its controller's yaw
	}
	SetActorRotation(Facing);
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
