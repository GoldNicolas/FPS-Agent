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

	/**
	 * Pure: MakeOpenFloorSpec plus random cover tiles (S1/S2 floors, spec §6).
	 * - Count: round(CoverDensity x Tiles^2) cover tiles, at most half the interior.
	 * - Height: the first LowFraction of the placed tiles are low ('l', 120 cm); the rest are high ('h', 250 cm).
	 * - Placement: cells are tried in one seeded order. A cell that would split the walkable floor is skipped, so every walkable
	 *   tile stays in one strongly connected component (FPSNav::FTileGraph). It never falls back to an empty floor; at very
	 *   high densities slightly fewer tiles than requested may fit.
	 * - Deterministic: the Rng draws (one shuffle) don't depend on which cells are skipped.
	 */
	FPSAGENT_API FMapSpec MakeCoverFloorSpec(int32 Tiles, float CoverDensity, float LowFraction, FRandomStream& Rng, const FString& Name);
}
