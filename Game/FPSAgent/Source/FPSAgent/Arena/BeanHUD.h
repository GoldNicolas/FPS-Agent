#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BeanHUD.generated.h"

class ABeanCharacter;
struct FShotResult;

/** Play-testing HUD: crosshair (gap grows with spread), HP/armor/ammo/speed/spread, hit markers. */
UCLASS()
class FPSAGENT_API ABeanHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
	void HandleShot(const FShotResult& Shot);

	TWeakObjectPtr<ABeanCharacter> BoundBean;
	FString LastHitText;
	double LastHitTime = -100.0;
};
