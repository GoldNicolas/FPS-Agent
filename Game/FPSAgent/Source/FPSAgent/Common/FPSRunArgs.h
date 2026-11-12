#pragma once

#include "CoreMinimal.h"

/** Our command-line arguments (documented in README -> Cluster). Parsed once; logged at startup. */
struct FPSAGENT_API FFPSRunArgs
{
	/** -Stage=S0..S3 (combat curriculum) or SpikeA (toy task). Empty = not given: the game builds the play arena. */
	FString Stage;
	int32 Seed = 0;
	int32 Arenas = 1;
	/** -Map=range|map01|gen_007|pool ; empty = not given (fall back to fps.PlayMap / settings). */
	FString Map;
	/** -Infer: run the policy with no trainer (watch / evaluate). Training needs an external trainer otherwise. */
	bool bInfer = false;
	/** -Scripted (implies -Infer): agents walk straight to the target instead of using the policy (baseline/env check). */
	bool bScripted = false;
	/** -ResumeFrom=<dir>: load policy/critic/encoder/decoder.bin from this directory before starting. */
	FString ResumeFrom;
	/**
	 * -Iterations=N: the trainer stops after N GRADIENT iterations (0 = use settings). The trainer runs
	 * IterationsPerGather (32) of them per experience batch, so -Iterations=150 is only about 5 batches (C-005).
	 */
	int32 Iterations = 0;
	/** -Batches=N: the trainer stops after N experience batches (= N x IterationsPerGather gradient iterations). Wins over -Iterations. */
	int32 Batches = 0;
	/** -StartDifficulty=X: initial difficulty level of every arena, 0..1 (chained jobs pass the last logged mean). */
	float StartDifficulty = 0.f;
	/** -Eval (implies -Infer): promotion test. Fixed seeds, no training, no ramp, mean actions; writes -EvalJson and exits 0. */
	bool bEval = false;
	/** -EvalEpisodes=N: episodes to play in -Eval (default 200). */
	int32 EvalEpisodes = 200;
	/** -EvalJson=<path>: where -Eval writes its summary. */
	FString EvalJson;
	/** -StatsCsv=<path>: one row per stats window (every reward term, metrics, ramp level). */
	FString StatsCsv;
	/** -Set=Path=Value;...: settings overrides applied at startup (FPSSettingsOverride). */
	FString SetOverrides;

	bool IsSpikeA() const { return Stage.Equals(TEXT("SpikeA"), ESearchCase::IgnoreCase); }
	/** -Stage=SpikeC: the headless mechanics probe (C-006). */
	bool IsSpikeC() const { return Stage.Equals(TEXT("SpikeC"), ESearchCase::IgnoreCase); }
	/** 0..3 for S0..S3 (case-insensitive), else -1. */
	static int32 CombatStageIndexOf(const FString& InStage);
	int32 CombatStageIndex() const { return CombatStageIndexOf(Stage); }
	bool IsCombatStage() const { return CombatStageIndex() >= 0; }
	/** Gradient iterations to request from the trainer: -Batches, else -Iterations, else SettingsIterations. */
	int32 GradientIterations(int32 IterationsPerGather, int32 SettingsIterations) const;

	static FFPSRunArgs Parse(const TCHAR* CommandLine);
	/** Parsed from FCommandLine::Get() on first use. */
	static const FFPSRunArgs& Get();
	FString ToString() const;
};
