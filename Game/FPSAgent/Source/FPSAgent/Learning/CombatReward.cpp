#include "Learning/CombatReward.h"

namespace FPSCombatReward
{
	const TCHAR* TermName(ETerm Term)
	{
		switch (Term)
		{
		case ETerm::DamageDealt:   return TEXT("DamageDealt");
		case ETerm::Kill:          return TEXT("Kill");
		case ETerm::FirstSighting: return TEXT("FirstSighting");
		case ETerm::Clear:         return TEXT("Clear");
		case ETerm::AimHelper:     return TEXT("AimHelper");
		case ETerm::DamageTaken:   return TEXT("DamageTaken");
		case ETerm::Death:         return TEXT("Death");
		case ETerm::Miss:          return TEXT("Miss");
		case ETerm::Time:          return TEXT("Time");
		default:                   return TEXT("Unknown");
		}
	}

	float DamageDealt(float AppliedDamage, const FCombatRewardSettings& S)
	{
		return S.DamageDealtWeight * AppliedDamage / FMath::Max(1.f, S.DamageNormalizer);
	}

	float DamageTaken(float AppliedDamage, const FCombatRewardSettings& S)
	{
		return -S.DamageTakenWeight * AppliedDamage / FMath::Max(1.f, S.DamageNormalizer);
	}

	float ClearBonus(float TimeLeftFraction, const FCombatRewardSettings& S)
	{
		return S.ClearBase + S.ClearSpeed * FMath::Clamp(TimeLeftFraction, 0.f, 1.f);
	}

	float AimPotential(float AimErrorDeg, float Weight, const FCombatRewardSettings& S)
	{
		if (AimErrorDeg < 0.f)
		{
			return 0.f;
		}
		return Weight * (1.f - FMath::Min(AimErrorDeg / FMath::Max(1e-3f, S.AimHelperRangeDeg), 1.f));
	}

	void FLedger::BeginEpisode(float InitialPotential)
	{
		FMemory::Memzero(Pending, sizeof(Pending));
		FMemory::Memzero(Episode, sizeof(Episode));
		LastPotential = InitialPotential;
		bClearPaid = false;
		Sighted.Reset();
	}

	bool FLedger::TrySighting(int32 BotIndex, const FCombatRewardSettings& S)
	{
		if (Sighted.Contains(BotIndex))
		{
			return false;
		}
		Sighted.Add(BotIndex);
		Add(ETerm::FirstSighting, S.FirstSighting);
		return true;
	}

	bool FLedger::TryClear(float TimeLeftFraction, const FCombatRewardSettings& S)
	{
		if (bClearPaid)
		{
			return false;
		}
		bClearPaid = true;
		Add(ETerm::Clear, ClearBonus(TimeLeftFraction, S));
		return true;
	}

	void FLedger::ApplyPotential(float NewPotential, float Discount)
	{
		Add(ETerm::AimHelper, Discount * NewPotential - LastPotential);
		LastPotential = NewPotential;
	}

	float FLedger::TakeDecision()
	{
		float Sum = 0.f;
		for (int32 i = 0; i < TermCount; ++i)
		{
			Sum += Pending[i];
			Episode[i] += Pending[i];
			Pending[i] = 0.f;
		}
		return Sum;
	}

	float FLedger::EpisodeReturn() const
	{
		float Sum = 0.f;
		for (int32 i = 0; i < TermCount; ++i)
		{
			Sum += Episode[i];
		}
		return Sum;
	}
}
