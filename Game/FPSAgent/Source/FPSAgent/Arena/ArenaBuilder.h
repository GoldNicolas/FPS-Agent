#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

class AStaticMeshActor;
class UWorld;

/** Runtime level geometry from data: no .umap needed, works headless. */
namespace FPSArena
{
	/** Axis-aligned gray box. Center and Size in cm. Blocks movement, visibility and the Weapon channel. */
	FPSAGENT_API AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Center, const FVector& Size);

	/** Gray box with a rotation (ramps). Size is in the box's local axes. */
	FPSAGENT_API AStaticMeshActor* SpawnBlockRotated(UWorld* World, const FVector& Center, const FVector& Size, const FRotator& Rotation);

	struct FRangeLayout
	{
		FTransform PlayerStart;
		/** One per FRangeSettings::Lanes entry, same order. */
		TArray<FTransform> BotSpawns;
	};

	/** Pure: player at Origin facing +X; lane bots in front, facing the player. SpawnHeightCm lifts capsules off the floor. */
	FPSAGENT_API FRangeLayout ComputeRangeLayout(const FRangeSettings& Range, const FVector& Origin, float SpawnHeightCm);

	/** Floor + four walls around the range whose player stands at Origin (floor top = Origin.Z). */
	FPSAGENT_API FRangeLayout BuildRange(UWorld* World, const FRangeSettings& Range, const FVector& Origin);

	/** Sun + sky atmosphere + sky light for viewing; does nothing under -nullrhi. */
	FPSAGENT_API void SpawnPreviewLighting(UWorld* World);
}
