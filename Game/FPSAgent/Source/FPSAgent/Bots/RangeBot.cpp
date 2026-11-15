#include "Bots/RangeBot.h"
#include "AIController.h"
#include "Camera/CameraComponent.h"
#include "Character/HealthComponent.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "Weapons/RifleComponent.h"

ARangeBot::ARangeBot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	AIControllerClass = AAIController::StaticClass();
	bUseControllerRotationYaw = false; // keep facing down the lane
	Camera->SetAutoActivate(false);
}

void ARangeBot::BeginPlay()
{
	Super::BeginPlay();
	Home = GetActorTransform();
	GetHealth()->OnDied.AddUObject(this, &ARangeBot::HandleDied);
}

void ARangeBot::InitBot(ERangeBotMode InMode, int32 Seed)
{
	Mode = InMode;
	Rng.Initialize(Seed);
	GetRifle()->SeedRandom(Seed);
}

void ARangeBot::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Mode != ERangeBotMode::Strafe || GetHealth()->IsDead())
	{
		return;
	}
	const FVector Right = Home.GetRotation().GetRightVector();
	const float Offset = FVector::DotProduct(GetActorLocation() - Home.GetLocation(), Right);
	const float Input = UpdateStrafe(Strafe, GetWorld()->GetTimeSeconds(), Offset, UFPSAgentSettings::Get().Range, Rng);
	AddMovementInput(Right, Input);
}

void ARangeBot::HandleDied(UHealthComponent* Victim, AActor* Killer)
{
	SetDeadState(true);
	GetWorldTimerManager().SetTimer(RespawnTimer, this, &ARangeBot::Respawn, UFPSAgentSettings::Get().Range.BotRespawnDelay, false);
}

void ARangeBot::Respawn()
{
	SetActorTransform(Home, false, nullptr, ETeleportType::TeleportPhysics);
	GetHealth()->ResetToFull();
	Strafe = FStrafeState();
	SetDeadState(false);
}
