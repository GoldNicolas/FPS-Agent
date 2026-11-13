#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Learning/CombatArena.h"
#include "Learning/CombatStats.h"
#include "Learning/Curriculum.h"
#include "CombatTaskActor.generated.h"

class ULearningAgentsManager;
class UTrainingSession;

/**
 * Combat stage S0 (spec 2026-11-12-combat-training-design; build A).
 * - Runs -Arenas=K arenas (one agent plus static bots each), with a decision every Combat.Ppo.TicksPerDecision fixed ticks (D-023).
 * - Each arena has its own automatic difficulty ramp (spec §5).
 * - Writes CombatStats lines and -StatsCsv rows.
 * - Modes: training against the external trainer (default); playback (-Infer, always in the editor); the reference agent
 *   (-Scripted); and the promotion test (-Eval -EvalEpisodes=N -EvalJson=<path>).
 * - Exit codes: 0 finished; 1 trainer failure; 2 bad arguments.
 */
UCLASS()
class FPSAGENT_API ACombatTaskActor : public AActor
{
	GENERATED_BODY()

public:
	ACombatTaskActor();

	/** Box around all arenas centred on Origin's arena grid. */
	static FBox ComputeBounds(int32 ArenaCount, const FVector& Origin, int32 StageIndex = 0);

	/** The agent bean of arena ArenaIndex, or null (viewing: fps.WatchAgent). */
	ABeanCharacter* GetArenaAgent(int32 ArenaIndex) const
	{
		return Arenas.IsValidIndex(ArenaIndex) && Arenas[ArenaIndex] ? Arenas[ArenaIndex]->GetAgent() : nullptr;
	}

	// ----- Called by UCombatInteractor / UCombatTrainingEnv (per Learning Agents agent id) -----
	void BuildObservation(int32 AgentId, FPSCombatObs::FObservationData& Out) const;
	void SetAction(int32 AgentId, const FPSCombatObs::FAgentAction& Action);
	float TakeDecisionReward(int32 AgentId);
	ECombatEpisodeEnd GetEpisodeEnd(int32 AgentId) const;
	/** Records the finished episode (if it really ended) and starts the next one. */
	void ResetAgentEpisode(int32 AgentId);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	UCombatArena* ArenaOf(int32 AgentId) const;
	void StartNextEpisode(int32 ArenaIndex);
	/** Starts the arena's next episode unless its -Eval quota is used up (then the arena goes idle). */
	bool TryStartNextEpisode(int32 ArenaIndex);
	/** Learning Agents agent id of the arena, or INDEX_NONE. */
	int32 AgentIdOf(int32 ArenaIndex) const;
	void RecordEpisode(int32 ArenaIndex, ECombatEpisodeEnd End);
	void Decide();
	void LogStats();
	void FinishEval();
	void Exit(int32 Code, const FString& Reason);
	double Now() const;
	FPSCombatStats::FLevelSummary LevelSummary() const;

	UPROPERTY() TObjectPtr<ULearningAgentsManager> Manager;
	UPROPERTY() TObjectPtr<UTrainingSession> Session;
	UPROPERTY() TArray<TObjectPtr<UCombatArena>> Arenas;

	/** Learning Agents agent id -> arena index. */
	TArray<int32> AgentToArena;
	TArray<FPSCurriculum::FDifficultyController> Difficulty;
	FPSCombatStats::FWindow Window;
	FPSCombatStats::FWindow EvalTotals;
	/** -Eval: episodes each arena may still start. The quotas sum to -EvalEpisodes, so slow episodes are never dropped. Empty outside -Eval. */
	TArray<int32> EvalStartsLeft;
	/** Arena finished its -Eval quota: no more decisions, ticks or records. */
	TArray<bool> ArenaIdle;
	double DecisionSeconds = 4.0 / 60.0;
	double NextDecisionTime = 0.0;
	double NextStatsTime = 0.0;
	double WindowWallStart = 0.0;
	int64 WindowDecisions = 0;
	bool bInfer = false;
	bool bScripted = false;
	bool bEval = false;
	/** Combat stage index: 0 = S0, 1 = S1 (set in BeginPlay). */
	int32 StageIndex = 0;
	bool bReady = false;
	bool bExiting = false;
	bool bCsvWarned = false;
};
