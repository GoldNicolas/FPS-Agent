#pragma once

#include "CoreMinimal.h"
#include "Character/BeanCharacter.h"
#include "Bots/StrafeLogic.h"
#include "RangeBot.generated.h"

class UHealthComponent;

/** Range target: stands still or ADAD-strafes along its lane; respawns at home after death. Does not shoot (yet). */
UCLASS()
class FPSAGENT_API ARangeBot : public ABeanCharacter
{
	GENERATED_BODY()

public:
	ARangeBot(const FObjectInitializer& ObjectInitializer);

	void InitBot(ERangeBotMode InMode, int32 Seed);
	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	void HandleDied(UHealthComponent* Victim, AActor* Killer);
	void Respawn();

	ERangeBotMode Mode = ERangeBotMode::Static;
	FTransform Home;
	FStrafeState Strafe;
	FRandomStream Rng;
	FTimerHandle RespawnTimer;
};
