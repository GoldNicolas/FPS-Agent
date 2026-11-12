#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Pure math for the Spike A toy task (no world, no Learning Agents): unit-tested in Tests/ReachTaskTests.cpp. */
namespace FPSReach
{
	/** Per-episode RNG seed from -Seed, the arena index and the episode count (CLAUDE.md determinism rule). */
	FPSAGENT_API int32 EpisodeSeed(int32 BaseSeed, int32 ArenaIndex, int32 EpisodeCount);

	/** Arena centers on a square grid (ceil(sqrt(N)) wide), SpacingCm apart. Row -> +X, column -> +Y. */
	FPSAGENT_API TArray<FVector> ArenaCenters(int32 Count, float SpacingCm, const FVector& Origin);

	/** Random start inside the walls (Z = ArenaCenter.Z). */
	FPSAGENT_API FVector SampleStart(FRandomStream& Rng, const FVector& ArenaCenter, const FReachTaskSettings& S);

	/**
	 * Random target TargetMin..MaxDistance from Start, inside the walls, at Start's height.
	 * If no sample fits after a few tries, it uses the inner corner farthest from Start (still inside, never on Start).
	 */
	FPSAGENT_API FVector SampleTarget(FRandomStream& Rng, const FVector& ArenaCenter, const FVector& Start, const FReachTaskSettings& S);

	FPSAGENT_API bool IsReached(float DistanceCm, const FReachTaskSettings& S);

	/** Progress (meters closed) * ProgressRewardPerMeter - TimePenaltyPerSecond * dt, + ReachBonus on the reaching step. */
	FPSAGENT_API float StepReward(float PrevDistanceCm, float DistanceCm, bool bReached, float DeltaSeconds, const FReachTaskSettings& S);

	/**
	 * One agent's episode bookkeeping, shared by training and playback (fix for C-004).
	 * Learning Agents asks for the completion BEFORE the reward on each step, while playback scores first. So the end of
	 * an episode is a pure function of the current distance (IsTerminal), never a flag set while scoring, and Score pays
	 * the reach bonus at most once per episode, in whatever order the two are called.
	 */
	struct FPSAGENT_API FReachEpisode
	{
		/** Distance at the previous Score (or the episode start): the base for the progress reward. */
		float LastDistanceCm = 0.f;
		float Return = 0.f;
		int32 Steps = 0;
		/** The target was reached (and the bonus paid) this episode. */
		bool bReached = false;

		/** Starts a new episode at this distance to the target. */
		void Begin(float StartDistanceCm);
		/** Reward for the step that just ran, ending at DistanceCm. */
		float Score(float DistanceCm, float DeltaSeconds, const FReachTaskSettings& S);
		/** The episode ends now (target within reach). Safe to call any number of times, before or after Score. */
		static bool IsTerminal(float DistanceCm, const FReachTaskSettings& S) { return IsReached(DistanceCm, S); }
	};
}
