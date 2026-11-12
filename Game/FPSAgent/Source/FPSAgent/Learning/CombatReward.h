#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Combat reward terms and per-episode bookkeeping (spec §3). Pure: no world, no Learning Agents. */
namespace FPSCombatReward
{
	enum class ETerm : uint8 { DamageDealt, Kill, FirstSighting, Clear, AimHelper, DamageTaken, Death, Miss, Time, Count };
	inline constexpr int32 TermCount = static_cast<int32>(ETerm::Count);
	FPSAGENT_API const TCHAR* TermName(ETerm Term);

	/** Health + armor actually removed from a bot, scaled. */
	FPSAGENT_API float DamageDealt(float AppliedDamage, const FCombatRewardSettings& S);
	/** Health + armor the agent lost (negative). */
	FPSAGENT_API float DamageTaken(float AppliedDamage, const FCombatRewardSettings& S);
	/** ClearBase + ClearSpeed * clamp(time left fraction, 0, 1). */
	FPSAGENT_API float ClearBonus(float TimeLeftFraction, const FCombatRewardSettings& S);
	/** Aim-helper potential W * (1 - min(err / range, 1)); 0 when no enemy is visible (AimErrorDeg < 0). Never negative. */
	FPSAGENT_API float AimPotential(float AimErrorDeg, float Weight, const FCombatRewardSettings& S);

	/** One agent's episode: per-term sums for the current decision (Pending) and the episode; one-off terms paid once. */
	struct FPSAGENT_API FLedger
	{
		float Pending[TermCount] = {};
		float Episode[TermCount] = {};
		/** Aim-helper potential of the state after the last decision. */
		float LastPotential = 0.f;
		bool bClearPaid = false;
		TSet<int32> Sighted;

		/** New episode; InitialPotential = the aim-helper potential of the spawn state. */
		void BeginEpisode(float InitialPotential);
		void Add(ETerm Term, float Value) { Pending[static_cast<int32>(Term)] += Value; }
		/** First time this bot is seen this episode: pays FirstSighting and returns true. */
		bool TrySighting(int32 BotIndex, const FCombatRewardSettings& S);
		/** Pays the clear bonus the first time per episode; returns true then. */
		bool TryClear(float TimeLeftFraction, const FCombatRewardSettings& S);
		/** Aim helper for the decision that ends in a state with potential NewPotential: gamma * Phi(s') - Phi(s). Pass 0 at the end. */
		void ApplyPotential(float NewPotential, float Discount);
		/** The reward of the decision that just ended: moves Pending into Episode and returns its sum. */
		float TakeDecision();
		float EpisodeReturn() const;
		float PendingTerm(ETerm Term) const { return Pending[static_cast<int32>(Term)]; }
		float EpisodeTerm(ETerm Term) const { return Episode[static_cast<int32>(Term)]; }
	};
}
