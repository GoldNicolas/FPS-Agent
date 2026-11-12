#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Common/Perception.h"
#include "Learning/CombatObservation.h"
#include "Learning/CombatReward.h"
#include "Learning/CombatStats.h"
#include "Learning/Curriculum.h"
#include "Weapons/CombatMath.h"
#include "CombatArena.generated.h"

class ABeanCharacter;
class AScriptedBot;
class UHealthComponent;
struct FShotResult;

/** Where an episode stands right now: a pure function of state (D-022). A dead agent wins over a clear (spec §6). */
enum class ECombatEpisodeEnd : uint8 { Running, Died, Cleared, TimedOut };

/**
 * One combat arena (spec §1): a generated floor, one agent bean, a pool of scripted bots, the reward ledger and the
 * episode state. Gameplay only: no Learning Agents types (the RL layer is UCombatInteractor / UCombatTrainingEnv).
 * All "Now" arguments are world time in seconds.
 */
UCLASS()
class FPSAGENT_API UCombatArena : public UObject
{
	GENERATED_BODY()

public:
	/** Builds the S0 floor centred on Center (floor top at Center.Z), spawns the agent and the bot pool (inactive). */
	void Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed);
	/** Seed of the next episode: FPSReach::EpisodeSeed(-Seed, arena index, episode count) (determinism rule). */
	int32 NextEpisodeSeed() const;
	/** Places the agent and bots for a sampled episode and resets every per-episode counter. */
	void StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now);
	/** Every tick: the time penalty, plus the held action (aim step, movement). */
	void TickAction(float DeltaSeconds);
	/** Once per decision, before completion, reward and observation: perception, first sightings, clear bonus, aim helper. */
	void PrepareDecision(double Now);
	ECombatEpisodeEnd GetEpisodeEnd(double Now) const;
	/** The reward of the decision that just ended (all terms since the previous decision). */
	float TakeDecisionReward() { return Ledger.TakeDecision(); }
	FPSCombatStats::FEpisodeRecord FinishEpisode(ECombatEpisodeEnd End, double Now) const;
	void BuildObservation(FPSCombatObs::FObservationData& Out, double Now) const;
	/** The action to hold until the next decision (buttons apply now). */
	void SetAction(const FPSCombatObs::FAgentAction& Action);
	/** -Scripted reference action for this decision. */
	FPSCombatObs::FAgentAction ScriptedAction(float DecisionSeconds) const;

	ABeanCharacter* GetAgent() const { return Agent; }
	AScriptedBot* GetBot(int32 Index) const { return Bots.IsValidIndex(Index) ? Bots[Index].Get() : nullptr; }
	int32 GetBotCount() const { return Bots.Num(); }
	const FPSCombatReward::FLedger& GetLedger() const { return Ledger; }
	const FVector& GetCenter() const { return Center; }
	/** Bots may stand within this radius of the centre (floor half-size minus a margin). */
	float GetBotRadiusCm() const;
	/** Half the side of the S0 floor including its wall ring. */
	static float FloorHalfSizeCm();

	virtual UWorld* GetWorld() const override { return WorldPtr.Get(); }

private:
	void Sense(TArray<FPSPerception::FSighting>& Out) const;
	float NearestVisibleAimErrorDeg(const TArray<FPSPerception::FSighting>& Sightings) const;
	bool IsAgentDead() const;
	bool AllActiveBotsDead() const;
	float TimeLeftFraction(double Now) const;
	int32 BotIndexOf(const AActor* Actor) const;
	void OnAgentShot(const FShotResult& Shot);
	void OnAgentDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator);
	void OnAgentDied(UHealthComponent* Victim, AActor* Killer);
	void OnBotDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator);
	void OnBotDied(UHealthComponent* Victim, AActor* Killer);

	UPROPERTY() TObjectPtr<ABeanCharacter> Agent;
	UPROPERTY() TArray<TObjectPtr<AScriptedBot>> Bots;
	TWeakObjectPtr<UWorld> WorldPtr;

	int32 ArenaIndex = 0;
	int32 BaseSeed = 0;
	int32 EpisodeCount = 0;
	FVector Center = FVector::ZeroVector;
	FPSCombatReward::FLedger Ledger;
	TArray<FPSPerception::FTrack> Tracks;
	FPSCombatObs::FAgentAction Held;
	double EpisodeStart = 0.0;
	double LastHitTime = -1.0e9;
	FVector LastHitDirection = FVector::ZeroVector;
	bool bReview = false;
	float Level = 0.f;
	int32 ActiveBots = 0;
	int32 Kills = 0;
	int32 Shots = 0;
	int32 Hits = 0;
	int32 Headshots = 0;
};
