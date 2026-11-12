#pragma once

#include "CoreMinimal.h"
#include "Character/BeanCharacter.h"
#include "ScriptedBot.generated.h"

class UHealthComponent;

/**
 * Training opponent (spec §4). Spawned once per arena and re-used: Activate() per episode, Deactivate() when unused.
 * In training a dead bot stays dead until the next Activate(). Build A: Static behaviour only (standing or crouched).
 */
UCLASS()
class FPSAGENT_API AScriptedBot : public ABeanCharacter
{
	GENERATED_BODY()

public:
	AScriptedBot(const FObjectInitializer& ObjectInitializer);

	/** New episode: teleport, face YawDeg, full health, alive, crouched or standing. Seed feeds the bot's rifle. */
	void Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed);
	/** Not in this episode: hidden, no collision, no movement. */
	void Deactivate();
	bool IsActive() const { return bActive; }
	bool IsAlive() const;

	virtual void PostInitializeComponents() override;

private:
	void EnsureDiedBound();
	void HandleDied(UHealthComponent* Victim, AActor* Killer);

	bool bActive = false;
	bool bDiedBound = false;
};
