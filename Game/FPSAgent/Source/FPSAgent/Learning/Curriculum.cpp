#include "Learning/Curriculum.h"

namespace
{
	/** Bots are kept at least this far apart (spawn geometry: a bean is about 70 cm wide). */
	constexpr float MinBotGapCm = 150.f;
	constexpr int32 PlacementTries = 16;
}

namespace FPSCurriculum
{
	void FDifficultyController::Init(float StartLevel)
	{
		Level = FMath::Clamp(StartLevel, 0.f, 1.f);
		Window.Reset();
	}

	float FDifficultyController::Record(bool bCleared, const FCurriculumSettings& C)
	{
		const int32 N = FMath::Max(1, C.WindowEpisodes);
		Window.Add(bCleared);
		if (Window.Num() < N)
		{
			return 0.f;
		}
		while (Window.Num() > N)
		{
			Window.RemoveAt(0);
		}
		int32 Clears = 0;
		for (const bool b : Window)
		{
			Clears += b ? 1 : 0;
		}
		const float Rate = static_cast<float>(Clears) / N;
		float Target = Level;
		if (Rate > C.RaiseAboveClearRate)
		{
			Target = FMath::Min(1.f, Level + C.Step);
		}
		else if (Rate < C.LowerBelowClearRate)
		{
			Target = FMath::Max(0.f, Level - C.Step);
		}
		const float Change = Target - Level;
		if (!FMath::IsNearlyZero(Change))
		{
			Level = Target;
			Window.Reset();
			return Change;
		}
		return 0.f;
	}

	float Knob(FRandomStream& Rng, float Easy, float Hard, float Level, float JitterFraction)
	{
		const float Base = FMath::Lerp(Easy, Hard, FMath::Clamp(Level, 0.f, 1.f));
		const float Jitter = JitterFraction * FMath::Abs(Hard - Easy);
		const float Value = Base + (Jitter > 0.f ? Rng.FRandRange(-Jitter, Jitter) : 0.f);
		return FMath::Clamp(Value, FMath::Min(Easy, Hard), FMath::Max(Easy, Hard));
	}

	FS0Episode SampleS0(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, float MaxRadiusCm)
	{
		const FCombatStageSpec& S = C.S0;
		FS0Episode E;
		E.bReview = bAllowReview && Rng.FRand() < C.ReviewFraction;
		E.Level = E.bReview ? Rng.FRandRange(0.f, ControllerLevel) : ControllerLevel;

		const int32 MaxBots = FMath::Max(S.BotsEasy, S.BotsHard);
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Knob(Rng, S.BotsEasy, S.BotsHard, E.Level, C.JitterFraction)), 1, MaxBots);
		const float MaxDist = FMath::Min(Knob(Rng, S.BotDistanceMaxCmEasy, S.BotDistanceMaxCmHard, E.Level, C.JitterFraction), MaxRadiusCm);
		const float MinDist = FMath::Min(S.BotDistanceMinCm, MaxDist);
		const float Spread = Knob(Rng, S.BotSpreadDegEasy, S.BotSpreadDegHard, E.Level, C.JitterFraction);
		const float CrouchChance = Knob(Rng, S.CrouchedFractionEasy, S.CrouchedFractionHard, E.Level, C.JitterFraction);
		E.AgentYawDeg = Rng.FRandRange(-180.f, 180.f);

		for (int32 i = 0; i < Count; ++i)
		{
			FVector2D P = FVector2D::ZeroVector;
			for (int32 Try = 0; Try < PlacementTries; ++Try)
			{
				const float Yaw = FMath::DegreesToRadians(E.AgentYawDeg + Rng.FRandRange(-Spread, Spread));
				const float Dist = Rng.FRandRange(MinDist, MaxDist);
				P = FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * Dist;
				bool bFree = true;
				for (const FVector2D& Q : E.BotOffsets)
				{
					if (FVector2D::Distance(P, Q) < MinBotGapCm) { bFree = false; break; }
				}
				if (bFree) { break; }
			}
			E.BotOffsets.Add(P);
			E.BotCrouched.Add(Rng.FRand() < CrouchChance);
		}
		return E;
	}
}
