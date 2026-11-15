#pragma once

#include "CoreMinimal.h"

/**
 * -Set=Path=Value;Path=Value on any UObject's UPROPERTYs (spec §6), e.g. "Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997".
 * Path = property names joined by '.', walking through struct properties. Numbers, bools (true/false/1/0) and anything
 * Unreal can import as text. Lets the cluster sweep reward weights and PPO knobs without a new build.
 */
namespace FPSSettingsOverride
{
	/** Applies the overrides in order; stops at the first error (unknown path, not a struct, unparsable value). */
	FPSAGENT_API bool Apply(UObject& Target, const FString& Spec, TArray<FString>& OutApplied, FString& OutError);
}
