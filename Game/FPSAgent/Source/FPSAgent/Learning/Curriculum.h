#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"
#include "Arena/MapSpec.h"
#include "Bots/BotBrain.h"

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

	/** One bot of an episode, any stage. Offset: cm from the arena centre (world XY). */
	struct FBotSpawn
	{
		FVector2D Offset = FVector2D::ZeroVector;
		float YawDeg = 0.f;
		bool bCrouched = false;
		FPSBots::EBotBehaviour Behaviour = FPSBots::EBotBehaviour::Static;
		float SpeedScale = 1.f;
		FIntPoint PeekHidden = FIntPoint(-1, -1);
		FIntPoint PeekExposed = FIntPoint(-1, -1);
	};

	/** One episode of any combat stage: this episode's arena map (S1: with cover), the agent's spawn and the bots. */
	struct FCombatEpisode
	{
		float Level = 0.f;
		bool bReview = false;
		FMapSpec Map;
		/** Agent spawn: cm from the arena centre (world XY). */
		FVector2D AgentOffset = FVector2D::ZeroVector;
		float AgentYawDeg = 0.f;
		TArray<FBotSpawn> Bots;
	};

	/** An S0 episode on the arena's open floor: the agent at the centre; static bots facing it. */
	FPSAGENT_API FCombatEpisode FromS0(const FS0Episode& E, const FMapSpec& OpenFloor);

	/**
	 * S1 (spec §5, D-031):
	 * - cover from the stage's density knob (MakeCoverFloorSpec);
	 * - the agent on a random free tile with a random facing;
	 * - bots around it on free tiles (S0's distance and spread knobs), each facing away so the agent is outside its view cone at spawn;
	 * - behaviours from the stage mix, speed from the stage knob;
	 * - a peek bot moves to a hidden tile with an exposed neighbour, or strafes when there is none.
	 */
	FPSAGENT_API FCombatEpisode SampleS1(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, const FBotBehaviourSettings& B);
}
