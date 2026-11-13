#include "Arena/TileGraph.h"
#include "Algo/Reverse.h"

namespace
{
	const FIntPoint Dirs[4] = { FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	/** grid.py _edge_level: the floor level of a tile along its edge facing D; INDEX_NONE = a ramp's side (no walkable edge). */
	int32 EdgeLevel(const FMapSpec& S, const FIntPoint& T, const FIntPoint& D)
	{
		const int32 H = S.LevelAt(T.X, T.Y);
		if (S.IsRamp(T.X, T.Y))
		{
			const FIntPoint R = S.RampDir(T.X, T.Y);
			if (D == R) { return H + 1; }
			if (D == FIntPoint(-R.X, -R.Y)) { return H; }
			return INDEX_NONE;
		}
		return H;
	}

	/** grid.py obstacle_top_cm. */
	float ObstacleTopCm(const FMapSpec& S, int32 R, int32 C)
	{
		const TCHAR Ch = S.TileAt(R, C);
		if (Ch == TEXT('#')) { return TNumericLimits<float>::Max(); }
		if (Ch == TEXT('x')) { return -TNumericLimits<float>::Max(); }
		const float Top = S.FloorTopCm(R, C);
		if (Ch == TEXT('h')) { return Top + FPSMapFormat::HighCoverCm; }
		if (Ch == TEXT('l')) { return Top + FPSMapFormat::LowCoverCm; }
		return Top;
	}

	bool InGrid(const FMapSpec& S, const FIntPoint& T)
	{
		return T.X >= 0 && T.Y >= 0 && T.X < S.Rows() && T.Y < S.Cols();
	}
}

namespace FPSNav
{
	bool CanMove(const FMapSpec& S, const FIntPoint& U, const FIntPoint& V)
	{
		if (!InGrid(S, U) || !InGrid(S, V) || !S.IsWalkable(U.X, U.Y) || !S.IsWalkable(V.X, V.Y))
		{
			return false;
		}
		const FIntPoint D = V - U;
		const int32 Target = EdgeLevel(S, V, FIntPoint(-D.X, -D.Y));
		if (Target == INDEX_NONE)
		{
			return false; // cannot enter a ramp from its side
		}
		int32 Source = EdgeLevel(S, U, D);
		if (Source == INDEX_NONE)
		{
			Source = S.LevelAt(U.X, U.Y); // stepping off a ramp's side: you drop from its low level at best
		}
		return Target <= Source;
	}

	FTileGraph FTileGraph::Build(const FMapSpec& S)
	{
		FTileGraph G;
		G.Rows = S.Rows();
		G.Cols = S.Cols();
		G.Walkable.Init(false, G.Rows * G.Cols);
		G.Out.SetNum(G.Rows * G.Cols);
		for (int32 R = 0; R < G.Rows; ++R)
		{
			for (int32 C = 0; C < G.Cols; ++C)
			{
				if (!S.IsWalkable(R, C))
				{
					continue;
				}
				const FIntPoint U(R, C);
				G.Walkable[G.Index(U)] = true;
				for (const FIntPoint& D : Dirs)
				{
					const FIntPoint V = U + D;
					if (G.InBounds(V) && CanMove(S, U, V))
					{
						G.Out[G.Index(U)].Add(G.Index(V));
					}
				}
			}
		}
		return G;
	}

	bool FTileGraph::HasEdge(const FIntPoint& From, const FIntPoint& To) const
	{
		return InBounds(From) && InBounds(To) && Out[Index(From)].Contains(Index(To));
	}

	TArray<FIntPoint> FTileGraph::WalkableTiles() const
	{
		TArray<FIntPoint> Tiles;
		for (int32 i = 0; i < Walkable.Num(); ++i)
		{
			if (Walkable[i]) { Tiles.Add(TileOf(i)); }
		}
		return Tiles;
	}

	int32 FTileGraph::CountStronglyConnected() const
	{
		const int32 N = Rows * Cols;
		TArray<int32> Order;
		Order.Reserve(N);
		TArray<bool> Seen;
		Seen.Init(false, N);
		for (int32 Start = 0; Start < N; ++Start)
		{
			if (!Walkable[Start] || Seen[Start])
			{
				continue;
			}
			TArray<TPair<int32, int32>> Stack; // node, next out-edge to look at
			Stack.Emplace(Start, 0);
			Seen[Start] = true;
			while (Stack.Num() > 0)
			{
				const int32 Node = Stack.Last().Key;
				const int32 Next = Stack.Last().Value;
				if (Next < Out[Node].Num())
				{
					Stack.Last().Value = Next + 1;
					const int32 V = Out[Node][Next];
					if (!Seen[V])
					{
						Seen[V] = true;
						Stack.Emplace(V, 0);
					}
				}
				else
				{
					Order.Add(Node);
					Stack.Pop();
				}
			}
		}
		TArray<TArray<int32>> In;
		In.SetNum(N);
		for (int32 U = 0; U < N; ++U)
		{
			for (const int32 V : Out[U]) { In[V].Add(U); }
		}
		TArray<bool> Assigned;
		Assigned.Init(false, N);
		int32 Count = 0;
		for (int32 i = Order.Num() - 1; i >= 0; --i)
		{
			const int32 Root = Order[i];
			if (Assigned[Root])
			{
				continue;
			}
			++Count;
			TArray<int32> Stack;
			Stack.Add(Root);
			Assigned[Root] = true;
			while (Stack.Num() > 0)
			{
				const int32 U = Stack.Pop();
				for (const int32 V : In[U])
				{
					if (!Assigned[V])
					{
						Assigned[V] = true;
						Stack.Add(V);
					}
				}
			}
		}
		return Count;
	}

