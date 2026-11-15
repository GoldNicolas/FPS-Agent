#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Pure gunplay math: no actors, no world. Covered by FPSAgent.Combat.* tests. */
namespace FPSCombat
{
	struct FDamageResult
	{
		float Health = 0.f;
		float Armor = 0.f;
		float DamageToHealth = 0.f;
		float DamageToArmor = 0.f;
		bool bKilled = false;
	};

	FPSAGENT_API float DamageForZone(const FRifleStats& Rifle, EHitZone Zone);

	/** Armor takes Damage*ArmorAbsorbFraction (up to what is left); the rest hits health. No-op on a dead target. */
	FPSAGENT_API FDamageResult ApplyDamage(float Health, float Armor, float Damage, float ArmorAbsorbFraction);

	/** Cone half-angle (deg) for the shot with the given spray index. */
	FPSAGENT_API float SpreadDegrees(const FRifleStats& Rifle, const FMovementTuning& Move, float HorizontalSpeed, bool bAirborne, bool bCrouched, int32 ShotIndex);

	/** Deterministic recoil for a spray index. X = yaw (deg, + right), Y = pitch (deg, + up). */
	FPSAGENT_API FVector2D RecoilOffsetDegrees(const FRifleStats& Rifle, int32 ShotIndex);

	/** Spray index after IdleSeconds without shooting (beyond the normal fire interval). */
	FPSAGENT_API int32 RecoverShotIndex(int32 ShotIndex, double IdleSeconds, float RecoveryTimePerShot);

	/** Random direction inside a cone of SpreadDegrees around Direction (unit vector). */
	FPSAGENT_API FVector ApplySpreadCone(const FVector& Direction, float SpreadDegrees, const FRandomStream& Rng);
}
