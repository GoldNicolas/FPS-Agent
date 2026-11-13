#pragma once

#include "CoreMinimal.h"
#include "Learning/CombatReward.h"

/** Episode records, stats windows, the log line, -StatsCsv rows and the -Eval JSON (spec §1 point 5, §6). Pure. */
namespace FPSCombatStats
{
	struct FEpisodeRecord
	{
		bool bCleared = false;
		bool bDied = false;
		bool bTimedOut = false;
		bool bReview = false;
		float Level = 0.f;
		float Return = 0.f;
		float Terms[FPSCombatReward::TermCount] = {};
		int32 Kills = 0;
		int32 Bots = 0;
		int32 Shots = 0;
		int32 Hits = 0;
		int32 Headshots = 0;
		float Seconds = 0.f;
	};

	struct FPSAGENT_API FWindow
	{
		int32 Episodes = 0;
		int32 Clears = 0;
		int32 Deaths = 0;
		int32 Timeouts = 0;
		double ReturnSum = 0.0;
		double TermSums[FPSCombatReward::TermCount] = {};
		double ClearSecondsSum = 0.0;
		int64 Kills = 0;
		int64 Shots = 0;
		int64 Hits = 0;
		int64 Headshots = 0;
		/** Non-review episodes at a level of at least TopLevel (the promotion trigger's gate-level clear rate, D-031). */
		int32 TopEpisodes = 0;
		int32 TopClears = 0;
		float TopLevel = 0.95f;

		void Add(const FEpisodeRecord& R);
		void Reset() { const float Keep = TopLevel; *this = FWindow(); TopLevel = Keep; }
		double ClearRate() const;
		double ClearRateTop() const;
		double DeathRate() const;
		double TimeoutRate() const;
		double AvgReturn() const;
		double AvgTerm(int32 Term) const;
		/** Hits / shots: a metric, never a reward. */
		double Accuracy() const;
		/** Headshots / hits. */
		double HeadshotRate() const;
		double AvgClearSeconds() const;
		double KillsPerEpisode() const;
	};

	struct FLevelSummary
	{
		float Mean = 0.f;
		float Min = 0.f;
		float Max = 0.f;
	};

	FPSAGENT_API FString LogLine(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond);
	FPSAGENT_API FString CsvHeader();
	FPSAGENT_API FString CsvRow(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond);
	FPSAGENT_API FString EvalJson(const FWindow& W, const FString& Stage, float Level, int32 Seed, const FString& Snapshots, float PromotionClearRate);
}
