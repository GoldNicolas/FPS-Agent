#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Arena/MapSpec.h"
#include "Arena/TileGraph.h"
#include "Bots/BotBrain.h"
#include "Bots/BotSenses.h"
#include "Common/Perception.h"
#include "Learning/CombatObservation.h"
#include "Learning/CombatReward.h"
#include "Learning/CombatStats.h"
#include "Learning/Curriculum.h"
#include "Weapons/CombatMath.h"
#include "CombatArena.generated.h"

class ABeanCharacter;
class AStaticMeshActor;
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
	void Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed, int32 InStageIndex = 0);
	/** Seed of the next episode: FPSReach::EpisodeSeed(-Seed, arena index, episode count) (determinism rule). */
	int32 NextEpisodeSeed() const;
	/** Places the agent and bots for a sampled episode and resets every per-episode counter. */
	void StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now);
	/** Places the agent, bots and (S1+) cover for an episode of any stage, and resets every per-episode counter. */
	void StartEpisode(const FPSCurriculum::FCombatEpisode& Config, int32 Seed, double Now);
	const FMapSpec& GetBaseSpec() const { return BaseSpec; }
	int32 GetStageIndex() const { return StageIndex; }
	const FPSBots::FBotBrain& GetBotBrain(int32 Index) const { return Brains[Index]; }
	const TArray<FPSBots::FSoundEvent>& GetSounds() const { return Sounds; }
	int32 GetVisibleCoverCount() const { return VisibleCover; }
	/** Adds a sound to this arena's buffer (bots and the agent hear only their own arena: hard rule 3). */
	void EmitSound(const FPSBots::FSoundEvent& Event) { AddSound(Event); }
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
	FPSCombatObs::FAgentAction ScriptedAction(float DecisionSeconds);

	ABeanCharacter* GetAgent() const { return Agent; }
	AScriptedBot* GetBot(int32 Index) const { return Bots.IsValidIndex(Index) ? Bots[Index].Get() : nullptr; }
	int32 GetBotCount() const { return Bots.Num(); }
	const FPSCombatReward::FLedger& GetLedger() const { return Ledger; }
	const FVector& GetCenter() const { return Center; }
	/** Bots may stand within this radius of the centre (floor half-size minus a margin). */
	float GetBotRadiusCm() const;
	/** Half the side of the S0 floor including its wall ring. */
	static float FloorHalfSizeCm(int32 InStageIndex = 0);

	virtual UWorld* GetWorld() const override { return WorldPtr.Get(); }

private:
	const FCombatStageSpec& StageSpec() const;
	FVector2D ToLocal(const FVector& World) const;
	void ApplyCover(const FMapSpec& Map);
	void TickBots(double Now, float DeltaSeconds);
	void EmitFootsteps(double Now);
	/** The only way into Sounds: draws the agent's heard-position noise (HeardOffset) from SoundRng. */
	void AddSound(FPSBots::FSoundEvent E);
	void SenseBots(double Now);
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

	UPROPERTY() TArray<TObjectPtr<AStaticMeshActor>> CoverPool;
	int32 StageIndex = 0;
	int32 VisibleCover = 0;
	FMapSpec BaseSpec;
	FMapSpec CurrentMap;
	FPSNav::FTileGraph Graph;
	FVector MapOrigin = FVector::ZeroVector;
	TArray<FPSBots::FBotBrain> Brains;
	TArray<FPSBots::FBotSenses> Senses;
	TArray<FRandomStream> BotRngs;
	TArray<FPSBots::FSoundEvent> Sounds;
	FRandomStream SoundRng;
	double BotSpeedSum = 0.0;
	int64 BotSpeedSamples = 0;
	/** Next footstep time per source: [0] = the agent, [i + 1] = bot i. */
	TArray<double> NextFootstep;
	/** The -Scripted reference's explore brain (S1+). The policy never reads it. */
	FPSBots::FBotBrain AgentBrain;
	double LastScriptedSoundTime = -1.0e9;
};
