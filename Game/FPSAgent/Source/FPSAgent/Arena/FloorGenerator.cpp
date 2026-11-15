#include "Arena/FloorGenerator.h"
#include "Arena/TileGraph.h"

namespace FPSArena
{
	FMapSpec MakeOpenFloorSpec(int32 Tiles, const FString& Name)
	{
		FMapSpec S;
		S.Name = Name;
		const int32 N = FMath::Max(1, Tiles) + 2;
		for (int32 R = 0; R < N; ++R)
		{
			const bool bRing = (R == 0 || R == N - 1);
			S.Tiles.Add(bRing ? FString::ChrN(N, TEXT('#')) : TEXT("#") + FString::ChrN(N - 2, TEXT('.')) + TEXT("#"));
			S.Heights.Add(FString::ChrN(N, TEXT('0')));
			S.Zones.Add(FString::ChrN(N, TEXT('.')));
		}
		return S;
	}

	FMapSpec MakeCoverFloorSpec(int32 Tiles, float CoverDensity, float LowFraction, FRandomStream& Rng, const FString& Name)
	{
		FMapSpec S = MakeOpenFloorSpec(Tiles, Name);
		const int32 N = FMath::Max(1, Tiles);
		const int32 Interior = N * N;
		const float Density = CoverDensity > 0.f ? CoverDensity : 0.f; // also maps NaN to 0
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Density * Interior), 0, Interior / 2);
		if (Count == 0)
		{
			return S;
		}
		const int32 LowCount = FMath::RoundToInt(FMath::Clamp(LowFraction, 0.f, 1.f) * Count);
		TArray<FIntPoint> Cells;
		Cells.Reserve(Interior);
		for (int32 R = 1; R <= N; ++R)
		{
			for (int32 C = 1; C <= N; ++C) { Cells.Add(FIntPoint(R, C)); }
		}
		for (int32 i = Cells.Num() - 1; i > 0; --i) // one seeded Fisher-Yates: the draws don't depend on what gets skipped
		{
			Cells.Swap(i, Rng.RandRange(0, i));
		}
		int32 Placed = 0;
		for (const FIntPoint& Cell : Cells)
		{
			if (Placed >= Count)
			{
				break;
			}
			S.Tiles[Cell.X][Cell.Y] = Placed < LowCount ? TEXT('l') : TEXT('h');
			if (FPSNav::FTileGraph::Build(S).CountStronglyConnected() == 1)
			{
				++Placed;
			}
			else
			{
				S.Tiles[Cell.X][Cell.Y] = TEXT('.'); // would cut the floor apart: skip this cell
			}
		}
		return S;
	}
}
