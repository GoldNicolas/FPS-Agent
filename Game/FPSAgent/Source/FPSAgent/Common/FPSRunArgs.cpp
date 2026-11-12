#include "Common/FPSRunArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FFPSRunArgs FFPSRunArgs::Parse(const TCHAR* CommandLine)
{
	FFPSRunArgs Args;
	FParse::Value(CommandLine, TEXT("Stage="), Args.Stage);
	FParse::Value(CommandLine, TEXT("Seed="), Args.Seed);
	FParse::Value(CommandLine, TEXT("Arenas="), Args.Arenas);
	FParse::Value(CommandLine, TEXT("-Map="), Args.Map);
	Args.bScripted = FParse::Param(CommandLine, TEXT("Scripted"));
	Args.bInfer = FParse::Param(CommandLine, TEXT("Infer")) || Args.bScripted;
	FParse::Value(CommandLine, TEXT("-ResumeFrom="), Args.ResumeFrom);
	FParse::Value(CommandLine, TEXT("-Iterations="), Args.Iterations);
	FParse::Value(CommandLine, TEXT("-Batches="), Args.Batches);
	Args.Arenas = FMath::Max(1, Args.Arenas);
	Args.Iterations = FMath::Max(0, Args.Iterations);
	Args.Batches = FMath::Max(0, Args.Batches);
	return Args;
}

int32 FFPSRunArgs::GradientIterations(int32 IterationsPerGather, int32 SettingsIterations) const
{
	if (Batches > 0)
	{
		const int64 Total = static_cast<int64>(Batches) * FMath::Max(1, IterationsPerGather);
		return static_cast<int32>(FMath::Min<int64>(Total, MAX_int32));
	}
	return Iterations > 0 ? Iterations : SettingsIterations;
}

const FFPSRunArgs& FFPSRunArgs::Get()
{
	static const FFPSRunArgs Args = Parse(FCommandLine::Get());
	return Args;
}

FString FFPSRunArgs::ToString() const
{
	return FString::Printf(TEXT("Stage=%s Seed=%d Arenas=%d Map=%s Infer=%d Scripted=%d ResumeFrom=%s Iterations=%d Batches=%d"),
		*Stage, Seed, Arenas, Map.IsEmpty() ? TEXT("(unset)") : *Map, bInfer ? 1 : 0, bScripted ? 1 : 0,
		ResumeFrom.IsEmpty() ? TEXT("(unset)") : *ResumeFrom, Iterations, Batches);
}
