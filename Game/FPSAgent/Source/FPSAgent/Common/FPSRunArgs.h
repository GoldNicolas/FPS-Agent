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
	/** -Iterations=N: the trainer stops after N iterations (0 = use settings). */
	int32 Iterations = 0;

	bool IsSpikeA() const { return Stage.Equals(TEXT("SpikeA"), ESearchCase::IgnoreCase); }

	static FFPSRunArgs Parse(const TCHAR* CommandLine);
	/** Parsed from FCommandLine::Get() on first use. */
	static const FFPSRunArgs& Get();
	FString ToString() const;
};
