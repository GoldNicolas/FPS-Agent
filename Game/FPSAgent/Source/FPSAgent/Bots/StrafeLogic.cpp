#include "Bots/StrafeLogic.h"

float UpdateStrafe(FStrafeState& State, double Now, float LateralOffsetCm, const FRangeSettings& Range, const FRandomStream& Rng)
{
	auto Schedule = [&]() { State.NextFlipTime = Now + Rng.FRandRange(Range.StrafeMinPeriod, Range.StrafeMaxPeriod); };
	if (State.NextFlipTime < 0.0)
	{
		Schedule();
		return State.Direction;
	}
	const bool bPastRight = LateralOffsetCm > Range.StrafeHalfWidthCm && State.Direction > 0.f;
	const bool bPastLeft = LateralOffsetCm < -Range.StrafeHalfWidthCm && State.Direction < 0.f;
	if (bPastRight || bPastLeft || Now >= State.NextFlipTime)
	{
		State.Direction = -State.Direction;
		Schedule();
	}
	return State.Direction;
}
