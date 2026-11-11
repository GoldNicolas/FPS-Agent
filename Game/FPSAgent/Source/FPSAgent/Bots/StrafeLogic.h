#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

struct FStrafeState
{
	float Direction = 1.f;
	/** < 0: first update schedules it. */
	double NextFlipTime = -1.0;
};

/** ADAD strafing: returns lateral input (+1 right / -1 left). Flips on a random schedule and when past the lane half-width. */
FPSAGENT_API float UpdateStrafe(FStrafeState& State, double Now, float LateralOffsetCm, const FRangeSettings& Range, const FRandomStream& Rng);
