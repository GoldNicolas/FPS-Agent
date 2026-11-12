#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "LearningAgentsCritic.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsPPOTrainer.h"
#include "TrainingSession.generated.h"

class ULearningAgentsInteractor;
class ULearningAgentsManager;
class ULearningAgentsTrainingEnvironment;

/** What UTrainingSession::Setup builds (spec §1: one shared helper for Spike A and the combat stages). */
struct FTrainingSessionConfig
{
	/** Log prefix: "SpikeA" or "Combat". */
	FString LogPrefix;
	TSubclassOf<ULearningAgentsInteractor> InteractorClass;
	TSubclassOf<ULearningAgentsTrainingEnvironment> EnvClass;
	FLearningAgentsPolicySettings Policy;
	FLearningAgentsCriticSettings Critic;
	FLearningAgentsPPOTrainerSettings Trainer;
	FLearningAgentsPPOTrainingSettings Training;
	/** Reset every agent after each policy update. Spike A: true. Combat: false, so episodes and the ramp aren't cut (D-026). */
	bool bResetAgentsOnUpdate = true;
	/** Playback only: no critic, no trainer connection. */
	bool bInfer = false;
	int32 Seed = 0;
	/** -ResumeFrom directory; empty = fresh networks. */
	FString ResumeFrom;
};

/**
 * Shared Learning Agents plumbing: interactor, policy, critic, snapshot loading (-ResumeFrom), the EXTERNAL trainer
 * connection (never a local trainer: hard rule 6) with a wall-clock watchdog, the PPO trainer, and exit codes
 * (0 finished, 1 trainer failure, 2 bad -ResumeFrom).
 */
UCLASS()
class FPSAGENT_API UTrainingSession : public UObject
{
	GENERATED_BODY()

public:
	enum class EStatus : uint8 { Running, Finished, Failed };

	/** Creates everything; returns false after requesting an exit. Agents are added to the manager afterwards. */
	bool Setup(ULearningAgentsManager& Manager, const FTrainingSessionConfig& InConfig);
	/** Training mode, once per decision. Finished = the trainer reached its iteration count (never call again then). */
	EStatus RunTrainingStep();
	ULearningAgentsPolicy* GetPolicy() const { return Policy; }
	const FTrainingSessionConfig& GetConfig() const { return Config; }

	/** Logs "<Prefix>: exiting (code N): <Reason>" and, outside the editor, ends the process with that code. */
	static void ExitProcess(const FString& LogPrefix, int32 Code, const FString& Reason);

private:
	bool LoadSnapshots(const FString& Dir);

	UPROPERTY() TObjectPtr<ULearningAgentsInteractor> Interactor;
	UPROPERTY() TObjectPtr<ULearningAgentsPolicy> Policy;
	UPROPERTY() TObjectPtr<ULearningAgentsCritic> Critic;
	UPROPERTY() TObjectPtr<ULearningAgentsTrainingEnvironment> Env;
	UPROPERTY() TObjectPtr<ULearningAgentsPPOTrainer> Trainer;

	FTrainingSessionConfig Config;
	bool bTrainingStarted = false;
};
