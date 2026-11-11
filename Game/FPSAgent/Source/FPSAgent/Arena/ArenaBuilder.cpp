#include "Arena/ArenaBuilder.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

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
} // namespace FPSArena
