#pragma once

#include "CoreMinimal.h"
#include "Arena/MapSpec.h"

namespace FPSArena
{
	/**
	 * Pure: an open floor of Tiles x Tiles walkable tiles (level 0) inside a one-tile wall ring, as a format v1 spec
	 * (README -> Maps). Used for S0 arenas; build B adds cover. Built with FPSArena::BuildMap like any other map.
	 */
	FPSAGENT_API FMapSpec MakeOpenFloorSpec(int32 Tiles, const FString& Name);
}
