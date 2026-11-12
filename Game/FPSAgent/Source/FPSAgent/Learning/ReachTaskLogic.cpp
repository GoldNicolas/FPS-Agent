#include "Learning/ReachTaskLogic.h"

namespace FPSReach
{
	int32 EpisodeSeed(int32 BaseSeed, int32 ArenaIndex, int32 EpisodeCount)
	{
		uint32 H = GetTypeHash(BaseSeed);
		H = HashCombineFast(H, GetTypeHash(ArenaIndex) * 2654435761u);
		H = HashCombineFast(H, GetTypeHash(EpisodeCount) * 2246822519u + 1u);
		return static_cast<int32>(H & 0x7fffffff);
	}

	TArray<FVector> ArenaCenters(int32 Count, float SpacingCm, const FVector& Origin)
	{
		TArray<FVector> Centers;
		const int32 Width = FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(FMath::Max(1, Count)))));
		for (int32 i = 0; i < Count; ++i)
		{
			Centers.Add(Origin + FVector((i / Width) * SpacingCm, (i % Width) * SpacingCm, 0.f));
		}
		return Centers;
	}

	static float InnerHalf(const FReachTaskSettings& S)
	{
		return FMath::Max(0.f, 0.5f * S.ArenaSizeCm - S.WallMarginCm);
	}

	FVector SampleStart(FRandomStream& Rng, const FVector& ArenaCenter, const FReachTaskSettings& S)
	{
		const float H = InnerHalf(S);
		return ArenaCenter + FVector(Rng.FRandRange(-H, H), Rng.FRandRange(-H, H), 0.f);
	}

	FVector SampleTarget(FRandomStream& Rng, const FVector& ArenaCenter, const FVector& Start, const FReachTaskSettings& S)
	{
		const float H = InnerHalf(S);
		for (int32 Try = 0; Try < 32; ++Try)
		{
			const float Angle = Rng.FRandRange(0.f, 2.f * UE_PI);
			const float Dist = Rng.FRandRange(S.TargetMinDistanceCm, S.TargetMaxDistanceCm);
			const FVector P = Start + FVector(FMath::Cos(Angle) * Dist, FMath::Sin(Angle) * Dist, 0.f);
			if (FMath::Abs(P.X - ArenaCenter.X) <= H && FMath::Abs(P.Y - ArenaCenter.Y) <= H)
			{
				return P;
			}
		}
		// Farthest inner corner: at least H*sqrt(2) away, so never on top of Start.
		const float SX = Start.X >= ArenaCenter.X ? -1.f : 1.f;
		const float SY = Start.Y >= ArenaCenter.Y ? -1.f : 1.f;
		return FVector(ArenaCenter.X + SX * H, ArenaCenter.Y + SY * H, Start.Z);
	}

	bool IsReached(float DistanceCm, const FReachTaskSettings& S)
	{
		return DistanceCm <= S.ReachRadiusCm;
	}

	float StepReward(float PrevDistanceCm, float DistanceCm, bool bReached, float DeltaSeconds, const FReachTaskSettings& S)
	{
		const float ProgressMeters = (PrevDistanceCm - DistanceCm) / 100.f;
		return ProgressMeters * S.ProgressRewardPerMeter - S.TimePenaltyPerSecond * DeltaSeconds + (bReached ? S.ReachBonus : 0.f);
	}
}
