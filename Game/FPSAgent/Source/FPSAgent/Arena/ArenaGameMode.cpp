#include "Arena/ArenaGameMode.h"
#include "Arena/BeanHUD.h"
#include "Arena/MapSpec.h"
#include "Bots/RangeBot.h"
#include "Character/BeanCharacter.h"
#include "Character/BeanPlayerController.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Common/SettingsOverride.h"
#include "Eval/SpikeCProbe.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Learning/CombatTaskActor.h"
#include "Learning/ReachTaskActor.h"

static TAutoConsoleVariable<FString> CVarPlayMap(
	TEXT("fps.PlayMap"), TEXT(""),
	TEXT("Arena for the next Play: range | map01 | gen_007 | pool. Overrides Project Settings; -Map= overrides this."),
	ECVF_Default);

static TAutoConsoleVariable<FString> CVarStage(
	TEXT("fps.Stage"), TEXT(""),
	TEXT("Stage for the next Play: SpikeA = toy reach task, S0 = combat aim gallery (policy playback only in the editor). -Stage= does the same."),
	ECVF_Default);

namespace
{
	/** -Set= overrides, applied once per process before anything reads the settings (spec §6, D-025). */
	bool ApplySettingsOverridesOnce()
	{
		static bool bDone = false;
		static bool bOk = true;
		if (bDone)
		{
			return bOk;
		}
		bDone = true;
		const FString& Spec = FFPSRunArgs::Get().SetOverrides;
		if (Spec.IsEmpty())
		{
			return true;
		}
		TArray<FString> Applied;
		FString Error;
		bOk = FPSSettingsOverride::Apply(*GetMutableDefault<UFPSAgentSettings>(), Spec, Applied, Error);
		if (bOk)
		{
			UE_LOG(LogFPSAgent, Display, TEXT("Settings overrides (-Set): %s"), *FString::Join(Applied, TEXT("; ")));
		}
		else
		{
			UE_LOG(LogFPSAgent, Error, TEXT("exiting (code 2): bad -Set: %s"), *Error);
		}
		return bOk;
	}
}

bool AArenaGameMode::WantsSpikeA()
{
	return FFPSRunArgs::Get().IsSpikeA() || CVarStage.GetValueOnGameThread().Equals(TEXT("SpikeA"), ESearchCase::IgnoreCase);
}

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

bool AArenaGameMode::WantsSpikeC()
{
	return FFPSRunArgs::Get().IsSpikeC() || CVarStage.GetValueOnGameThread().Equals(TEXT("SpikeC"), ESearchCase::IgnoreCase);
}

int32 AArenaGameMode::RequestedCombatStage()
{
	const int32 FromArgs = FFPSRunArgs::Get().CombatStageIndex();
	return FromArgs >= 0 ? FromArgs : FFPSRunArgs::CombatStageIndexOf(CVarStage.GetValueOnGameThread());
}

void AArenaGameMode::BuildCombat(UWorld* World)
{
	// The combat task builds its own arenas, agents and bots. The local player only watches (spectator, no bean).
	ArenaName = TEXT("combat");
	bStartPlayersAsSpectators = true;
	Bounds = ACombatTaskActor::ComputeBounds(FFPSRunArgs::Get().Arenas, FVector::ZeroVector);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ACombatTaskActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	// Watch arena 0 (centred on the origin) from above its -X edge.
	const float Half = UCombatArena::FloorHalfSizeCm();
	PlayerStart = World->SpawnActor<APlayerStart>(FVector(-Half - 1500.0, 0.0, 2500.0), FRotator(-45.0, 0.0, 0.0), Params);
}

void AArenaGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	if (!ApplySettingsOverridesOnce())
	{
		if (!GIsEditor)
		{
			FPlatformMisc::RequestExitWithStatus(false, 2);
		}
		return;
	}
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	Rng.Initialize(Args.Seed);

	if (WantsSpikeA())
	{
		BuildSpikeA(GetWorld());
		FPSArena::SpawnPreviewLighting(GetWorld());
		UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s %s"), *ArenaName, *Args.ToString());
		return;
	}

	if (WantsSpikeC())
	{
		BuildSpikeC(GetWorld());
		FPSArena::SpawnPreviewLighting(GetWorld());
		UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s %s"), *ArenaName, *Args.ToString());
		return;
	}

	if (RequestedCombatStage() >= 0)
	{
		BuildCombat(GetWorld());
		FPSArena::SpawnPreviewLighting(GetWorld());
		UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s %s"), *ArenaName, *Args.ToString());
		return;
	}

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

void AArenaGameMode::BuildSpikeC(UWorld* World)
{
	// The probe builds its own floor and bean; the local player only watches.
	ArenaName = TEXT("spikeC");
	bStartPlayersAsSpectators = true;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ASpikeCProbe>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Bounds = FBox(FVector(-4400.0, -4400.0, 0.0), FVector(4400.0, 4400.0, 600.0));
	PlayerStart = World->SpawnActor<APlayerStart>(FVector(-3000.0, -1500.0, 800.0), FRotator(-25.0, 45.0, 0.0), Params);
}

void AArenaGameMode::BuildSpikeA(UWorld* World)
{
	// The toy task builds its own arenas and agents. The local player only watches (spectator, no bean).
	ArenaName = TEXT("spikeA");
	bStartPlayersAsSpectators = true;
	const FReachTaskSettings& S = UFPSAgentSettings::Get().SpikeA;
	Bounds = AReachTaskActor::ComputeBounds(FFPSRunArgs::Get().Arenas, S, FVector::ZeroVector);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<AReachTaskActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	// Watch arena 0 from behind its -X wall, looking down at it.
	const FVector Eye(-0.5f * S.ArenaSizeCm - 600.f, 0.f, 900.f);
	PlayerStart = World->SpawnActor<APlayerStart>(Eye, FRotator(-40.f, 0.f, 0.f), Params);
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
	if (ArenaName == TEXT("spikeA") || ArenaName == TEXT("combat") || ArenaName == TEXT("spikeC"))
	{
		return; // the training tasks spawn their own agents and bots
	}
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
