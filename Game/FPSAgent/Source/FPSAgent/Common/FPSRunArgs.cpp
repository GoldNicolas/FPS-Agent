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
	FParse::Value(CommandLine, TEXT("-StartDifficulty="), Args.StartDifficulty);
	Args.bEval = FParse::Param(CommandLine, TEXT("Eval"));
	FParse::Value(CommandLine, TEXT("-EvalEpisodes="), Args.EvalEpisodes);
	FParse::Value(CommandLine, TEXT("-EvalJson="), Args.EvalJson);
	FParse::Value(CommandLine, TEXT("-StatsCsv="), Args.StatsCsv);
	FParse::Value(CommandLine, TEXT("-Set="), Args.SetOverrides, /*bShouldStopOnSeparator=*/false);
	Args.bInfer = Args.bInfer || Args.bEval;
	Args.StartDifficulty = FMath::Clamp(Args.StartDifficulty, 0.f, 1.f);
	Args.EvalEpisodes = FMath::Max(1, Args.EvalEpisodes);
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

int32 FFPSRunArgs::CombatStageIndexOf(const FString& InStage)
{
	if (InStage.Len() == 2 && (InStage[0] == TEXT('S') || InStage[0] == TEXT('s')) && InStage[1] >= TEXT('0') && InStage[1] <= TEXT('3'))
	{
		return InStage[1] - TEXT('0');
	}
	return -1;
}

FString FFPSRunArgs::ToString() const
{
	return FString::Printf(TEXT("Stage=%s Seed=%d Arenas=%d Map=%s Infer=%d Scripted=%d ResumeFrom=%s Iterations=%d Batches=%d StartDifficulty=%.2f Eval=%d EvalEpisodes=%d EvalJson=%s StatsCsv=%s Set=%s"),
		Stage.IsEmpty() ? TEXT("(unset)") : *Stage, Seed, Arenas, Map.IsEmpty() ? TEXT("(unset)") : *Map, bInfer ? 1 : 0, bScripted ? 1 : 0,
		ResumeFrom.IsEmpty() ? TEXT("(unset)") : *ResumeFrom, Iterations, Batches, StartDifficulty, bEval ? 1 : 0, EvalEpisodes,
		EvalJson.IsEmpty() ? TEXT("(unset)") : *EvalJson, StatsCsv.IsEmpty() ? TEXT("(unset)") : *StatsCsv,
		SetOverrides.IsEmpty() ? TEXT("(unset)") : *SetOverrides);
}
