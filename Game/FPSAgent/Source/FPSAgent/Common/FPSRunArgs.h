#pragma once

#include "CoreMinimal.h"

/** Our command-line arguments (documented in README -> Cluster). Parsed once; logged at startup. */
struct FPSAGENT_API FFPSRunArgs
{
	FString Stage = TEXT("S0");
	int32 Seed = 0;
	int32 Arenas = 1;
	/** -Map=range|map01|gen_007|pool ; empty = not given (fall back to fps.PlayMap / settings). */
	FString Map;

	static FFPSRunArgs Parse(const TCHAR* CommandLine);
	/** Parsed from FCommandLine::Get() on first use. */
	static const FFPSRunArgs& Get();
	FString ToString() const;
};
