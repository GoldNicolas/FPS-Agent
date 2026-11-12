#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpikeCProbe.generated.h"

class ABeanCharacter;
struct FShotResult;

/**
 * -Stage=SpikeC (request C-006): measures movement and rifle mechanics headless, in the packaged game, at the fixed
 * 60 Hz step used for training. One bean on an open floor:
 * - runs, walks and crouches in straight lines; the speed is averaged over the last second of each 3 s phase;
 * - holds the trigger: rounds/s over the first 20 shots;
 * - taps 8 times: shots per tap.
 * It logs measured vs configured values with a verdict, then exits 0. Pure measurement: no learning.
 */
UCLASS()
class FPSAGENT_API ASpikeCProbe : public AActor
{
	GENERATED_BODY()

public:
	ASpikeCProbe();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EPhase : uint8 { Settle, Run, Walk, Crouch, Stop, Hold, Taps, Done };

	void Enter(EPhase Next);
	void MoveAndMeasure(double PhaseTime, float& OutSpeed, EPhase Next);
	void OnShot(const FShotResult& Shot);
	void Finish();

	UPROPERTY() TObjectPtr<ABeanCharacter> Bean;

	EPhase Phase = EPhase::Settle;
	float TickSeconds = 1.f / 60.f;
	double PhaseStart = 0.0;
	double SpeedSum = 0.0;
	int32 SpeedSamples = 0;
	float RunSpeed = 0.f;
	float WalkSpeed = 0.f;
	float CrouchSpeed = 0.f;
	TArray<double> HoldShotTimes;
	int32 TapsRequested = 0;
	int32 TapShots = 0;
	double NextTapTime = 0.0;
	bool bTapHeld = false;
};
