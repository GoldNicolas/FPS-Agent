#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BeanPlayerController.generated.h"

class ABeanCharacter;
class ACameraActor;
class UInputAction;
class UInputMappingContext;
struct FInputActionValue;

/** Human play-testing controls, built in code (no input assets). WASD, mouse, LMB fire, Space, Ctrl crouch, Shift walk, R reload. */
UCLASS()
class FPSAGENT_API ABeanPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	/** Console: "ShowHitboxes 1" draws every bean's hitboxes (same as the fps.ShowHitboxes variable). */
	UFUNCTION(Exec)
	void ShowHitboxes(bool bShow);

	/** Console: "TopDownView 1" shows the whole arena from above, north up (same as the fps.TopDown variable). */
	UFUNCTION(Exec)
	void TopDownView(bool bOn);

	virtual void PlayerTick(float DeltaTime) override;

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	void BuildInput();
	void ApplyTopDown(bool bOn);
	void ApplyHitboxes(bool bShow);
	ABeanCharacter* GetBean() const;
	void OnMove(const FInputActionValue& Value);
	void OnLook(const FInputActionValue& Value);
	void OnFirePressed();
	void OnFireReleased();
	void OnJumpPressed();
	void OnJumpReleased();
	void OnCrouchPressed();
	void OnCrouchReleased();
	void OnWalkPressed();
	void OnWalkReleased();
	void OnReload();

	UPROPERTY() TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY() TObjectPtr<UInputAction> MoveAction;
	UPROPERTY() TObjectPtr<UInputAction> LookAction;
	UPROPERTY() TObjectPtr<UInputAction> FireAction;
	UPROPERTY() TObjectPtr<UInputAction> JumpAction;
	UPROPERTY() TObjectPtr<UInputAction> CrouchAction;
	UPROPERTY() TObjectPtr<UInputAction> WalkAction;
	UPROPERTY() TObjectPtr<UInputAction> ReloadAction;
	UPROPERTY() TObjectPtr<ACameraActor> TopDownCamera;
	bool bTopDownActive = false;
	bool bHitboxesShown = false;
};
