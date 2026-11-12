#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Common/FPSAgentTypes.h"
#include "LearningAgentsPPOTrainer.h"
#include "ReachTaskActor.generated.h"

class ABeanCharacter;
class ULearningAgentsCritic;
class ULearningAgentsInteractor;
class ULearningAgentsManager;
class ULearningAgentsPolicy;
class ULearningAgentsTrainingEnvironment;
class UStaticMeshComponent;

/**
 * Spike A (-Stage=SpikeA): K walled arenas (-Arenas=K), one bean agent each, walking to random targets.
 * Training (default): connects to an EXTERNAL trainer only (-LearningAgentsUseExternalTrainingProcess, -LearningAgentsPort=);
 * it never spawns a local Python trainer (hard rule 6). -Infer (always on in the editor) plays the policy back.
 * -ResumeFrom=<dir> loads policy/critic/encoder/decoder.bin before starting.
 * Exit codes outside the editor: 0 = trainer finished, 1 = trainer connection/training failed, 2 = bad -ResumeFrom.
 */
UCLASS()
class FPSAGENT_API AReachTaskActor : public AActor
{
	GENERATED_BODY()

public:
	AReachTaskActor();

	/** Box around all arenas built for ArenaCount arenas whose floor top is at Origin.Z. */
	static FBox ComputeBounds(int32 ArenaCount, const FReachTaskSettings& S, const FVector& Origin);

	// ----- Called by UReachInteractor / UReachTrainingEnv (per Learning Agents agent id) -----
	FVector GetAgentLocation(int32 AgentId) const;
	FVector GetAgentVelocity(int32 AgentId) const;
	FVector GetTarget(int32 AgentId) const;
	/** The policy just observed this agent: remember the distance for the next step's progress reward. */
	void NoteDecision(int32 AgentId);
	void ApplyMove(int32 AgentId, float X, float Y);
	/** Reward for the step that just ran; also updates the reached flag and episode stats. */
	float TakeStepReward(int32 AgentId);
	bool IsAgentReached(int32 AgentId) const;
	/** New episode: seeded start and target, teleport, clear per-episode state. */
	void ResetAgent(int32 AgentId);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	struct FAgentState
	{
		int32 Arena = INDEX_NONE;
		FVector Target = FVector::ZeroVector;
		float DecisionDistance = 0.f;
		int32 Episode = 0;
		int32 Steps = 0;
		float Return = 0.f;
		bool bReached = false;
		/** Last move input, held between policy decisions (playback in the editor runs above 60 fps). */
		FVector LastMove = FVector::ZeroVector;
	};

	void BuildArenas();
	bool SetupLearning();
	bool LoadSnapshots(const FString& Dir);
	void SpawnAgents();
	ABeanCharacter* BeanOf(int32 AgentId) const;
	float Distance2D(int32 AgentId) const;
	void RecordEpisodeEnd(const FAgentState& A);
	void LogStats();
	void ExitWith(int32 Code, const TCHAR* Reason);

	UPROPERTY() TObjectPtr<ULearningAgentsManager> Manager;
	UPROPERTY() TObjectPtr<ULearningAgentsInteractor> Interactor;
	UPROPERTY() TObjectPtr<ULearningAgentsPolicy> Policy;
	UPROPERTY() TObjectPtr<ULearningAgentsCritic> Critic;
	UPROPERTY() TObjectPtr<ULearningAgentsTrainingEnvironment> Env;
	UPROPERTY() TObjectPtr<ULearningAgentsPPOTrainer> Trainer;
	/** By arena index. */
	UPROPERTY() TArray<TObjectPtr<ABeanCharacter>> Beans;
	/** Target markers by arena index (only when rendering). */
	UPROPERTY() TArray<TObjectPtr<UStaticMeshComponent>> Markers;

	FReachTaskSettings Settings;
	FLearningAgentsPPOTrainingSettings TrainingSettings;
	TArray<FVector> Centers;      // arena centers at agent-center height
	TArray<FAgentState> Agents;   // by agent id
	int32 BaseSeed = 0;
	float AgentCenterHeight = 0.f;
	/** One policy step = one fixed 60 Hz tick (Learning Agents' training default; Spike C checks it). */
	float StepSeconds = 1.f / 60.f;
	double InferClock = 0.0;
	bool bInfer = false;
	bool bReady = false;
	bool bTrainingStarted = false;
	bool bInferStepped = false;
	bool bExiting = false;

	int32 TotalEpisodes = 0;
	int32 WindowEpisodes = 0;
	int32 WindowReached = 0;
	double WindowReturn = 0.0;
	int64 WindowSteps = 0;
	double WindowWallStart = 0.0;
	double NextStatsTime = 0.0;
};
