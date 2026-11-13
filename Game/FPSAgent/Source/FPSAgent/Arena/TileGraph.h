#pragma once

#include "CoreMinimal.h"
#include "Arena/MapSpec.h"

/**
 * Bot navigation without a navmesh (spec §4): walkable tiles, one-way edges and A* on an FMapSpec. Pure (no world).
 * A C++ port of tools/mapgen/mapgen/grid.py (same move and line-of-sight rules). Tiles are FIntPoint(X = row, Y = column).
 */
namespace FPSNav
{
	/** Standing bean eye above the floor top: tools/mapgen/mapgen/spec.py EYE_CM. */
	inline constexpr float StandingEyeCm = 158.f;

	struct FPSAGENT_API FTileGraph
	{
		int32 Rows = 0;
		int32 Cols = 0;
		TArray<bool> Walkable;
		/** Out-neighbours of each tile index (Row * Cols + Col), in N, S, E, W order. */
		TArray<TArray<int32>> Out;

		static FTileGraph Build(const FMapSpec& Spec);

		int32 Index(const FIntPoint& Tile) const { return Tile.X * Cols + Tile.Y; }
		FIntPoint TileOf(int32 InIndex) const { return FIntPoint(InIndex / Cols, InIndex % Cols); }
		bool InBounds(const FIntPoint& Tile) const { return Tile.X >= 0 && Tile.X < Rows && Tile.Y >= 0 && Tile.Y < Cols; }
		bool IsWalkable(const FIntPoint& Tile) const { return InBounds(Tile) && Walkable[Index(Tile)]; }
		bool HasEdge(const FIntPoint& From, const FIntPoint& To) const;
		/** Strongly connected components among walkable tiles (iterative Kosaraju, like grid.py). */
		int32 CountStronglyConnected() const;
		/** A*: unit step cost, Manhattan heuristic, ties broken by lower f, then lower h, then lower index (deterministic). Start and Goal included; empty if unreachable. */
		TArray<FIntPoint> FindPath(const FIntPoint& Start, const FIntPoint& Goal) const;
		/** Walkable tiles in row-major order. */
		TArray<FIntPoint> WalkableTiles() const;
	};

	/** grid.py can_move: one 4-neighbour step, same level or a drop; ramps only along their axis (leaving one sideways = a drop). */
	FPSAGENT_API bool CanMove(const FMapSpec& Spec, const FIntPoint& From, const FIntPoint& To);
	/** grid.py has_los: the segment between eye points above two tile centres is blocked where it passes at or below an obstacle top. */
	FPSAGENT_API bool HasTileLos(const FMapSpec& Spec, const FIntPoint& A, const FIntPoint& B, float EyeCm = StandingEyeCm, float Step = 0.25f);
	/** The tile containing a map-local XY position (cm, relative to the map origin), clamped to the grid. */
	FPSAGENT_API FIntPoint TileAt(const FMapSpec& Spec, const FVector2D& Local);
	/** Map-local XY of a tile's centre. */
	FPSAGENT_API FVector2D TileCenterLocal(const FMapSpec& Spec, const FIntPoint& Tile);
}
