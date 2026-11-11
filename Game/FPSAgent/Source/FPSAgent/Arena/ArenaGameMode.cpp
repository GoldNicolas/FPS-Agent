#include "Arena/ArenaGameMode.h"
#include "Arena/BeanHUD.h"
#include "Arena/MapSpec.h"
#include "Bots/RangeBot.h"
#include "Character/BeanCharacter.h"
#include "Character/BeanPlayerController.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"

static TAutoConsoleVariable<FString> CVarPlayMap(
	TEXT("fps.PlayMap"), TEXT(""),
	TEXT("Arena for the next Play: range | map01 | gen_007 | pool. Overrides Project Settings; -Map= overrides this."),
	ECVF_Default);

AArenaGameMode::AArenaGameMode()
{
	DefaultPawnClass = ABeanCharacter::StaticClass();
	PlayerControllerClass = ABeanPlayerController::StaticClass();
	HUDClass = ABeanHUD::StaticClass();
}

FString AArenaGameMode::ResolveArenaName(const FString& Requested, const TArray<FString>& PoolNames, FRandomStream& Rng)
{
	if (Requested.IsEmpty() || Requested.Equals(TEXT("range"), ESearchCase::IgnoreCase))
	{
		return TEXT("range");
	}
	if (Requested.Equals(TEXT("pool"), ESearchCase::IgnoreCase))
	{
		return PoolNames.Num() > 0 ? PoolNames[Rng.RandRange(0, PoolNames.Num() - 1)] : TEXT("range");
	}
	return Requested;
}

void AArenaGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	Rng.Initialize(Args.Seed);

	FString Requested = Args.Map;
	if (Requested.IsEmpty()) { Requested = CVarPlayMap.GetValueOnGameThread(); }
	if (Requested.IsEmpty()) { Requested = UFPSAgentSettings::Get().Map.PlayMap; }
	const bool bPool = Requested.Equals(TEXT("pool"), ESearchCase::IgnoreCase);
	const FString Resolved = ResolveArenaName(Requested, bPool ? FMapSpec::LoadPoolNames() : TArray<FString>(), Rng);

	UWorld* World = GetWorld();
	if (Resolved == TEXT("range") || !BuildMapArena(World, Resolved))
	{
		BuildRangeArena(World);
	}
	FPSArena::SpawnPreviewLighting(World);
	UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s (requested '%s') %s"), *ArenaName, *Requested, *Args.ToString());
}

void AArenaGameMode::BuildRangeArena(UWorld* World)
{
	ArenaName = TEXT("range");
	const FRangeSettings& Range = UFPSAgentSettings::Get().Range;
	RangeLayout = FPSArena::BuildRange(World, Range, FVector::ZeroVector);
	Bounds = FBox(FVector(-500.f, -0.5f * Range.ArenaWidthCm, 0.f), FVector(Range.ArenaLengthCm - 500.f, 0.5f * Range.ArenaWidthCm, Range.WallHeightCm));
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PlayerStart = World->SpawnActor<APlayerStart>(RangeLayout.PlayerStart.GetLocation(), RangeLayout.PlayerStart.Rotator(), Params);
}

bool AArenaGameMode::BuildMapArena(UWorld* World, const FString& SpecName)
{
	FMapSpec Spec;
	FString Error;
	if (!FMapSpec::LoadFromContent(SpecName, Spec, Error))
	{
		UE_LOG(LogFPSAgent, Error, TEXT("ArenaGameMode: cannot load map '%s' (%s); falling back to the range"), *SpecName, *Error);
		return false;
	}
	MapLayout = FPSArena::BuildMap(World, Spec, FVector::ZeroVector);
	if (MapLayout.AttackerSpawns.Num() == 0)
	{
		UE_LOG(LogFPSAgent, Error, TEXT("ArenaGameMode: map '%s' has no attacker spawn"), *SpecName);
		return false;
	}
	ArenaName = SpecName;
	Bounds = MapLayout.Bounds;
	const FTransform& Start = MapLayout.AttackerSpawns[Rng.RandRange(0, MapLayout.AttackerSpawns.Num() - 1)];
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PlayerStart = World->SpawnActor<APlayerStart>(Start.GetLocation(), Start.Rotator(), Params);
	UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: built '%s' (%d blocks, %d bot spots)"), *SpecName, MapLayout.BlockCount, MapLayout.BotSpots.Num());
	return true;
}

AActor* AArenaGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return PlayerStart ? PlayerStart.Get() : Super::ChoosePlayerStart_Implementation(Player);
}

void AArenaGameMode::StartPlay()
{
	Super::StartPlay();
	const int32 BaseSeed = FFPSRunArgs::Get().Seed;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (ArenaName == TEXT("range"))
	{
		const FRangeSettings& Range = UFPSAgentSettings::Get().Range;
		for (int32 i = 0; i < RangeLayout.BotSpawns.Num() && i < Range.Lanes.Num(); ++i)
		{
			const FTransform& Spawn = RangeLayout.BotSpawns[i];
			if (ARangeBot* Bot = GetWorld()->SpawnActor<ARangeBot>(Spawn.GetLocation(), Spawn.Rotator(), Params))
			{
				Bot->InitBot(Range.Lanes[i].Mode, BaseSeed * 1000 + i);
			}
		}
		return;
	}

	TArray<FVector> Spots = MapLayout.BotSpots;
	const FVector Center = Bounds.GetCenter();
	const int32 Count = FMath::Min(UFPSAgentSettings::Get().Map.BotCount, Spots.Num());
	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Spot = Spots[Rng.RandRange(0, Spots.Num() - 1)];
		Spots.RemoveSingleSwap(Spot);
		const FRotator Facing(0.f, (Center - Spot).GetSafeNormal2D().Rotation().Yaw, 0.f);
		if (ARangeBot* Bot = GetWorld()->SpawnActor<ARangeBot>(Spot, Facing, Params))
		{
			Bot->InitBot(Rng.FRand() < 0.5f ? ERangeBotMode::Static : ERangeBotMode::Strafe, BaseSeed * 1000 + i);
		}
	}
}
