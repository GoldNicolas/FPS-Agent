#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Arena/ArenaBuilder.h"
#include "Arena/MapBuilder.h"
#include "ArenaGameMode.generated.h"

class APlayerStart;

/**
 * Builds the play space at runtime (works on the empty Entry map and headless): the shooting range or a map spec.
 * Which one: -Map= command line > fps.PlayMap console variable > Project Settings > FPS Agent > Map > PlayMap.
 */
UCLASS()
class FPSAGENT_API AArenaGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AArenaGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	virtual void StartPlay() override;

	/** World-space bounds of the built arena (for the top-down camera). */
	const FBox& GetArenaBounds() const { return Bounds; }
	/** "range" or the spec name that was built. */
	const FString& GetArenaName() const { return ArenaName; }

	/** Resolves the requested arena name; "pool" picks a pool entry with Rng. Returns "range" for empty/"range". */
	static FString ResolveArenaName(const FString& Requested, const TArray<FString>& PoolNames, FRandomStream& Rng);

	/** -Stage=SpikeA or fps.Stage SpikeA: build the toy reach task instead of the range/map. */
	static bool WantsSpikeA();
	/** -Stage=S0..S3 or fps.Stage S0..S3: the combat stage index, else -1. */
	static int32 RequestedCombatStage();
	/** -Stage=SpikeC or fps.Stage SpikeC: the headless mechanics probe (C-006). */
	static bool WantsSpikeC();

protected:
	void BuildRangeArena(UWorld* World);
	void BuildSpikeA(UWorld* World);
	void BuildSpikeC(UWorld* World);
	void BuildCombat(UWorld* World);
	bool BuildMapArena(UWorld* World, const FString& SpecName);

	FString ArenaName = TEXT("range");
	FPSArena::FRangeLayout RangeLayout;
	FMapLayout MapLayout;
	FBox Bounds = FBox(ForceInit);
	FRandomStream Rng;

private:
	UPROPERTY() TObjectPtr<APlayerStart> PlayerStart;
};