	TArray<FIntPoint> FTileGraph::FindPath(const FIntPoint& Start, const FIntPoint& Goal) const
	{
		TArray<FIntPoint> Path;
		if (!IsWalkable(Start) || !IsWalkable(Goal))
		{
			return Path;
		}
		const int32 N = Rows * Cols;
		const int32 S = Index(Start);
		const int32 G = Index(Goal);
		TArray<int32> Cost;
		Cost.Init(MAX_int32, N);
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, N);
		TArray<bool> Closed;
		Closed.Init(false, N);
		auto H = [this, &Goal](int32 I)
		{
			const FIntPoint T = TileOf(I);
			return FMath::Abs(T.X - Goal.X) + FMath::Abs(T.Y - Goal.Y);
		};
		// Maps are at most a few hundred tiles: a linear scan over the open set is fast and keeps the tie-break explicit.
		TArray<int32> Open;
		Open.Add(S);
		Cost[S] = 0;
		while (Open.Num() > 0)
		{
			int32 BestK = 0;
			for (int32 k = 1; k < Open.Num(); ++k)
			{
				const int32 A = Open[k];
				const int32 B = Open[BestK];
				const int32 FA = Cost[A] + H(A);
				const int32 FB = Cost[B] + H(B);
				if (FA < FB || (FA == FB && (H(A) < H(B) || (H(A) == H(B) && A < B))))
				{
					BestK = k;
				}
			}
			const int32 U = Open[BestK];
			Open.RemoveAtSwap(BestK);
			if (U == G)
			{
				break;
			}
			if (Closed[U])
			{
				continue;
			}
			Closed[U] = true;
			for (const int32 V : Out[U])
			{
				if (Closed[V])
				{
					continue;
				}
				const int32 C = Cost[U] + 1;
				if (C < Cost[V])
				{
					Cost[V] = C;
					Parent[V] = U;
					Open.AddUnique(V);
				}
			}
		}
		if (Cost[G] == MAX_int32)
		{
			return Path;
		}
		for (int32 I = G; I != INDEX_NONE; I = Parent[I])
		{
			Path.Add(TileOf(I));
		}
		Algo::Reverse(Path);
		return Path;
	}

	bool HasTileLos(const FMapSpec& S, const FIntPoint& A, const FIntPoint& B, float EyeCm, float Step)
	{
		const double Za = S.FloorTopCm(A.X, A.Y) + EyeCm;
		const double Zb = S.FloorTopCm(B.X, B.Y) + EyeCm;
		const FVector2D Pa(A.X + 0.5, A.Y + 0.5);
		const FVector2D Pb(B.X + 0.5, B.Y + 0.5);
		const double Dist = FVector2D::Distance(Pa, Pb);
		const int32 N = FMath::Max(1, static_cast<int32>(Dist / FMath::Max(1e-3f, Step)));
		for (int32 i = 1; i < N; ++i)
		{
			const double T = static_cast<double>(i) / N;
			const int32 R = FMath::FloorToInt32(Pa.X + (Pb.X - Pa.X) * T);
			const int32 C = FMath::FloorToInt32(Pa.Y + (Pb.Y - Pa.Y) * T);
			if ((R == A.X && C == A.Y) || (R == B.X && C == B.Y))
			{
				continue;
			}
			if (Za + (Zb - Za) * T <= ObstacleTopCm(S, R, C))
			{
				return false;
			}
		}
		return true;
	}

	FIntPoint TileAt(const FMapSpec& S, const FVector2D& Local)
	{
		// Inverse of FMapSpec::TileCenter: X = (Rows - 1 - R + 0.5) * Tile, Y = (C + 0.5) * Tile.
		const int32 R = S.Rows() - 1 - FMath::FloorToInt32(Local.X / S.TileCm);
		const int32 C = FMath::FloorToInt32(Local.Y / S.TileCm);
		return FIntPoint(FMath::Clamp(R, 0, S.Rows() - 1), FMath::Clamp(C, 0, S.Cols() - 1));
	}

	FVector2D TileCenterLocal(const FMapSpec& S, const FIntPoint& T)
	{
		const FVector C = S.TileCenter(T.X, T.Y);
		return FVector2D(C.X, C.Y);
	}
}
