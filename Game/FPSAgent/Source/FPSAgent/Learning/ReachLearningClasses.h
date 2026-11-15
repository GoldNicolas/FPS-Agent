#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "ReachLearningClasses.generated.h"

class AReachTaskActor;

/**
 * Spike A observations and actions. Thin: all state lives in AReachTaskActor (the Learning Agents layer only
 * translates between it and the trainer, so the RL stack could be swapped without touching gameplay).
 * Observation: target position relative to the agent + agent velocity (no other agents, hard rule 3/4).
 * Action: 2D move input (world X/Y), clamped to unit length.
 */
UCLASS()
class FPSAGENT_API UReachInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	virtual void SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement, ULearningAgentsObservationSchema* InObservationSchema) override;
	virtual void GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement, ULearningAgentsObservationObject* InObservationObject, const int32 AgentId) override;
	virtual void SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema) override;
	virtual void PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject, const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId) override;

	/** Observation normalization (toy values): positions in units of 10 m, velocities in units of 6 m/s. */
	static constexpr float TargetScaleCm = 1000.f;
	static constexpr float VelocityScaleCmPerS = 600.f;
};

/** Spike A rewards, completions and episode resets. Thin: forwards to AReachTaskActor. */
UCLASS()
class FPSAGENT_API UReachTrainingEnv : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;
};
