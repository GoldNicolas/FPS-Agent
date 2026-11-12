#include "Arena/FloorGenerator.h"

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
}
