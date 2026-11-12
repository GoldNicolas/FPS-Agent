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
}
