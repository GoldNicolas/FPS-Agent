#pragma once

#include "CoreMinimal.h"
#include "Arena/MapSpec.h"

class UWorld;

/** One gray box of map geometry (world-space relative to the map origin). */
struct FMapBlock
{
	FVector Center = FVector::ZeroVector;
	FVector Size = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	/** Floor/wall column standing on the column base (vs. cover boxes and ramp slabs). */
	bool bColumn = false;
};

/** Where things go on a built map (world space). */
struct FMapLayout
{
	TArray<FTransform> AttackerSpawns;
	TArray<FTransform> DefenderSpawns;
	/** Plain floor tiles in sites, mid and defender spawn: candidate bot positions (capsule-center height). */
	TArray<FVector> BotSpots;
	FBox Bounds = FBox(ForceInit);
	int32 BlockCount = 0;
};

namespace FPSArena
{
	/** Pure: merged floor/wall columns, cover boxes and ramp slabs for a spec (relative to the map origin). */
	FPSAGENT_API TArray<FMapBlock> ComputeMapBlocks(const FMapSpec& Spec);

	/** Pure: spawns (facing the map center), bot spots and bounds. SpawnHeightCm lifts capsules off the floor. */
	FPSAGENT_API FMapLayout ComputeMapLayout(const FMapSpec& Spec, const FVector& Origin, float SpawnHeightCm);

	/** Spawns the map's blocks into World at Origin; returns the layout (spawn height = standing capsule). */
	FPSAGENT_API FMapLayout BuildMap(UWorld* World, const FMapSpec& Spec, const FVector& Origin);
}
