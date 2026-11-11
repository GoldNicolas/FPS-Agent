#include "Arena/ArenaBuilder.h"
#include "Common/FPSAgentSettings.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "Misc/App.h"

namespace FPSArena
{
AStaticMeshActor* SpawnBlock(UWorld* World, const FVector& Center, const FVector& Size)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")); // 100 cm cube, centered pivot
	const FTransform Xf(FRotator::ZeroRotator, Center, Size / 100.f);
	AStaticMeshActor* Block = World->SpawnActorDeferred<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Xf, nullptr, nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	UStaticMeshComponent* Mesh = Block->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(Cube);
	Block->FinishSpawning(Xf);
	return Block;
}

FRangeLayout ComputeRangeLayout(const FRangeSettings& Range, const FVector& Origin, float SpawnHeightCm)
{
	FRangeLayout L;
	L.PlayerStart = FTransform(FRotator::ZeroRotator, Origin + FVector(0.f, 0.f, SpawnHeightCm));
	for (const FRangeLane& Lane : Range.Lanes)
	{
		L.BotSpawns.Add(FTransform(FRotator(0.f, 180.f, 0.f), Origin + FVector(Lane.DistanceCm, Lane.LateralCm, SpawnHeightCm)));
	}
	return L;
}

FRangeLayout BuildRange(UWorld* World, const FRangeSettings& Range, const FVector& Origin)
{
	const float Len = Range.ArenaLengthCm;
	const float Wid = Range.ArenaWidthCm;
	const float Hgt = Range.WallHeightCm;
	const float Thk = Range.WallThicknessCm;
	const float BackGap = 500.f; // floor extends 5 m behind the player
	const float CenterX = Origin.X + 0.5f * Len - BackGap;

	SpawnBlock(World, FVector(CenterX, Origin.Y, Origin.Z - 0.5f * Thk), FVector(Len, Wid, Thk));                              // floor
	SpawnBlock(World, FVector(CenterX, Origin.Y - 0.5f * (Wid + Thk), Origin.Z + 0.5f * Hgt), FVector(Len, Thk, Hgt));         // left
	SpawnBlock(World, FVector(CenterX, Origin.Y + 0.5f * (Wid + Thk), Origin.Z + 0.5f * Hgt), FVector(Len, Thk, Hgt));         // right
	SpawnBlock(World, FVector(Origin.X - BackGap - 0.5f * Thk, Origin.Y, Origin.Z + 0.5f * Hgt), FVector(Thk, Wid, Hgt));      // back
	SpawnBlock(World, FVector(Origin.X + Len - BackGap + 0.5f * Thk, Origin.Y, Origin.Z + 0.5f * Hgt), FVector(Thk, Wid, Hgt)); // front

	return ComputeRangeLayout(Range, Origin, UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f);
}

void SpawnPreviewLighting(UWorld* World)
{
	if (!FApp::CanEverRender())
	{
		return;
	}
	auto SpawnLight = [World](const FRotator& Rotation, float Intensity)
	{
		const FTransform Xf(Rotation, FVector::ZeroVector);
		ADirectionalLight* Light = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), Xf);
		Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
		Light->GetLightComponent()->SetIntensity(Intensity);
		Light->FinishSpawning(Xf);
	};
	SpawnLight(FRotator(-50.f, -30.f, 0.f), 8.f); // key
	SpawnLight(FRotator(-30.f, 150.f, 0.f), 3.f); // fill
}
} // namespace FPSArena
