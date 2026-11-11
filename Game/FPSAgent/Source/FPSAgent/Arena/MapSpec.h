#pragma once

#include "CoreMinimal.h"

/**
 * Map format v1 (README -> Maps; Python twin: tools/mapgen/mapgen/spec.py).
 * Three equal-size character grids. Row 0 = north = +X max; column 0 = west (+Y east).
 * Tiles: '.' floor, '#' wall, 'l' low cover, 'h' high cover, 'x' void, '^' 'v' '>' '<' ramp rising N/S/E/W.
 * Heights: digit floor level (ramps: their low end). Zones: 'a' 'd' spawns, 'A' 'B' 'C' sites, 'm' mid, '.' none.
 */
namespace FPSMapFormat
{
	// Must match tools/mapgen/mapgen/spec.py
	inline constexpr float LowCoverCm = 120.f;
	inline constexpr float HighCoverCm = 250.f;
	inline constexpr float ColumnBaseCm = -50.f;   // floor/wall columns start here (below level 0)
	inline constexpr float RampThicknessCm = 20.f;
}

struct FPSAGENT_API FMapSpec
{
	FString Name;
	int32 TileCm = 400;
	int32 LevelCm = 100;
	int32 WallCm = 600;
	TArray<FString> Tiles;
	TArray<FString> Heights;
	TArray<FString> Zones;

	int32 Rows() const { return Tiles.Num(); }
	int32 Cols() const { return Tiles.Num() > 0 ? Tiles[0].Len() : 0; }
	TCHAR TileAt(int32 R, int32 C) const { return Tiles[R][C]; }
	TCHAR ZoneAt(int32 R, int32 C) const { return Zones[R][C]; }
	int32 LevelAt(int32 R, int32 C) const;
	bool IsRamp(int32 R, int32 C) const;
	bool IsWalkable(int32 R, int32 C) const;
	/** (dRow, dCol) toward the ramp's high end as (X, Y); (0,0) if not a ramp. */
	FIntPoint RampDir(int32 R, int32 C) const;
	/** Floor top in cm; a ramp counts as its midpoint (low level + half a level). */
	float FloorTopCm(int32 R, int32 C) const;
	/** Tile center at floor-top height, relative to the map origin (cm). */
	FVector TileCenter(int32 R, int32 C) const;
	/** Map center at level 0, relative to the map origin. */
	FVector Center() const;

	/** Parses JSON text; on failure returns false and an error starting with the failed check ("json", "dims", "chars"). */
	static bool Parse(const FString& Json, FMapSpec& Out, FString& OutError);
	/** Loads Content/MapSpecs/<MapName>.json (works in the editor and in packaged builds via UFS staging). */
	static bool LoadFromContent(const FString& MapName, FMapSpec& Out, FString& OutError);
	/** Map names listed in Content/MapSpecs/pool.json. */
	static TArray<FString> LoadPoolNames();
};
