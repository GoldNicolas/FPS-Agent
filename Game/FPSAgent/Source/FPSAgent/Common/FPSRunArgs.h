#pragma once

#include "CoreMinimal.h"

/** Our command-line arguments (documented in README -> Cluster). Parsed once; logged at startup. */
struct FPSAGENT_API FFPSRunArgs
{
	/** -Stage=S0..S3 (curriculum) or SpikeA (toy training task). */
	FString Stage = TEXT("S0");
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

	bool IsSpikeA() const { return Stage.Equals(TEXT("SpikeA"), ESearchCase::IgnoreCase); }
	/** Gradient iterations to request from the trainer: -Batches, else -Iterations, else SettingsIterations. */
	int32 GradientIterations(int32 IterationsPerGather, int32 SettingsIterations) const;

	static FFPSRunArgs Parse(const TCHAR* CommandLine);
	/** Parsed from FCommandLine::Get() on first use. */
	static const FFPSRunArgs& Get();
	FString ToString() const;
};
