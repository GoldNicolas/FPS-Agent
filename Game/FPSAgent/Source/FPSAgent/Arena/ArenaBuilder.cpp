#include "Arena/ArenaBuilder.h"
#include "Common/FPSAgentSettings.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
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
	// One sun shining from behind the player (+X) so the fronts of the bots are lit; it also drives the sky.
	const FRotator SunRotation(-40.f, 15.f, 0.f); // light travels +X (away from the player), 40 deg above the horizon
	ADirectionalLight* Sun = World->SpawnActorDeferred<ADirectionalLight>(ADirectionalLight::StaticClass(), FTransform::Identity);
	UDirectionalLightComponent* SunLight = CastChecked<UDirectionalLightComponent>(Sun->GetLightComponent());
	SunLight->SetMobility(EComponentMobility::Movable);
	SunLight->SetIntensity(10.f);
	SunLight->SetAtmosphereSunLight(true);
	Sun->FinishSpawning(FTransform::Identity);
	// Spawning composes the spawn transform with the class default root rotation (~-46 pitch for directional
	// lights), so set the final rotation explicitly.
	Sun->SetActorRotation(SunRotation);

	// Blue sky + ambient fill from a real-time captured sky light.
	World->SpawnActor<ASkyAtmosphere>(FVector::ZeroVector, FRotator::ZeroRotator);
	ASkyLight* Sky = World->SpawnActorDeferred<ASkyLight>(ASkyLight::StaticClass(), FTransform::Identity);
	USkyLightComponent* SkyLight = Sky->GetLightComponent();
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SourceType = SLS_CapturedScene;
	SkyLight->bRealTimeCapture = true;
	Sky->FinishSpawning(FTransform::Identity);
}
} // namespace FPSArena
