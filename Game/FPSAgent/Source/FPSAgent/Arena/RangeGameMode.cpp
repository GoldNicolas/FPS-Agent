#include "Arena/RangeGameMode.h"
#include "Arena/BeanHUD.h"
#include "Character/BeanCharacter.h"
#include "Character/BeanPlayerController.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/PlayerStart.h"

ARangeGameMode::ARangeGameMode()
{
	DefaultPawnClass = ABeanCharacter::StaticClass();
	PlayerControllerClass = ABeanPlayerController::StaticClass();
	HUDClass = ABeanHUD::StaticClass();
}

void ARangeGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	UE_LOG(LogFPSAgent, Log, TEXT("RangeGameMode: %s"), *FFPSRunArgs::Get().ToString());

	UWorld* World = GetWorld();
	Layout = FPSArena::BuildRange(World, UFPSAgentSettings::Get().Range, FVector::ZeroVector);
	FPSArena::SpawnPreviewLighting(World);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	PlayerStart = World->SpawnActor<APlayerStart>(Layout.PlayerStart.GetLocation(), Layout.PlayerStart.Rotator(), Params);
}

AActor* ARangeGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	return PlayerStart ? PlayerStart.Get() : Super::ChoosePlayerStart_Implementation(Player);
}
