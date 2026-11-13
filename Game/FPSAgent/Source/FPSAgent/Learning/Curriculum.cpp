#include "Learning/Curriculum.h"
#include "Arena/FloorGenerator.h"
#include "Arena/TileGraph.h"

namespace
{
	/** Bots are kept at least this far apart (spawn geometry: a bean is about 70 cm wide). */
	constexpr float MinBotGapCm = 150.f;
	constexpr int32 PlacementTries = 16;

	FPSBots::EBotBehaviour SampleBehaviour(FRandomStream& Rng, const FBotBehaviourMix& Easy, const FBotBehaviourMix& Hard, float Level)
	{
		const float L = FMath::Clamp(Level, 0.f, 1.f);
		const float W[6] = {
			FMath::Lerp(Easy.Static, Hard.Static, L), FMath::Lerp(Easy.Strafe, Hard.Strafe, L), FMath::Lerp(Easy.Peek, Hard.Peek, L),
			FMath::Lerp(Easy.Crouch, Hard.Crouch, L), FMath::Lerp(Easy.Patrol, Hard.Patrol, L), FMath::Lerp(Easy.Explore, Hard.Explore, L) };
		float Sum = 0.f;
		for (const float Wi : W) { Sum += FMath::Max(0.f, Wi); }
		if (Sum <= 0.f)
		{
			return FPSBots::EBotBehaviour::Static;
		}
		float R = Rng.FRand() * Sum;
		for (int32 i = 0; i < 6; ++i)
		{
			R -= FMath::Max(0.f, W[i]);
			if (R < 0.f) { return static_cast<FPSBots::EBotBehaviour>(i); }
		}
		return FPSBots::EBotBehaviour::Explore;
	}

