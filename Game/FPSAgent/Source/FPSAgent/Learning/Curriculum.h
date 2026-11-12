#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Automatic difficulty ramp and episode sampling (spec §5). Pure: no world. */
namespace FPSCurriculum
{
	/** Per-arena difficulty level 0..1 from the clear rate of the last WindowEpisodes counted episodes. */
	struct FPSAGENT_API FDifficultyController
	{
		float Level = 0.f;
		TArray<bool> Window;

		void Init(float StartLevel);
		/** Records a counted (non-review) episode; returns the level change (+Step, -Step or 0). */
		float Record(bool bCleared, const FCurriculumSettings& C);
	};

	/** lerp(Easy, Hard, Level) +/- JitterFraction * |Hard - Easy|, clamped to [min(Easy, Hard), max(Easy, Hard)]. */
	FPSAGENT_API float Knob(FRandomStream& Rng, float Easy, float Hard, float Level, float JitterFraction);

	/** One S0 episode: the agent stands at the arena centre facing AgentYawDeg; bots around it. */
	struct FS0Episode
	{
		float Level = 0.f;
		bool bReview = false;
		float AgentYawDeg = 0.f;
		/** Bot positions relative to the agent (cm, world XY). */
		TArray<FVector2D> BotOffsets;
		TArray<bool> BotCrouched;
	};

	/**
	 * Samples an S0 episode. With bAllowReview, ReviewFraction of episodes are "review" episodes at a uniform random
	 * level in [0, ControllerLevel] (not counted by the ramp). Bot distances never exceed MaxRadiusCm.
	 */
	FPSAGENT_API FS0Episode SampleS0(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, float MaxRadiusCm);
}
