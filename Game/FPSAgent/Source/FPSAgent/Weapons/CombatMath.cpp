#include "Weapons/CombatMath.h"

namespace FPSCombat
{
float DamageForZone(const FRifleStats& Rifle, EHitZone Zone)
{
	switch (Zone)
	{
	case EHitZone::Head: return Rifle.DamageHead;
	case EHitZone::Body: return Rifle.DamageBody;
	case EHitZone::Legs: return Rifle.DamageLegs;
	default:             return 0.f;
	}
}

FDamageResult ApplyDamage(float Health, float Armor, float Damage, float ArmorAbsorbFraction)
{
	FDamageResult R;
	R.Health = Health;
	R.Armor = Armor;
	if (Health <= 0.f || Damage <= 0.f)
	{
		return R;
	}
	const float Absorb = FMath::Clamp(ArmorAbsorbFraction, 0.f, 1.f);
	R.DamageToArmor = FMath::Min(FMath::Max(Armor, 0.f), Damage * Absorb);
	R.DamageToHealth = Damage - R.DamageToArmor;
	R.Armor = Armor - R.DamageToArmor;
	R.Health = FMath::Max(0.f, Health - R.DamageToHealth);
	R.bKilled = R.Health <= 0.f;
	return R;
}
} // namespace FPSCombat
