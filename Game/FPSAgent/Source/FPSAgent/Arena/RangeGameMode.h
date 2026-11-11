#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Arena/ArenaBuilder.h"
#include "RangeGameMode.generated.h"

class APlayerStart;

/** Shooting range built at runtime (works on the empty Entry map and headless). */
UCLASS()
class FPSAGENT_API ARangeGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARangeGameMode();
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual AActor* ChoosePlayerStart_Implementation(AController* Player) override;

protected:
	FPSArena::FRangeLayout Layout;

private:
	UPROPERTY() TObjectPtr<APlayerStart> PlayerStart;
};
