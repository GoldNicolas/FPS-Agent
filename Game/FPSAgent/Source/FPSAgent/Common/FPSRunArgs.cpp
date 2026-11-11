#include "Common/FPSRunArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

FFPSRunArgs FFPSRunArgs::Parse(const TCHAR* CommandLine)
{
	FFPSRunArgs Args;
	FParse::Value(CommandLine, TEXT("Stage="), Args.Stage);
	FParse::Value(CommandLine, TEXT("Seed="), Args.Seed);
	FParse::Value(CommandLine, TEXT("Arenas="), Args.Arenas);
	Args.Arenas = FMath::Max(1, Args.Arenas);
	return Args;
}

const FFPSRunArgs& FFPSRunArgs::Get()
{
	static const FFPSRunArgs Args = Parse(FCommandLine::Get());
	return Args;
}

FString FFPSRunArgs::ToString() const
{
	return FString::Printf(TEXT("Stage=%s Seed=%d Arenas=%d"), *Stage, Seed, Arenas);
}
