#include "Arena/MapBuilder.h"
#include "Arena/ArenaBuilder.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"

namespace
{
	/** Column "key" per tile: kind + top height. Equal keys merge into one rectangle. */
	struct FColumnKey
	{
		bool bValid = false;
		bool bWall = false;
		float TopCm = 0.f;
		bool operator==(const FColumnKey& O) const { return bValid == O.bValid && bWall == O.bWall && FMath::IsNearlyEqual(TopCm, O.TopCm); }
	};

	FColumnKey KeyFor(const FMapSpec& S, int32 R, int32 C)
	{
		FColumnKey K;
		const TCHAR Ch = S.TileAt(R, C);
		if (Ch == TEXT('x'))
		{
			return K;
		}
		K.bValid = true;
		K.bWall = (Ch == TEXT('#'));
		K.TopCm = K.bWall ? static_cast<float>(S.WallCm) : static_cast<float>(S.LevelAt(R, C) * S.LevelCm);
		return K;
	}

	/** World X range of rows [R0, R0+H): rows grow southward (smaller X). */
	FMapBlock RectBlock(const FMapSpec& S, int32 R0, int32 C0, int32 H, int32 W, float BottomCm, float TopCm)
	{
		FMapBlock B;
		B.Center = FVector((S.Rows() - R0 - 0.5 * H) * S.TileCm, (C0 + 0.5 * W) * S.TileCm, 0.5 * (BottomCm + TopCm));
		B.Size = FVector(H * S.TileCm, W * S.TileCm, TopCm - BottomCm);
		return B;
	}
}

namespace FPSArena
{
TArray<FMapBlock> ComputeMapBlocks(const FMapSpec& S)
{
	TArray<FMapBlock> Blocks;
	const int32 Rows = S.Rows();
	const int32 Cols = S.Cols();

	// 1) Floor/wall columns: greedy rectangles of equal key.
	TArray<bool> Done;
	Done.Init(false, Rows * Cols);
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			const FColumnKey K = KeyFor(S, R, C);
			if (Done[R * Cols + C] || !K.bValid)
			{
				continue;
			}
			int32 W = 1;
			while (C + W < Cols && !Done[R * Cols + C + W] && KeyFor(S, R, C + W) == K) { ++W; }
			int32 H = 1;
			for (bool bGrow = true; bGrow && R + H < Rows; )
			{
				for (int32 X = C; X < C + W; ++X)
				{
					if (Done[(R + H) * Cols + X] || !(KeyFor(S, R + H, X) == K)) { bGrow = false; break; }
				}
				if (bGrow) { ++H; }
			}
			for (int32 Y = R; Y < R + H; ++Y)
			{
				for (int32 X = C; X < C + W; ++X) { Done[Y * Cols + X] = true; }
			}
			FMapBlock B = RectBlock(S, R, C, H, W, FPSMapFormat::ColumnBaseCm, K.TopCm);
			B.bColumn = true;
			Blocks.Add(B);
		}
	}

	// 2) Cover boxes on top of the floor, 3) ramp slabs.
	const float RampRise = static_cast<float>(S.LevelCm);
	const float RampRun = static_cast<float>(S.TileCm);
	const float RampPitch = FMath::RadiansToDegrees(FMath::Atan2(RampRise, RampRun));
	for (int32 R = 0; R < Rows; ++R)
	{
		for (int32 C = 0; C < Cols; ++C)
		{
			const TCHAR Ch = S.TileAt(R, C);
			if (Ch == TEXT('l') || Ch == TEXT('h'))
			{
				const float Floor = S.FloorTopCm(R, C);
				const float Height = (Ch == TEXT('h')) ? FPSMapFormat::HighCoverCm : FPSMapFormat::LowCoverCm;
				Blocks.Add(RectBlock(S, R, C, 1, 1, Floor, Floor + Height));
			}
			else if (S.IsRamp(R, C))
			{
				const FIntPoint D = S.RampDir(R, C);                      // (dRow, dCol) toward the high end
				const FVector Up2D(-D.X, D.Y, 0.0);                       // north = +X, east = +Y
				FMapBlock B;
				B.Rotation = FRotator(RampPitch, FMath::RadiansToDegrees(FMath::Atan2(Up2D.Y, Up2D.X)), 0.f);
				B.Size = FVector(FMath::Sqrt(RampRise * RampRise + RampRun * RampRun), static_cast<float>(S.TileCm), FPSMapFormat::RampThicknessCm);
				const FVector TopAtCenter = S.TileCenter(R, C);           // midpoint of the slope
				B.Center = TopAtCenter - FRotationMatrix(B.Rotation).GetUnitAxis(EAxis::Z) * (0.5 * FPSMapFormat::RampThicknessCm);
				Blocks.Add(B);
			}
		}
	}
	return Blocks;
}

FMapLayout ComputeMapLayout(const FMapSpec& S, const FVector& Origin, float SpawnHeightCm)
{
	FMapLayout L;
	const FVector Center = Origin + S.Center();
	for (int32 R = 0; R < S.Rows(); ++R)
	{
		for (int32 C = 0; C < S.Cols(); ++C)
		{
			if (S.TileAt(R, C) != TEXT('.'))
			{
				continue;
			}
			const FVector Pos = Origin + S.TileCenter(R, C) + FVector(0.f, 0.f, SpawnHeightCm);
			const FRotator Facing(0.f, (Center - Pos).GetSafeNormal2D().Rotation().Yaw, 0.f);
			switch (S.ZoneAt(R, C))
			{
			case TEXT('a'): L.AttackerSpawns.Add(FTransform(Facing, Pos)); break;
			case TEXT('d'): L.DefenderSpawns.Add(FTransform(Facing, Pos)); L.BotSpots.Add(Pos); break;
			case TEXT('A'): case TEXT('B'): case TEXT('C'): case TEXT('m'): L.BotSpots.Add(Pos); break;
			default: break;
			}
		}
	}
	L.Bounds = FBox(Origin + FVector(0.f, 0.f, FPSMapFormat::ColumnBaseCm),
		Origin + FVector(S.Rows() * S.TileCm, S.Cols() * S.TileCm, S.WallCm));
	return L;
}

FMapLayout BuildMap(UWorld* World, const FMapSpec& Spec, const FVector& Origin)
{
	const TArray<FMapBlock> Blocks = ComputeMapBlocks(Spec);
	for (const FMapBlock& B : Blocks)
	{
		SpawnBlockRotated(World, Origin + B.Center, B.Size, B.Rotation);
	}
	FMapLayout L = ComputeMapLayout(Spec, Origin, UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f);
	L.BlockCount = Blocks.Num();
	return L;
}
} // namespace FPSArena
