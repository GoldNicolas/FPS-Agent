#include "Weapons/RifleComponent.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "Weapons/CombatMath.h"

URifleComponent::URifleComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics; // fire after movement so the eye point is current
	Rng.Initialize(0);
}

void URifleComponent::BeginPlay()
{
	Super::BeginPlay();
	ResetWeapon();
}

void URifleComponent::ResetWeapon()
{
	State.Reset(UFPSAgentSettings::Get().Rifle);
	bTriggerHeld = false;
}

void URifleComponent::RequestReload()
{
	if (const UWorld* World = GetWorld())
	{
		State.StartReload(UFPSAgentSettings::Get().Rifle, World->GetTimeSeconds());
	}
}

void URifleComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	State.Update(UFPSAgentSettings::Get().Rifle, GetWorld()->GetTimeSeconds(), bTriggerHeld, PendingShots);
	for (const int32 Index : PendingShots)
	{
		FireShot(Index);
	}
}

float URifleComponent::SpreadFor(int32 ShotIndex) const
{
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	const ABeanCharacter* Bean = Cast<ABeanCharacter>(GetOwner());
	const float Speed = Bean ? Bean->GetHorizontalSpeed() : 0.f;
	const bool bAirborne = Bean && Bean->IsAirborne();
	const bool bCrouched = Bean && Bean->bIsCrouched;
	return FPSCombat::SpreadDegrees(S.Rifle, S.Movement, Speed, bAirborne, bCrouched, ShotIndex);
}

FShotResult URifleComponent::FireShot(int32 ShotIndex)
{
	FShotResult Result;
	Result.ShotIndex = ShotIndex;
	AActor* OwnerActor = GetOwner();
	UWorld* World = GetWorld();
	if (!OwnerActor || !World)
	{
		return Result;
	}

	const FRifleStats& Stats = UFPSAgentSettings::Get().Rifle;
	FVector EyeLocation;
	FRotator EyeRotation;
	OwnerActor->GetActorEyesViewPoint(EyeLocation, EyeRotation);
	const FVector2D Recoil = FPSCombat::RecoilOffsetDegrees(Stats, ShotIndex);
	EyeRotation.Pitch += Recoil.Y;
	EyeRotation.Yaw += Recoil.X;
	Result.SpreadDegrees = SpreadFor(ShotIndex);
	const FVector Direction = FPSCombat::ApplySpreadCone(EyeRotation.Vector(), Result.SpreadDegrees, Rng);
	Result.Start = EyeLocation;
	Result.End = EyeLocation + Direction * Stats.MaxRangeCm;

	FCollisionQueryParams Params(SCENE_QUERY_STAT(RifleShot), /*bTraceComplex=*/false, OwnerActor);
	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Result.Start, Result.End, ECC_Weapon, Params))
	{
		Result.End = Hit.ImpactPoint;
		Result.HitActor = Hit.GetActor();
		ABeanCharacter* Victim = Cast<ABeanCharacter>(Hit.GetActor());
		Result.Zone = Victim ? Victim->GetHitZone(Hit.GetComponent()) : EHitZone::None;
		if (Result.Zone != EHitZone::None)
		{
			Result.Damage = FPSCombat::DamageForZone(Stats, Result.Zone);
			Result.bKill = Victim->GetHealth()->ApplyHit(Result.Damage, OwnerActor).bKilled;
		}
		else
		{
			Result.bHitWorld = true;
		}
	}
	OnShotFired.Broadcast(Result);
	return Result;
}
