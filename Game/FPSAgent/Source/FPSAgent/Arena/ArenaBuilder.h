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
}
