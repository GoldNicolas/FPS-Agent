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
		const FMapSpec Open = MakeOpenFloorSpec(Tiles, Name);
		const int32 N = FMath::Max(1, Tiles);
		const int32 Interior = N * N;
		const int32 Count = FMath::Clamp(FMath::RoundToInt(FMath::Max(0.f, CoverDensity) * Interior), 0, Interior / 2);
		if (Count == 0)
		{
			return Open;
		}
		const int32 LowCount = FMath::RoundToInt(FMath::Clamp(LowFraction, 0.f, 1.f) * Count);
		constexpr int32 MaxLayouts = 8;
		for (int32 Attempt = 0; Attempt < MaxLayouts; ++Attempt)
		{
			TArray<FIntPoint> Cells;
			Cells.Reserve(Interior);
			for (int32 R = 1; R <= N; ++R)
			{
				for (int32 C = 1; C <= N; ++C) { Cells.Add(FIntPoint(R, C)); }
			}
			for (int32 i = Cells.Num() - 1; i > 0; --i) // seeded Fisher-Yates
			{
				Cells.Swap(i, Rng.RandRange(0, i));
			}
			FMapSpec S = Open;
			for (int32 k = 0; k < Count; ++k)
			{
				S.Tiles[Cells[k].X][Cells[k].Y] = k < LowCount ? TEXT('l') : TEXT('h');
			}
			if (FPSNav::FTileGraph::Build(S).CountStronglyConnected() == 1)
			{
				return S;
			}
		}
		return Open;
	}
}
