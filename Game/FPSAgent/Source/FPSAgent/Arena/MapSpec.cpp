#include "Arena/MapSpec.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	const FString TileChars = TEXT("#.lhx^v><");
	const FString ZoneChars = TEXT(".adABCm");

	bool ReadRows(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<FString>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Obj->TryGetArrayField(Field, Values))
		{
			return false;
		}
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Values)
		{
			Out.Add(V->AsString());
		}
		return true;
	}

	FString MapSpecsDir()
	{
		return FPaths::Combine(FPaths::ProjectContentDir(), TEXT("MapSpecs"));
	}
}

int32 FMapSpec::LevelAt(int32 R, int32 C) const
{
	const TCHAR Ch = Heights[R][C];
	return FChar::IsDigit(Ch) ? Ch - TEXT('0') : 0;
}

FIntPoint FMapSpec::RampDir(int32 R, int32 C) const
{
	switch (TileAt(R, C))
	{
	case TEXT('^'): return FIntPoint(-1, 0);
	case TEXT('v'): return FIntPoint(1, 0);
	case TEXT('>'): return FIntPoint(0, 1);
	case TEXT('<'): return FIntPoint(0, -1);
	default:        return FIntPoint(0, 0);
	}
}

bool FMapSpec::IsRamp(int32 R, int32 C) const
{
	return RampDir(R, C) != FIntPoint(0, 0);
}

bool FMapSpec::IsWalkable(int32 R, int32 C) const
{
	return TileAt(R, C) == TEXT('.') || IsRamp(R, C);
}

float FMapSpec::FloorTopCm(int32 R, int32 C) const
{
	const float Base = static_cast<float>(LevelAt(R, C) * LevelCm);
	return IsRamp(R, C) ? Base + 0.5f * LevelCm : Base;
}

FVector FMapSpec::TileCenter(int32 R, int32 C) const
{
	return FVector((Rows() - 1 - R + 0.5) * TileCm, (C + 0.5) * TileCm, FloorTopCm(R, C));
}

FVector FMapSpec::Center() const
{
	return FVector(0.5 * Rows() * TileCm, 0.5 * Cols() * TileCm, 0.0);
}

bool FMapSpec::Parse(const FString& Json, FMapSpec& Out, FString& OutError)
{
	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Obj) || !Obj.IsValid())
	{
		OutError = TEXT("json: not a JSON object");
		return false;
	}
	FMapSpec S;
	S.Name = Obj->GetStringField(TEXT("name"));
	S.TileCm = Obj->GetIntegerField(TEXT("tile_cm"));
	S.LevelCm = Obj->GetIntegerField(TEXT("level_cm"));
	S.WallCm = Obj->GetIntegerField(TEXT("wall_cm"));
	if (!ReadRows(Obj, TEXT("tiles"), S.Tiles) || !ReadRows(Obj, TEXT("heights"), S.Heights) || !ReadRows(Obj, TEXT("zones"), S.Zones))
	{
		OutError = TEXT("json: tiles/heights/zones arrays missing");
		return false;
	}
	const int32 Rows = S.Tiles.Num();
	const int32 Cols = Rows > 0 ? S.Tiles[0].Len() : 0;
	if (Rows == 0 || Cols == 0 || S.Heights.Num() != Rows || S.Zones.Num() != Rows)
	{
		OutError = TEXT("dims: grids must be non-empty with equal row counts");
		return false;
	}
	for (int32 R = 0; R < Rows; ++R)
	{
		if (S.Tiles[R].Len() != Cols || S.Heights[R].Len() != Cols || S.Zones[R].Len() != Cols)
		{
			OutError = FString::Printf(TEXT("dims: row %d length differs from %d"), R, Cols);
			return false;
		}
		for (int32 C = 0; C < Cols; ++C)
		{
			int32 Unused;
			if (!TileChars.FindChar(S.Tiles[R][C], Unused) || !ZoneChars.FindChar(S.Zones[R][C], Unused) || !FChar::IsDigit(S.Heights[R][C]))
			{
				OutError = FString::Printf(TEXT("chars: illegal character at (%d,%d)"), R, C);
				return false;
			}
		}
	}
	Out = MoveTemp(S);
	return true;
}

bool FMapSpec::LoadFromContent(const FString& MapName, FMapSpec& Out, FString& OutError)
{
	const FString Path = FPaths::Combine(MapSpecsDir(), MapName + TEXT(".json"));
	FString Json;
	if (!FFileHelper::LoadFileToString(Json, *Path))
	{
		OutError = FString::Printf(TEXT("file: cannot read %s"), *Path);
		return false;
	}
	return Parse(Json, Out, OutError);
}

TArray<FString> FMapSpec::LoadPoolNames()
{
	TArray<FString> Names;
	FString Json;
	TSharedPtr<FJsonObject> Obj;
	if (FFileHelper::LoadFileToString(Json, *FPaths::Combine(MapSpecsDir(), TEXT("pool.json"))))
	{
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid())
		{
			ReadRows(Obj, TEXT("maps"), Names);
		}
	}
	return Names;
}