	/** A free tile near Near (radius 2, row-major) hidden from the agent's tile, with a free 4-neighbour the agent can see. */
	bool FindPeekPair(const FMapSpec& Map, const FPSNav::FTileGraph& G, const FIntPoint& Agent, const FIntPoint& Near, const TSet<FIntPoint>& Taken, const FVector2D& AgentLocal, float MinDist, float MaxDist, FIntPoint& OutHidden, FIntPoint& OutExposed)
	{
		static const FIntPoint Dirs[4] = { FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		for (int32 Dr = -2; Dr <= 2; ++Dr)
		{
			for (int32 Dc = -2; Dc <= 2; ++Dc)
			{
				const FIntPoint H = Near + FIntPoint(Dr, Dc);
				if (!G.IsWalkable(H) || H == Agent || Taken.Contains(H) || FPSNav::HasTileLos(Map, Agent, H))
				{
					continue;
				}
				const double HDist = FVector2D::Distance(FPSNav::TileCenterLocal(Map, H), AgentLocal);
				if (HDist < MinDist || HDist > MaxDist) // the peek tile keeps the distance knobs too
				{
					continue;
				}
				for (const FIntPoint& D : Dirs)
				{
					const FIntPoint E = H + D;
					if (G.IsWalkable(E) && E != Agent && !Taken.Contains(E) && FPSNav::HasTileLos(Map, Agent, E))
					{
						OutHidden = H;
						OutExposed = E;
						return true;
					}
				}
			}
		}
		return false;
	}

	/**
	 * A bot tile that honours the stage knobs: uniform over free, untaken tiles whose centre lies within [MinDist, MaxDist] of the
	 * agent and within +/-Spread of its facing; if none, any direction within the band; if none, the free tile closest to the band.
	 * One RNG draw when a pool exists. (-1, -1) only when no free tile is left.
	 */
	FIntPoint PickBotTile(FRandomStream& Rng, const TArray<FIntPoint>& Free, const TSet<FIntPoint>& Taken, const FMapSpec& Map,
		const FVector2D& AgentLocal, float AgentYawDeg, float MinDist, float MaxDist, float Spread)
	{
		TArray<FIntPoint> InCone, InBand;
		FIntPoint Closest(-1, -1);
		double ClosestGap = TNumericLimits<double>::Max();
		for (const FIntPoint& T : Free)
		{
			if (Taken.Contains(T))
			{
				continue;
			}
			const FVector2D D = FPSNav::TileCenterLocal(Map, T) - AgentLocal;
			const double Dist = D.Size();
			const double Gap = Dist < MinDist ? MinDist - Dist : (Dist > MaxDist ? Dist - MaxDist : 0.0);
			if (Gap < ClosestGap)
			{
				ClosestGap = Gap;
				Closest = T;
			}
			if (Gap > 0.0)
			{
				continue;
			}
			InBand.Add(T);
			const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
			if (FMath::Abs(FRotator::NormalizeAxis(Yaw - AgentYawDeg)) <= Spread)
			{
				InCone.Add(T);
			}
		}
		const TArray<FIntPoint>& Pool = InCone.Num() > 0 ? InCone : InBand;
		return Pool.Num() > 0 ? Pool[Rng.RandRange(0, Pool.Num() - 1)] : Closest;
	}
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

	FCombatEpisode FromS0(const FS0Episode& E, const FMapSpec& OpenFloor)
	{
		FCombatEpisode Out;
		Out.Level = E.Level;
		Out.bReview = E.bReview;
		Out.Map = OpenFloor;
		Out.AgentYawDeg = E.AgentYawDeg;
		for (int32 i = 0; i < E.BotOffsets.Num(); ++i)
		{
			FBotSpawn Bot;
			Bot.Offset = E.BotOffsets[i];
			Bot.YawDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-Bot.Offset.Y, -Bot.Offset.X))); // face the agent at the centre ((-180, 180])
			Bot.bCrouched = E.BotCrouched.IsValidIndex(i) && E.BotCrouched[i];
			Out.Bots.Add(Bot);
		}
		return Out;
	}

	FCombatEpisode SampleS1(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, const FBotBehaviourSettings& B)
	{
		const FCombatStageSpec& S = C.S1;
		FCombatEpisode E;
		E.bReview = bAllowReview && Rng.FRand() < C.ReviewFraction;
		E.Level = E.bReview ? Rng.FRandRange(0.f, ControllerLevel) : ControllerLevel;
		const float Density = Knob(Rng, S.CoverDensityEasy, S.CoverDensityHard, E.Level, C.JitterFraction);
		E.Map = FPSArena::MakeCoverFloorSpec(S.FloorTiles, Density, S.LowCoverFraction, Rng, TEXT("s1floor"));
		const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(E.Map);
		const TArray<FIntPoint> Free = G.WalkableTiles();
		if (Free.Num() == 0)
		{
			return E;
		}
		const FVector2D MapCenter(E.Map.Center().X, E.Map.Center().Y);
		const FIntPoint AgentTile = Free[Rng.RandRange(0, Free.Num() - 1)];
		const FVector2D AgentLocal = FPSNav::TileCenterLocal(E.Map, AgentTile);
		E.AgentOffset = AgentLocal - MapCenter;
		E.AgentYawDeg = Rng.FRandRange(-180.f, 180.f);

		const int32 MaxBots = FMath::Max(S.BotsEasy, S.BotsHard);
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Knob(Rng, S.BotsEasy, S.BotsHard, E.Level, C.JitterFraction)), 1, MaxBots);
		const float MaxDist = Knob(Rng, S.BotDistanceMaxCmEasy, S.BotDistanceMaxCmHard, E.Level, C.JitterFraction);
		const float MinDist = FMath::Min(S.BotDistanceMinCm, MaxDist);
		const float Spread = Knob(Rng, S.BotSpreadDegEasy, S.BotSpreadDegHard, E.Level, C.JitterFraction);
		const float CrouchChance = Knob(Rng, S.CrouchedFractionEasy, S.CrouchedFractionHard, E.Level, C.JitterFraction);
		// Facing away from the agent by up to this much keeps the agent outside the bot's view cone, with a 10 degree margin.
		const float AwayHalfDeg = FMath::Max(0.f, 180.f - 0.5f * B.SightFovDeg - 10.f);
		TSet<FIntPoint> Taken = { AgentTile };
		for (int32 i = 0; i < Count; ++i)
		{
			FIntPoint Tile = PickBotTile(Rng, Free, Taken, E.Map, AgentLocal, E.AgentYawDeg, MinDist, MaxDist, Spread);
			if (Tile.X < 0)
			{
				break;
			}
			FBotSpawn Bot;
			Bot.Behaviour = SampleBehaviour(Rng, S.BehaviourMixEasy, S.BehaviourMixHard, E.Level);
			Bot.bCrouched = Rng.FRand() < CrouchChance;
			Bot.SpeedScale = Knob(Rng, S.BotSpeedScaleEasy, S.BotSpeedScaleHard, E.Level, C.JitterFraction);
			if (Bot.Behaviour == FPSBots::EBotBehaviour::Peek)
			{
				FIntPoint Hidden, Exposed;
				if (FindPeekPair(E.Map, G, AgentTile, Tile, Taken, AgentLocal, MinDist, MaxDist, Hidden, Exposed))
				{
					Tile = Hidden;
					Bot.PeekHidden = Hidden;
					Bot.PeekExposed = Exposed;
					Taken.Add(Exposed);
				}
				else
				{
					Bot.Behaviour = FPSBots::EBotBehaviour::Strafe; // no cover nearby (D-031)
				}
			}
			Taken.Add(Tile);
			const FVector2D Local = FPSNav::TileCenterLocal(E.Map, Tile);
			Bot.Offset = Local - MapCenter;
			const float ToAgent = FMath::RadiansToDegrees(FMath::Atan2(AgentLocal.Y - Local.Y, AgentLocal.X - Local.X));
			Bot.YawDeg = FRotator::NormalizeAxis(ToAgent + 180.f + Rng.FRandRange(-AwayHalfDeg, AwayHalfDeg));
			E.Bots.Add(Bot);
		}
		return E;
	}
}
