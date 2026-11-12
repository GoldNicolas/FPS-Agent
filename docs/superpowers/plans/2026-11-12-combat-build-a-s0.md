# Combat Build A (S0 aim gallery) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a Linux build where `-Stage=S0` trains the combat agent against static scripted bots on generated open floors. It has the agreed observation/action layout, every reward term (including the clear bonus and the aim helper), the per-arena automatic difficulty ramp, per-term stats, `-Eval`, `-Scripted`, `-Set=`, `-StartDifficulty` and `-StatsCsv`.

**Architecture:**
- Gameplay logic lives in small pure modules that are unit-tested without a world: reward ledger, curriculum, perception math, observation packing, stats. A `UCombatArena` UObject per arena glues them to the beans.
- A thin Learning Agents layer (`UCombatInteractor`, `UCombatTrainingEnv`) translates between the arena data and the trainer.
- `ACombatTaskActor` runs K arenas, a decision every 4 fixed ticks, the ramp, stats, eval and the shared `UTrainingSession`. That session helper is extracted from Spike A's `AReachTaskActor`.

**Tech Stack:** Unreal Engine 5.7.4 C++ (module `FPSAgent`), Learning Agents 5.7 (PPO, external socket trainer), Unreal automation tests, PowerShell build and test scripts.

**Spec:** [`docs/superpowers/specs/2026-11-12-combat-training-design.md`](../specs/2026-11-12-combat-training-design.md) (approved 2026-11-12). Research behind it: [`docs/research/rl-reward-and-optimization.md`](../../research/rl-reward-and-optimization.md).

## Global Constraints
- UE **5.7.4**; verify Learning Agents names against the 5.7 headers (`C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Experimental\LearningAgents\Source`).
- **C++ only** for logic. Includes are relative to the module root (`"Common/FPSAgentSettings.h"`). `FVector`/`FVector2D`/`FRotator` are double: cast to float in `TestEqual`.
- **Everything works under `-nullrhi -nosound`** (hard rule 2). No rendering-dependent code.
- **Fair observations** (hard rule 4 + D-024): enemies only via line of sight inside the 103° FOV, or a ≤ 2 s memory; plus the hit indicator and hearing (hearing is empty in S0). **Rays and line-of-sight traces hit level geometry only**: object-type queries on WorldStatic + WorldDynamic, never pawns.
- **Agents train only against scripted bots; arenas never interact** (hard rule 3).
- **Mechanics are data** (hard rule 7): every reward, PPO, perception and stage value lives in `UFPSAgentSettings` (`Combat`, `Curriculum`), each with a source or `verify` note.
- **Determinism:** all episode randomness comes from `FPSReach::EpisodeSeed(-Seed, arena index, episode count)`.
- **D-022:** episode end is a pure function of state; one-off rewards are paid at most once. **D-023:** 15 decisions per second (4 ticks), discount 0.995, one observation/action layout for all stages.
- **No local training** (hard rule 6). Local checks are tests, headless `-Scripted`/`-Infer`/`-Eval` runs, and a no-trainer exit check.
- **Build and test:** editor closed; `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 [-Filter <prefix>]`.
- **Commits:** prefix `[local]`, small and focused; `git pull --rebase` then `git push` after each task. Never force-push. Document before and after each step (user rule).
- **Exit codes:** `0` = finished · `1` = trainer unreachable or failed · `2` = bad arguments (`-ResumeFrom`, `-Set`, a stage not in this build, `-Eval` without snapshots).

## File structure

| File | Status | Responsibility |
|---|---|---|
| `Common/FPSAgentTypes.h` | modify | New settings structs: `FCombatRewardSettings`, `FCombatPpoSettings`, `FCombatPerceptionSettings`, `FCombatStageSpec`, `FCurriculumSettings`, `FCombatSettings` |
| `Common/FPSAgentSettings.h` | modify | `Combat`, `Curriculum` config properties |
| `Common/FPSRunArgs.h/.cpp` | modify | `-Stage` (no default), `-StartDifficulty`, `-Eval`, `-EvalEpisodes`, `-EvalJson`, `-StatsCsv`, `-Set` |
| `Common/SettingsOverride.h/.cpp` | create | `-Set=Path=Value;...` via reflection |
| `Common/Perception.h/.cpp` | create | Aim error, FOV, view frame, ray directions, enemy tracks; geometry-only line of sight and rays |
| `Arena/FloorGenerator.h/.cpp` | create | Open-floor `FMapSpec` (S0) |
| `Arena/ArenaGameMode.h/.cpp` | modify | Apply `-Set`; dispatch `-Stage=S0..S3` to `ACombatTaskActor` |
| `Character/BeanAIController.h/.cpp` | create | AI controller that keeps the aim it is given |
| `Character/BeanCharacter.h` | modify | `IsWalking()` accessor |
| `Bots/ScriptedBot.h/.cpp` | create | Training bot (build A: Static), activate/deactivate per episode |
| `Learning/CombatReward.h/.cpp` | create | Reward terms + per-episode ledger |
| `Learning/Curriculum.h/.cpp` | create | Difficulty controller + S0 episode sampler |
| `Learning/CombatObservation.h/.cpp` | create | Observation packing, action sanitizing, aim per tick, scripted reference action |
| `Learning/CombatStats.h/.cpp` | create | Episode records, stats window, log line, CSV, eval JSON |
| `Learning/CombatArena.h/.cpp` | create | One arena: floor, agent, bots, events → ledger, episode state, observation |
| `Learning/TrainingSession.h/.cpp` | create | Shared Learning Agents plumbing (from `AReachTaskActor`) |
| `Learning/ReachTaskActor.h/.cpp` | modify | Use `UTrainingSession` |
| `Learning/CombatLearningClasses.h/.cpp` | create | `UCombatInteractor`, `UCombatTrainingEnv` |
| `Learning/CombatTaskActor.h/.cpp` | create | K arenas, decision cadence, ramp, stats, eval, scripted |
| `Eval/SpikeCProbe.h/.cpp` | create | `-Stage=SpikeC` headless mechanics probe (C-006) |
| `Tests/*.cpp` | create/modify | One test file per module (named in each task) |
| `README.md`, `docs/decisions.md`, spec, `docs/builds.md`, `docs/STATUS.md` | modify | Docs before and after (Task 13) |

All C++ paths below are relative to `Game/FPSAgent/Source/FPSAgent/`.

---

### Task 0: Document the start (before any code)

**Files:** Modify `docs/STATUS.md` (LOCAL focus line only).

- [ ] **Step 1: Point STATUS at this plan.** In `docs/STATUS.md` → *Current focus* → LOCAL, replace the line that starts with `  - **Combat training design APPROVED by the user (2026-11-12)**` with:

```markdown
  - **Combat training design APPROVED by the user (2026-11-12)**: [`superpowers/specs/2026-11-12-combat-training-design.md`](superpowers/specs/2026-11-12-combat-training-design.md). **Now implementing build A (S0 aim gallery)** following [`superpowers/plans/2026-11-12-combat-build-a-s0.md`](superpowers/plans/2026-11-12-combat-build-a-s0.md). The cluster gets it as a new build plus request L-007.
```

- [ ] **Step 2: Commit and push**

```bash
git add docs/STATUS.md docs/superpowers/plans/2026-11-12-combat-build-a-s0.md
git commit -m "[local] Build A (S0) implementation plan; STATUS: implementing build A"
git pull --rebase && git push
```

---

### Task 1: Combat settings and new command-line arguments

**Files:**
- Modify: `Common/FPSAgentTypes.h` (append after `FReachTaskSettings`)
- Modify: `Common/FPSAgentSettings.h:25` (after `SpikeA`)
- Modify: `Common/FPSRunArgs.h`, `Common/FPSRunArgs.cpp`
- Test: `Tests/SettingsTests.cpp`

**Interfaces:**
- Produces:
  - `UFPSAgentSettings::Get().Combat` (`FCombatSettings`: `Reward`, `Ppo`, `Perception`, `ArenaSpacingCm`) and `.Curriculum` (`FCurriculumSettings`: ramp fields + `S0` `FCombatStageSpec`).
  - `FFPSRunArgs` fields `Stage` (default empty), `StartDifficulty`, `bEval`, `EvalEpisodes`, `EvalJson`, `StatsCsv`, `SetOverrides`.
  - `int32 FFPSRunArgs::CombatStageIndex() const`, `static int32 FFPSRunArgs::CombatStageIndexOf(const FString&)`, `bool IsCombatStage() const`.

- [ ] **Step 1: Write the failing tests.** In `Tests/SettingsTests.cpp`:
  - Add `#include "LearningAgentsPPOTrainer.h"` below the existing includes (the batch-setting defaults are compared with Learning Agents' own).
  - In `FFPSSettingsDefaultsTest::RunTest`, before `return true;`, add:

```cpp
	const FCombatSettings& C = S.Combat;
	TestEqual(TEXT("15 decisions per second (D-023)"), 60 / C.Ppo.TicksPerDecision, 15);
	TestTrue(TEXT("discount in the agreed band"), C.Ppo.Discount >= 0.99f && C.Ppo.Discount <= 0.997f);
	TestEqual(TEXT("72 rays"), C.Perception.RayYawCount * C.Perception.RayPitchesDeg.Num(), 72);
	TestEqual(TEXT("clear bonus max 2.0"), C.Reward.ClearBase + C.Reward.ClearSpeed, 2.f);
	const FLearningAgentsPPOTrainingSettings LaTraining;
	const FLearningAgentsPPOTrainerSettings LaTrainer;
	TestEqual(TEXT("iterations per gather = Learning Agents default"), C.Ppo.IterationsPerGather, LaTraining.IterationsPerGather);
	TestEqual(TEXT("policy batch = Learning Agents default"), C.Ppo.PolicyBatchSize, LaTraining.PolicyBatchSize);
	TestEqual(TEXT("critic batch = Learning Agents default"), C.Ppo.CriticBatchSize, LaTraining.CriticBatchSize);
	TestEqual(TEXT("replay steps = Learning Agents default"), C.Ppo.ReplayMaxSteps, LaTrainer.MaximumRecordedStepsPerIteration);
	TestEqual(TEXT("replay episodes = Learning Agents default"), C.Ppo.ReplayMaxEpisodes, LaTrainer.MaximumRecordedEpisodesPerIteration);
	TestEqual(TEXT("S0 time limit 20 s"), S.Curriculum.S0.TimeLimitSeconds, 20.f);
	TestFalse(TEXT("S0 agent can't move"), S.Curriculum.S0.bAgentCanMove);
	TestTrue(TEXT("arenas at least floor + 50 m apart"),
		C.ArenaSpacingCm >= (S.Curriculum.S0.FloorTiles + 2) * 400.f + 5000.f);
```

  - In `FFPSRunArgsTest::RunTest`, replace `TestEqual(TEXT("default stage"), D.Stage, FString(TEXT("S0")));` with `TestTrue(TEXT("no stage by default"), D.Stage.IsEmpty());`.
  - Then, before `return true;`, add:

```cpp
	const FFPSRunArgs Cb = FFPSRunArgs::Parse(TEXT("-Stage=S0 -StartDifficulty=0.6 -Eval -EvalEpisodes=50 -EvalJson=/r/eval.json -StatsCsv=/r/stats.csv -Set=\"Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997\""));
	TestEqual(TEXT("combat stage index"), Cb.CombatStageIndex(), 0);
	TestTrue(TEXT("combat stage"), Cb.IsCombatStage());
	TestEqual(TEXT("start difficulty"), Cb.StartDifficulty, 0.6f);
	TestTrue(TEXT("eval"), Cb.bEval);
	TestTrue(TEXT("eval implies infer"), Cb.bInfer);
	TestEqual(TEXT("eval episodes"), Cb.EvalEpisodes, 50);
	TestEqual(TEXT("eval json"), Cb.EvalJson, FString(TEXT("/r/eval.json")));
	TestEqual(TEXT("stats csv"), Cb.StatsCsv, FString(TEXT("/r/stats.csv")));
	TestEqual(TEXT("-Set keeps ; and ="), Cb.SetOverrides, FString(TEXT("Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997")));
	TestEqual(TEXT("s3 is stage 3"), FFPSRunArgs::CombatStageIndexOf(TEXT("s3")), 3);
	TestEqual(TEXT("SpikeA is not combat"), FFPSRunArgs::CombatStageIndexOf(TEXT("SpikeA")), -1);
	TestEqual(TEXT("S4 is not a stage"), FFPSRunArgs::CombatStageIndexOf(TEXT("S4")), -1);
	TestFalse(TEXT("no stage: not combat"), D.IsCombatStage());
	TestEqual(TEXT("difficulty clamped"), FFPSRunArgs::Parse(TEXT("-StartDifficulty=3")).StartDifficulty, 1.f);
	TestEqual(TEXT("eval episodes default"), D.EvalEpisodes, 200);
	TestFalse(TEXT("eval off by default"), D.bEval);
```

- [ ] **Step 2: Run the build to verify it fails.**
  - Run: `powershell -File tools/build.ps1`
  - Expected: compile errors such as `'Combat': is not a member of 'UFPSAgentSettings'` and `'CombatStageIndex': is not a member of 'FFPSRunArgs'`.

- [ ] **Step 3: Add the settings structs.** Append to `Common/FPSAgentTypes.h` (end of file):

```cpp
/**
 * Combat reward weights (spec 2026-11-12-combat-training-design §3). Design values agreed with the user, no external
 * source: tune by sweeps (-Set=Combat.Reward.<Field>=<value>).
 */
USTRUCT(BlueprintType)
struct FCombatRewardSettings
{
	GENERATED_BODY()
	/** Damage is divided by this: full health + armor (100 + 50), so a kill from full is worth 1.0. */
	UPROPERTY(EditAnywhere, Category="Reward") float DamageNormalizer = 150.f;
	/** Multiplies damage dealt / DamageNormalizer (health + armor actually removed). */
	UPROPERTY(EditAnywhere, Category="Reward") float DamageDealtWeight = 1.f;
	UPROPERTY(EditAnywhere, Category="Reward") float Kill = 1.f;
	/** Once per bot per episode, on the first decision it passes the line-of-sight-in-FOV check. */
	UPROPERTY(EditAnywhere, Category="Reward") float FirstSighting = 0.2f;
	/** Clear bonus = ClearBase + ClearSpeed * (time left / time limit): once, when every bot is dead and the agent lives. */
	UPROPERTY(EditAnywhere, Category="Reward") float ClearBase = 0.5f;
	UPROPERTY(EditAnywhere, Category="Reward") float ClearSpeed = 1.5f;
	/** The aim-helper potential reaches 0 at this aim error (degrees). Its weight is per stage (FCombatStageSpec). */
	UPROPERTY(EditAnywhere, Category="Reward") float AimHelperRangeDeg = 30.f;
	/** Multiplies -(damage taken / DamageNormalizer). */
	UPROPERTY(EditAnywhere, Category="Reward") float DamageTakenWeight = 1.f;
	UPROPERTY(EditAnywhere, Category="Reward") float Death = -1.f;
	/** Per fired bullet that hits no enemy. */
	UPROPERTY(EditAnywhere, Category="Reward") float MissPerBullet = -0.01f;
	/** Per second of episode time (anti-camping). */
	UPROPERTY(EditAnywhere, Category="Reward") float TimePerSecond = -0.01f;
};

/** Trainer and network settings for combat stages (D-023). The game sends them to the trainer; sweep with -Set=Combat.Ppo.*. */
USTRUCT(BlueprintType)
struct FCombatPpoSettings
{
	GENERATED_BODY()
	/** A decision every N fixed 60 Hz ticks: 4 = 15 decisions per second (D-023). */
	UPROPERTY(EditAnywhere, Category="PPO") int32 TicksPerDecision = 4;
	/** 0.995 at 15 Hz looks about 13 s ahead (research note §2). Sweep 0.99-0.997. */
	UPROPERTY(EditAnywhere, Category="PPO") float Discount = 0.995f;
	UPROPERTY(EditAnywhere, Category="PPO") float GaeLambda = 0.95f;
	/** Learning Agents default 0; sweep 0 / 0.001 / 0.01 (research note §5). */
	UPROPERTY(EditAnywhere, Category="PPO") float EntropyWeight = 0.f;
	UPROPERTY(EditAnywhere, Category="PPO") float LearningRatePolicy = 0.0001f;
	UPROPERTY(EditAnywhere, Category="PPO") float LearningRateCritic = 0.001f;
	/** Gradient work per update (Learning Agents defaults). Scale with the number of games via -Set=Combat.Ppo.* (C-007, Spike B). */
	UPROPERTY(EditAnywhere, Category="PPO") int32 IterationsPerGather = 32;
	UPROPERTY(EditAnywhere, Category="PPO") int32 PolicyBatchSize = 1024;
	UPROPERTY(EditAnywhere, Category="PPO") int32 CriticBatchSize = 4096;
	/** Experience per update: the replay buffer is sent when either limit is reached (Learning Agents defaults; C-007). */
	UPROPERTY(EditAnywhere, Category="PPO") int32 ReplayMaxSteps = 10000;
	UPROPERTY(EditAnywhere, Category="PPO") int32 ReplayMaxEpisodes = 1000;
	/** Policy and critic: HiddenLayers x HiddenSize; policy memory MemorySize. Changing them breaks resume from older snapshots. */
	UPROPERTY(EditAnywhere, Category="PPO") int32 HiddenLayers = 2;
	UPROPERTY(EditAnywhere, Category="PPO") int32 HiddenSize = 256;
	UPROPERTY(EditAnywhere, Category="PPO") int32 MemorySize = 128;
	/** Trainer snapshot interval in gradient iterations (README -> Snapshot timing). */
	UPROPERTY(EditAnywhere, Category="PPO") int32 IterationsPerSnapshot = 10;
	/** Run length when neither -Batches nor -Iterations is given (gradient iterations). */
	UPROPERTY(EditAnywhere, Category="PPO") int32 MaxIterations = 1000000;
	UPROPERTY(EditAnywhere, Category="PPO") bool bTrainOnGpu = true;
	UPROPERTY(EditAnywhere, Category="PPO") bool bUseTensorboard = true;
	/** Game-side stats line (and -StatsCsv row) every N seconds of game time. */
	UPROPERTY(EditAnywhere, Category="PPO") float StatsLogIntervalSeconds = 10.f;
};

/** What the agent perceives and how fast it turns (spec §2). Changing ray or set sizes changes the network layout. */
USTRUCT(BlueprintType)
struct FCombatPerceptionSettings
{
	GENERATED_BODY()
	/** Horizontal field of view for the line-of-sight check (README: 103°, verify). */
	UPROPERTY(EditAnywhere, Category="Perception") float FieldOfViewDeg = 103.f;
	/** Enemies seen within this many seconds stay in the observation as "remembered" (spec §2). */
	UPROPERTY(EditAnywhere, Category="Perception") float MemorySeconds = 2.f;
	UPROPERTY(EditAnywhere, Category="Perception") int32 MaxEnemies = 5;
	UPROPERTY(EditAnywhere, Category="Perception") int32 MaxSounds = 4;
	/** Ray ring from eye height: RayYawCount directions x RayPitchesDeg (degrees, + = up). Level geometry only. */
	UPROPERTY(EditAnywhere, Category="Perception") int32 RayYawCount = 24;
	UPROPERTY(EditAnywhere, Category="Perception") TArray<float> RayPitchesDeg = { -20.f, 0.f, 15.f };
	UPROPERTY(EditAnywhere, Category="Perception") float RayRangeCm = 3000.f;
	/** Observation scales: positions and distances / PositionScaleCm, velocities / VelocityScaleCmPerS, angles / 180°. */
	UPROPERTY(EditAnywhere, Category="Perception") float PositionScaleCm = 3000.f;
	UPROPERTY(EditAnywhere, Category="Perception") float VelocityScaleCmPerS = 600.f;
	/** The hit indicator shows the direction of damage for this long (D-024). */
	UPROPERTY(EditAnywhere, Category="Perception") float HitIndicatorSeconds = 1.f;
	/** Aim turn-rate cap (human-likeness, verify): 720°/s = 48° per decision at 15 Hz. */
	UPROPERTY(EditAnywhere, Category="Perception") float MaxTurnRateDegPerSec = 720.f;
	UPROPERTY(EditAnywhere, Category="Perception") float MaxPitchDeg = 80.f;
};

/** One curriculum stage (spec §5): each knob has an easy (difficulty 0) and a hard (difficulty 1) value. Build A: S0. */
USTRUCT(BlueprintType)
struct FCombatStageSpec
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Stage") float TimeLimitSeconds = 20.f;
	/** Aim-helper potential weight W (spec §3): 0.2 in S0/S1, 0.1 in S2, 0 in S3. */
	UPROPERTY(EditAnywhere, Category="Stage") float AimHelperWeight = 0.2f;
	/** S0: false (movement, crouch, walk and jump actions are ignored). */
	UPROPERTY(EditAnywhere, Category="Stage") bool bAgentCanMove = false;
	/** Open floor side in 4 m tiles (20 = 80 m), inside a one-tile wall ring. */
	UPROPERTY(EditAnywhere, Category="Stage") int32 FloorTiles = 20;
	UPROPERTY(EditAnywhere, Category="Stage") int32 BotsEasy = 1;
	UPROPERTY(EditAnywhere, Category="Stage") int32 BotsHard = 5;
	UPROPERTY(EditAnywhere, Category="Stage") float BotDistanceMinCm = 500.f;
	UPROPERTY(EditAnywhere, Category="Stage") float BotDistanceMaxCmEasy = 1500.f;
	UPROPERTY(EditAnywhere, Category="Stage") float BotDistanceMaxCmHard = 3500.f;
	/** Bots stand within ± this many degrees of the agent's facing (180 = all around). */
	UPROPERTY(EditAnywhere, Category="Stage") float BotSpreadDegEasy = 45.f;
	UPROPERTY(EditAnywhere, Category="Stage") float BotSpreadDegHard = 180.f;
	UPROPERTY(EditAnywhere, Category="Stage") float CrouchedFractionEasy = 0.f;
	UPROPERTY(EditAnywhere, Category="Stage") float CrouchedFractionHard = 0.3f;
	/** Promotion gate: the -Eval clear rate at difficulty 1 (spec §5). */
	UPROPERTY(EditAnywhere, Category="Stage") float PromotionClearRate = 0.7f;
};

/** The automatic difficulty ramp (spec §5) and the stage table. */
USTRUCT(BlueprintType)
struct FCurriculumSettings
{
	GENERATED_BODY()
	/** Counted (non-review) episodes per rolling window. */
	UPROPERTY(EditAnywhere, Category="Curriculum") int32 WindowEpisodes = 20;
	UPROPERTY(EditAnywhere, Category="Curriculum") float RaiseAboveClearRate = 0.7f;
	UPROPERTY(EditAnywhere, Category="Curriculum") float LowerBelowClearRate = 0.4f;
	UPROPERTY(EditAnywhere, Category="Curriculum") float Step = 0.05f;
	/** Fraction of episodes sampled at a random easier level (anti-forgetting). They are not counted by the ramp. */
	UPROPERTY(EditAnywhere, Category="Curriculum") float ReviewFraction = 0.2f;
	/** Knob jitter as a fraction of the easy-hard range. */
	UPROPERTY(EditAnywhere, Category="Curriculum") float JitterFraction = 0.1f;
	UPROPERTY(EditAnywhere, Category="Curriculum") FCombatStageSpec S0;
};

/** Combat stages (S0-S3): rewards, trainer, perception, arena spacing. */
USTRUCT(BlueprintType)
struct FCombatSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Combat") FCombatRewardSettings Reward;
	UPROPERTY(EditAnywhere, Category="Combat") FCombatPpoSettings Ppo;
	UPROPERTY(EditAnywhere, Category="Combat") FCombatPerceptionSettings Perception;
	/** Distance between arena centres: more than the largest arena plus 50 m (spec §5). Arenas never interact anyway. */
	UPROPERTY(EditAnywhere, Category="Combat") float ArenaSpacingCm = 15000.f;
};
```

- [ ] **Step 4: Expose them in the settings class.** In `Common/FPSAgentSettings.h`, after the `SpikeA` line, add:

```cpp
	UPROPERTY(Config, EditAnywhere, Category="Learning") FCombatSettings Combat;
	UPROPERTY(Config, EditAnywhere, Category="Learning") FCurriculumSettings Curriculum;
```

- [ ] **Step 5: Add the arguments.** In `Common/FPSRunArgs.h`:
  - Replace the `Stage` member and its comment with:

```cpp
	/** -Stage=S0..S3 (combat curriculum) or SpikeA (toy task). Empty = not given: the game builds the play arena. */
	FString Stage;
```

  - After the `Batches` member, add:

```cpp
	/** -StartDifficulty=X: initial difficulty level of every arena, 0..1 (chained jobs pass the last logged mean). */
	float StartDifficulty = 0.f;
	/** -Eval (implies -Infer): promotion test. Fixed seeds, no training, no ramp, mean actions; writes -EvalJson and exits 0. */
	bool bEval = false;
	/** -EvalEpisodes=N: episodes to play in -Eval (default 200). */
	int32 EvalEpisodes = 200;
	/** -EvalJson=<path>: where -Eval writes its summary. */
	FString EvalJson;
	/** -StatsCsv=<path>: one row per stats window (every reward term, metrics, ramp level). */
	FString StatsCsv;
	/** -Set=Path=Value;...: settings overrides applied at startup (FPSSettingsOverride). */
	FString SetOverrides;
```

  - After `IsSpikeA()`, add:

```cpp
	/** 0..3 for S0..S3 (case-insensitive), else -1. */
	static int32 CombatStageIndexOf(const FString& InStage);
	int32 CombatStageIndex() const { return CombatStageIndexOf(Stage); }
	bool IsCombatStage() const { return CombatStageIndex() >= 0; }
```

- [ ] **Step 6: Parse them.** In `Common/FPSRunArgs.cpp`:
  - In `Parse`, after `FParse::Value(CommandLine, TEXT("-Batches="), Args.Batches);`, add:

```cpp
	FParse::Value(CommandLine, TEXT("-StartDifficulty="), Args.StartDifficulty);
	Args.bEval = FParse::Param(CommandLine, TEXT("Eval"));
	FParse::Value(CommandLine, TEXT("-EvalEpisodes="), Args.EvalEpisodes);
	FParse::Value(CommandLine, TEXT("-EvalJson="), Args.EvalJson);
	FParse::Value(CommandLine, TEXT("-StatsCsv="), Args.StatsCsv);
	FParse::Value(CommandLine, TEXT("-Set="), Args.SetOverrides, /*bShouldStopOnSeparator=*/false);
	Args.bInfer = Args.bInfer || Args.bEval;
	Args.StartDifficulty = FMath::Clamp(Args.StartDifficulty, 0.f, 1.f);
	Args.EvalEpisodes = FMath::Max(1, Args.EvalEpisodes);
```

  - Add the stage helper:

```cpp
int32 FFPSRunArgs::CombatStageIndexOf(const FString& InStage)
{
	if (InStage.Len() == 2 && (InStage[0] == TEXT('S') || InStage[0] == TEXT('s')) && InStage[1] >= TEXT('0') && InStage[1] <= TEXT('3'))
	{
		return InStage[1] - TEXT('0');
	}
	return -1;
}
```

  - Replace `ToString` with:

```cpp
FString FFPSRunArgs::ToString() const
{
	return FString::Printf(TEXT("Stage=%s Seed=%d Arenas=%d Map=%s Infer=%d Scripted=%d ResumeFrom=%s Iterations=%d Batches=%d StartDifficulty=%.2f Eval=%d EvalEpisodes=%d EvalJson=%s StatsCsv=%s Set=%s"),
		Stage.IsEmpty() ? TEXT("(unset)") : *Stage, Seed, Arenas, Map.IsEmpty() ? TEXT("(unset)") : *Map, bInfer ? 1 : 0, bScripted ? 1 : 0,
		ResumeFrom.IsEmpty() ? TEXT("(unset)") : *ResumeFrom, Iterations, Batches, StartDifficulty, bEval ? 1 : 0, EvalEpisodes,
		EvalJson.IsEmpty() ? TEXT("(unset)") : *EvalJson, StatsCsv.IsEmpty() ? TEXT("(unset)") : *StatsCsv,
		SetOverrides.IsEmpty() ? TEXT("(unset)") : *SetOverrides);
}
```

- [ ] **Step 7: Build and run the settings tests.**
  - Run: `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Settings"`
  - Expected: `Succeeded: 2  Failed: 0`.
  - Also run the full suite `powershell -File tools/run_tests.ps1`. Expected `Failed: 0`.
  - Nothing else reads the old default `Stage == "S0"`: `ArenaGameMode` only checks `IsSpikeA()` today.

- [ ] **Step 8: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Common Game/FPSAgent/Source/FPSAgent/Tests/SettingsTests.cpp
git commit -m "[local] Combat settings (reward, PPO, perception, S0 stage, ramp) and args -StartDifficulty/-Eval/-EvalEpisodes/-EvalJson/-StatsCsv/-Set; -Stage has no default"
git pull --rebase && git push
```

---

### Task 2: `-Set=` settings overrides

**Files:**
- Create: `Common/SettingsOverride.h`, `Common/SettingsOverride.cpp`
- Modify: `Arena/ArenaGameMode.cpp` (apply at the top of `InitGame`)
- Test: `Tests/SettingsOverrideTests.cpp`

**Interfaces:**
- Consumes: `FFPSRunArgs::SetOverrides` (Task 1).
- Produces: `bool FPSSettingsOverride::Apply(UObject& Target, const FString& Spec, TArray<FString>& OutApplied, FString& OutError)`.

- [ ] **Step 1: Write the failing test** `Tests/SettingsOverrideTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Common/FPSAgentSettings.h"
#include "Common/SettingsOverride.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSSettingsOverrideTest, "FPSAgent.Settings.Override",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSSettingsOverrideTest::RunTest(const FString& Parameters)
{
	TStrongObjectPtr<UFPSAgentSettings> S(NewObject<UFPSAgentSettings>(GetTransientPackage()));
	TArray<FString> Applied;
	FString Error;
	const bool bOk = FPSSettingsOverride::Apply(*S, TEXT("Combat.Reward.Kill=1.5; Combat.Ppo.Discount=0.997;Combat.Ppo.bTrainOnGpu=false;Curriculum.S0.BotsHard=3"), Applied, Error);
	TestTrue(TEXT("valid overrides apply"), bOk);
	TestEqual(TEXT("error empty"), Error, FString());
	TestEqual(TEXT("float"), S->Combat.Reward.Kill, 1.5f);
	TestEqual(TEXT("float in another struct"), S->Combat.Ppo.Discount, 0.997f);
	TestFalse(TEXT("bool"), S->Combat.Ppo.bTrainOnGpu);
	TestEqual(TEXT("int"), S->Curriculum.S0.BotsHard, 3);
	TestEqual(TEXT("applied list"), Applied.Num(), 4);
	TestEqual(TEXT("applied text is trimmed"), Applied[1], FString(TEXT("Combat.Ppo.Discount=0.997")));

	auto Fails = [&](const TCHAR* Spec, const TCHAR* Expect)
	{
		TArray<FString> A;
		FString E;
		const bool bResult = FPSSettingsOverride::Apply(*S, Spec, A, E);
		TestFalse(FString::Printf(TEXT("rejects %s"), Spec), bResult);
		TestTrue(FString::Printf(TEXT("error for %s mentions '%s' (got '%s')"), Spec, Expect, *E), E.Contains(Expect));
	};
	Fails(TEXT("Combat.Reward.Nope=1"), TEXT("unknown setting"));
	Fails(TEXT("Combat.Reward.Kill.X=1"), TEXT("not a struct"));
	Fails(TEXT("Combat.Reward.Kill=abc"), TEXT("not a number"));
	Fails(TEXT("Curriculum.S0.BotsHard=2.5"), TEXT("not an integer"));
	Fails(TEXT("Combat.Ppo.bTrainOnGpu=maybe"), TEXT("not a bool"));
	Fails(TEXT("Combat.Reward.Kill"), TEXT("Path=Value"));

	TArray<FString> None;
	TestTrue(TEXT("empty spec is fine"), FPSSettingsOverride::Apply(*S, TEXT(""), None, Error));
	TestEqual(TEXT("nothing applied"), None.Num(), 0);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Common/SettingsOverride.h'`.

- [ ] **Step 3: Implement** `Common/SettingsOverride.h`:

```cpp
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
```

`Common/SettingsOverride.cpp`:

```cpp
#include "Common/SettingsOverride.h"
#include "UObject/UnrealType.h"

namespace
{
	bool ParseNumber(const FString& Text, double& Out)
	{
		const FString T = Text.TrimStartAndEnd();
		if (T.IsEmpty())
		{
			return false;
		}
		TCHAR* End = nullptr;
		Out = FCString::Strtod(*T, &End);
		return End != nullptr && *End == TEXT('\0');
	}

	bool SetLeaf(FProperty* Prop, void* ValuePtr, const FString& RawValue, FString& OutError)
	{
		const FString Value = RawValue.TrimStartAndEnd();
		if (FBoolProperty* Bool = CastField<FBoolProperty>(Prop))
		{
			if (Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) || Value == TEXT("1")) { Bool->SetPropertyValue(ValuePtr, true); return true; }
			if (Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) || Value == TEXT("0")) { Bool->SetPropertyValue(ValuePtr, false); return true; }
			OutError = FString::Printf(TEXT("'%s' is not a bool"), *Value);
			return false;
		}
		if (FNumericProperty* Number = CastField<FNumericProperty>(Prop))
		{
			double D = 0.0;
			if (!ParseNumber(Value, D))
			{
				OutError = FString::Printf(TEXT("'%s' is not a number"), *Value);
				return false;
			}
			if (Number->IsFloatingPoint())
			{
				Number->SetFloatingPointPropertyValue(ValuePtr, D);
				return true;
			}
			if (FMath::Frac(D) != 0.0)
			{
				OutError = FString::Printf(TEXT("'%s' is not an integer"), *Value);
				return false;
			}
			Number->SetIntPropertyValue(ValuePtr, static_cast<int64>(D));
			return true;
		}
		if (!Prop->ImportText_Direct(*Value, ValuePtr, nullptr, PPF_None))
		{
			OutError = FString::Printf(TEXT("cannot parse '%s'"), *Value);
			return false;
		}
		return true;
	}
}

namespace FPSSettingsOverride
{
	bool Apply(UObject& Target, const FString& Spec, TArray<FString>& OutApplied, FString& OutError)
	{
		TArray<FString> Items;
		Spec.ParseIntoArray(Items, TEXT(";"), /*InCullEmpty=*/true);
		for (const FString& Item : Items)
		{
			FString Path;
			FString Value;
			if (!Item.Split(TEXT("="), &Path, &Value))
			{
				OutError = FString::Printf(TEXT("'%s' is not Path=Value"), *Item.TrimStartAndEnd());
				return false;
			}
			Path.TrimStartAndEndInline();
			TArray<FString> Parts;
			Path.ParseIntoArray(Parts, TEXT("."), /*InCullEmpty=*/true);
			if (Parts.Num() == 0)
			{
				OutError = FString::Printf(TEXT("'%s' has an empty path"), *Item.TrimStartAndEnd());
				return false;
			}
			const UStruct* Struct = Target.GetClass();
			void* Container = &Target;
			for (int32 i = 0; i < Parts.Num(); ++i)
			{
				FProperty* Prop = FindFProperty<FProperty>(Struct, FName(*Parts[i]));
				if (!Prop)
				{
					OutError = FString::Printf(TEXT("unknown setting '%s' (no '%s')"), *Path, *Parts[i]);
					return false;
				}
				void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Container);
				if (i + 1 < Parts.Num())
				{
					const FStructProperty* StructProp = CastField<FStructProperty>(Prop);
					if (!StructProp)
					{
						OutError = FString::Printf(TEXT("'%s' in '%s' is not a struct"), *Parts[i], *Path);
						return false;
					}
					Struct = StructProp->Struct;
					Container = ValuePtr;
				}
				else if (!SetLeaf(Prop, ValuePtr, Value, OutError))
				{
					OutError = FString::Printf(TEXT("%s: %s"), *Path, *OutError);
					return false;
				}
			}
			OutApplied.Add(FString::Printf(TEXT("%s=%s"), *Path, *Value.TrimStartAndEnd()));
		}
		return true;
	}
}
```

- [ ] **Step 4: Apply it at startup.** In `Arena/ArenaGameMode.cpp`:
  - Add the includes `#include "Common/SettingsOverride.h"` and `#include "HAL/PlatformMisc.h"`.
  - Add, above `AArenaGameMode::WantsSpikeA`:

```cpp
namespace
{
	/** -Set= overrides, applied once per process before anything reads the settings (spec §6, D-025). */
	bool ApplySettingsOverridesOnce()
	{
		static bool bDone = false;
		static bool bOk = true;
		if (bDone)
		{
			return bOk;
		}
		bDone = true;
		const FString& Spec = FFPSRunArgs::Get().SetOverrides;
		if (Spec.IsEmpty())
		{
			return true;
		}
		TArray<FString> Applied;
		FString Error;
		bOk = FPSSettingsOverride::Apply(*GetMutableDefault<UFPSAgentSettings>(), Spec, Applied, Error);
		if (bOk)
		{
			UE_LOG(LogFPSAgent, Display, TEXT("Settings overrides (-Set): %s"), *FString::Join(Applied, TEXT("; ")));
		}
		else
		{
			UE_LOG(LogFPSAgent, Error, TEXT("exiting (code 2): bad -Set: %s"), *Error);
		}
		return bOk;
	}
}
```

  - In `InitGame`, right after `Super::InitGame(...)`, add:

```cpp
	if (!ApplySettingsOverridesOnce())
	{
		if (!GIsEditor)
		{
			FPlatformMisc::RequestExitWithStatus(false, 2);
		}
		return;
	}
```

- [ ] **Step 5: Build and run the tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Settings"`. Expected: `Succeeded: 3  Failed: 0`.

- [ ] **Step 6: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Common/SettingsOverride.* Game/FPSAgent/Source/FPSAgent/Arena/ArenaGameMode.cpp Game/FPSAgent/Source/FPSAgent/Tests/SettingsOverrideTests.cpp
git commit -m "[local] -Set=Path=Value overrides via reflection (sweeps without rebuilds); bad -Set exits 2"
git pull --rebase && git push
```

---

### Task 3: Reward terms and the per-episode ledger

**Files:**
- Create: `Learning/CombatReward.h`, `Learning/CombatReward.cpp`
- Test: `Tests/CombatRewardTests.cpp`

**Interfaces:**
- Consumes: `FCombatRewardSettings` (Task 1).
- Produces (namespace `FPSCombatReward`):
  - `enum class ETerm { DamageDealt, Kill, FirstSighting, Clear, AimHelper, DamageTaken, Death, Miss, Time, Count }`, `TermCount`, `const TCHAR* TermName(ETerm)`.
  - `float DamageDealt(float, const FCombatRewardSettings&)`, `float DamageTaken(...)`, `float ClearBonus(float TimeLeftFraction, ...)`, `float AimPotential(float AimErrorDeg, float Weight, ...)`.
  - `struct FLedger { float Pending[TermCount]; float Episode[TermCount]; float LastPotential; void BeginEpisode(float); void Add(ETerm, float); bool TrySighting(int32, const S&); bool TryClear(float, const S&); void ApplyPotential(float NewPotential, float Discount); float TakeDecision(); float EpisodeReturn() const; float PendingTerm(ETerm) const; float EpisodeTerm(ETerm) const; }`.

- [ ] **Step 1: Write the failing test** `Tests/CombatRewardTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Learning/CombatReward.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatReward;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardTermsTest, "FPSAgent.Combat.Reward.Terms",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardTermsTest::RunTest(const FString& Parameters)
{
	const FCombatRewardSettings S;
	TestEqual(TEXT("kill from full = 1"), DamageDealt(150.f, S), 1.f, 1e-6f);
	TestEqual(TEXT("body shot"), DamageDealt(40.f, S), 40.f / 150.f, 1e-6f);
	TestEqual(TEXT("damage taken is negative"), DamageTaken(75.f, S), -0.5f, 1e-6f);
	TestEqual(TEXT("instant clear = 2.0"), ClearBonus(1.f, S), 2.f, 1e-6f);
	TestEqual(TEXT("clear at the limit = 0.5"), ClearBonus(0.f, S), 0.5f, 1e-6f);
	TestEqual(TEXT("clear halfway"), ClearBonus(0.5f, S), 1.25f, 1e-6f);
	TestEqual(TEXT("clear clamps"), ClearBonus(3.f, S), 2.f, 1e-6f);
	TestEqual(TEXT("no visible enemy: potential 0"), AimPotential(-1.f, 0.2f, S), 0.f);
	TestEqual(TEXT("on target: potential W"), AimPotential(0.f, 0.2f, S), 0.2f, 1e-6f);
	TestEqual(TEXT("half range: W/2"), AimPotential(15.f, 0.2f, S), 0.1f, 1e-6f);
	TestEqual(TEXT("beyond range: 0, never negative"), AimPotential(45.f, 0.2f, S), 0.f);
	TestEqual(TEXT("term names"), FString(TermName(ETerm::AimHelper)), FString(TEXT("AimHelper")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardLedgerTest, "FPSAgent.Combat.Reward.Ledger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardLedgerTest::RunTest(const FString& Parameters)
{
	const FCombatRewardSettings S;
	FLedger L;
	L.BeginEpisode(0.f);
	L.Add(ETerm::Kill, 1.f);
	L.Add(ETerm::Miss, -0.01f);
	TestEqual(TEXT("pending kill"), L.PendingTerm(ETerm::Kill), 1.f);
	TestEqual(TEXT("decision reward"), L.TakeDecision(), 0.99f, 1e-6f);
	TestEqual(TEXT("taken once: next decision is 0"), L.TakeDecision(), 0.f);
	TestEqual(TEXT("episode return"), L.EpisodeReturn(), 0.99f, 1e-6f);
	TestEqual(TEXT("episode kill term"), L.EpisodeTerm(ETerm::Kill), 1.f);

	TestTrue(TEXT("first sighting of bot 2"), L.TrySighting(2, S));
	TestFalse(TEXT("bot 2 again: no"), L.TrySighting(2, S));
	TestTrue(TEXT("bot 3 is new"), L.TrySighting(3, S));
	TestEqual(TEXT("two sightings pending"), L.PendingTerm(ETerm::FirstSighting), 0.4f, 1e-6f);

	TestTrue(TEXT("clear pays"), L.TryClear(0.5f, S));
	TestFalse(TEXT("clear pays once (D-022)"), L.TryClear(1.f, S));
	TestEqual(TEXT("clear amount"), L.PendingTerm(ETerm::Clear), 1.25f, 1e-6f);

	L.BeginEpisode(0.f);
	TestEqual(TEXT("new episode: return cleared"), L.EpisodeReturn(), 0.f);
	TestEqual(TEXT("new episode: pending cleared"), L.PendingTerm(ETerm::Clear), 0.f);
	TestTrue(TEXT("new episode: sightings reset"), L.TrySighting(2, S));
	TestTrue(TEXT("new episode: clear available"), L.TryClear(0.f, S));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatRewardPotentialTest, "FPSAgent.Combat.Reward.AimPotential",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatRewardPotentialTest::RunTest(const FString& Parameters)
{
	// Potential-based shaping telescopes: with discount 1 the episode sum is Phi(end) - Phi(start) = -Phi(start).
	FLedger L;
	L.BeginEpisode(0.1f);
	L.ApplyPotential(0.15f, 1.f);
	TestEqual(TEXT("closer aim pays the difference"), L.PendingTerm(ETerm::AimHelper), 0.05f, 1e-6f);
	L.ApplyPotential(0.f, 1.f);
	TestEqual(TEXT("sum telescopes to -Phi(start)"), L.PendingTerm(ETerm::AimHelper), -0.1f, 1e-6f);

	FLedger D;
	D.BeginEpisode(0.f);
	D.ApplyPotential(0.2f, 0.995f);
	TestEqual(TEXT("discounted gamma*Phi' - Phi"), D.PendingTerm(ETerm::AimHelper), 0.995f * 0.2f, 1e-6f);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Learning/CombatReward.h'`.

- [ ] **Step 3: Implement** `Learning/CombatReward.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Combat reward terms and per-episode bookkeeping (spec §3). Pure: no world, no Learning Agents. */
namespace FPSCombatReward
{
	enum class ETerm : uint8 { DamageDealt, Kill, FirstSighting, Clear, AimHelper, DamageTaken, Death, Miss, Time, Count };
	inline constexpr int32 TermCount = static_cast<int32>(ETerm::Count);
	FPSAGENT_API const TCHAR* TermName(ETerm Term);

	/** Health + armor actually removed from a bot, scaled. */
	FPSAGENT_API float DamageDealt(float AppliedDamage, const FCombatRewardSettings& S);
	/** Health + armor the agent lost (negative). */
	FPSAGENT_API float DamageTaken(float AppliedDamage, const FCombatRewardSettings& S);
	/** ClearBase + ClearSpeed * clamp(time left fraction, 0, 1). */
	FPSAGENT_API float ClearBonus(float TimeLeftFraction, const FCombatRewardSettings& S);
	/** Aim-helper potential W * (1 - min(err / range, 1)); 0 when no enemy is visible (AimErrorDeg < 0). Never negative. */
	FPSAGENT_API float AimPotential(float AimErrorDeg, float Weight, const FCombatRewardSettings& S);

	/** One agent's episode: per-term sums for the current decision (Pending) and the episode; one-off terms paid once. */
	struct FPSAGENT_API FLedger
	{
		float Pending[TermCount] = {};
		float Episode[TermCount] = {};
		/** Aim-helper potential of the state after the last decision. */
		float LastPotential = 0.f;
		bool bClearPaid = false;
		TSet<int32> Sighted;

		/** New episode; InitialPotential = the aim-helper potential of the spawn state. */
		void BeginEpisode(float InitialPotential);
		void Add(ETerm Term, float Value) { Pending[static_cast<int32>(Term)] += Value; }
		/** First time this bot is seen this episode: pays FirstSighting and returns true. */
		bool TrySighting(int32 BotIndex, const FCombatRewardSettings& S);
		/** Pays the clear bonus the first time per episode; returns true then. */
		bool TryClear(float TimeLeftFraction, const FCombatRewardSettings& S);
		/** Aim helper for the decision that ends in a state with potential NewPotential: gamma * Phi(s') - Phi(s). Pass 0 at the end. */
		void ApplyPotential(float NewPotential, float Discount);
		/** The reward of the decision that just ended: moves Pending into Episode and returns its sum. */
		float TakeDecision();
		float EpisodeReturn() const;
		float PendingTerm(ETerm Term) const { return Pending[static_cast<int32>(Term)]; }
		float EpisodeTerm(ETerm Term) const { return Episode[static_cast<int32>(Term)]; }
	};
}
```

`Learning/CombatReward.cpp`:

```cpp
#include "Learning/CombatReward.h"

namespace FPSCombatReward
{
	const TCHAR* TermName(ETerm Term)
	{
		switch (Term)
		{
		case ETerm::DamageDealt:   return TEXT("DamageDealt");
		case ETerm::Kill:          return TEXT("Kill");
		case ETerm::FirstSighting: return TEXT("FirstSighting");
		case ETerm::Clear:         return TEXT("Clear");
		case ETerm::AimHelper:     return TEXT("AimHelper");
		case ETerm::DamageTaken:   return TEXT("DamageTaken");
		case ETerm::Death:         return TEXT("Death");
		case ETerm::Miss:          return TEXT("Miss");
		case ETerm::Time:          return TEXT("Time");
		default:                   return TEXT("Unknown");
		}
	}

	float DamageDealt(float AppliedDamage, const FCombatRewardSettings& S)
	{
		return S.DamageDealtWeight * AppliedDamage / FMath::Max(1.f, S.DamageNormalizer);
	}

	float DamageTaken(float AppliedDamage, const FCombatRewardSettings& S)
	{
		return -S.DamageTakenWeight * AppliedDamage / FMath::Max(1.f, S.DamageNormalizer);
	}

	float ClearBonus(float TimeLeftFraction, const FCombatRewardSettings& S)
	{
		return S.ClearBase + S.ClearSpeed * FMath::Clamp(TimeLeftFraction, 0.f, 1.f);
	}

	float AimPotential(float AimErrorDeg, float Weight, const FCombatRewardSettings& S)
	{
		if (AimErrorDeg < 0.f)
		{
			return 0.f;
		}
		return Weight * (1.f - FMath::Min(AimErrorDeg / FMath::Max(1e-3f, S.AimHelperRangeDeg), 1.f));
	}

	void FLedger::BeginEpisode(float InitialPotential)
	{
		FMemory::Memzero(Pending, sizeof(Pending));
		FMemory::Memzero(Episode, sizeof(Episode));
		LastPotential = InitialPotential;
		bClearPaid = false;
		Sighted.Reset();
	}

	bool FLedger::TrySighting(int32 BotIndex, const FCombatRewardSettings& S)
	{
		if (Sighted.Contains(BotIndex))
		{
			return false;
		}
		Sighted.Add(BotIndex);
		Add(ETerm::FirstSighting, S.FirstSighting);
		return true;
	}

	bool FLedger::TryClear(float TimeLeftFraction, const FCombatRewardSettings& S)
	{
		if (bClearPaid)
		{
			return false;
		}
		bClearPaid = true;
		Add(ETerm::Clear, ClearBonus(TimeLeftFraction, S));
		return true;
	}

	void FLedger::ApplyPotential(float NewPotential, float Discount)
	{
		Add(ETerm::AimHelper, Discount * NewPotential - LastPotential);
		LastPotential = NewPotential;
	}

	float FLedger::TakeDecision()
	{
		float Sum = 0.f;
		for (int32 i = 0; i < TermCount; ++i)
		{
			Sum += Pending[i];
			Episode[i] += Pending[i];
			Pending[i] = 0.f;
		}
		return Sum;
	}

	float FLedger::EpisodeReturn() const
	{
		float Sum = 0.f;
		for (int32 i = 0; i < TermCount; ++i)
		{
			Sum += Episode[i];
		}
		return Sum;
	}
}
```

- [ ] **Step 4: Build and run the tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Combat.Reward"`. Expected: `Succeeded: 3  Failed: 0`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatReward.* Game/FPSAgent/Source/FPSAgent/Tests/CombatRewardTests.cpp
git commit -m "[local] Combat reward terms + ledger (once-only sighting/clear, potential-based aim helper)"
git pull --rebase && git push
```

---

### Task 4: Difficulty controller and the S0 episode sampler

**Files:**
- Create: `Learning/Curriculum.h`, `Learning/Curriculum.cpp`
- Test: `Tests/CurriculumTests.cpp`

**Interfaces:**
- Consumes: `FCurriculumSettings`, `FCombatStageSpec` (Task 1).
- Produces (namespace `FPSCurriculum`):
  - `struct FDifficultyController { float Level; TArray<bool> Window; void Init(float); float Record(bool bCleared, const FCurriculumSettings&); }`.
  - `float Knob(FRandomStream&, float Easy, float Hard, float Level, float JitterFraction)`.
  - `struct FS0Episode { float Level; bool bReview; float AgentYawDeg; TArray<FVector2D> BotOffsets; TArray<bool> BotCrouched; }`.
  - `FS0Episode SampleS0(FRandomStream&, float ControllerLevel, bool bAllowReview, const FCurriculumSettings&, float MaxRadiusCm)`.

- [ ] **Step 1: Write the failing test** `Tests/CurriculumTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Learning/Curriculum.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCurriculum;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumControllerTest, "FPSAgent.Combat.Curriculum.Controller",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumControllerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C; // window 20, raise > 0.7, lower < 0.4, step 0.05
	FDifficultyController D;
	D.Init(0.f);
	for (int32 i = 0; i < 19; ++i) { TestEqual(TEXT("no change before the window fills"), D.Record(true, C), 0.f); }
	TestEqual(TEXT("20 clears raise the level"), D.Record(true, C), 0.05f, 1e-6f);
	TestEqual(TEXT("level"), D.Level, 0.05f, 1e-6f);
	TestEqual(TEXT("window resets after a change"), D.Window.Num(), 0);

	FDifficultyController Low;
	Low.Init(0.f);
	for (int32 i = 0; i < 25; ++i) { Low.Record(false, C); }
	TestEqual(TEXT("can't go below 0"), Low.Level, 0.f);
	TestEqual(TEXT("at the bound the window keeps rolling"), Low.Window.Num(), 20);

	FDifficultyController Mid;
	Mid.Init(0.5f);
	for (int32 i = 0; i < 20; ++i) { Mid.Record(false, C); }
	TestEqual(TEXT("20 failures lower the level"), Mid.Level, 0.45f, 1e-6f);

	FDifficultyController Even;
	Even.Init(0.5f);
	for (int32 i = 0; i < 30; ++i) { Even.Record(i % 2 == 0, C); }
	TestEqual(TEXT("50% clears: no change"), Even.Level, 0.5f, 1e-6f);
	TestEqual(TEXT("rolling window keeps the last 20"), Even.Window.Num(), 20);

	FDifficultyController Top;
	Top.Init(1.f);
	for (int32 i = 0; i < 20; ++i) { Top.Record(true, C); }
	TestEqual(TEXT("can't go above 1"), Top.Level, 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumSamplerTest, "FPSAgent.Combat.Curriculum.SampleS0",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumSamplerTest::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C;
	const float MaxRadius = 3700.f;
	for (int32 Seed = 0; Seed < 200; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 0.f, false, C, MaxRadius);
		TestEqual(TEXT("level 0: one bot"), E.BotOffsets.Num(), 1);
		TestEqual(TEXT("crouch flags match"), E.BotCrouched.Num(), E.BotOffsets.Num());
		for (const FVector2D& P : E.BotOffsets)
		{
			const double Dist = P.Size();
			TestTrue(TEXT("level 0: at least the minimum distance"), Dist >= C.S0.BotDistanceMinCm - 1.0);
			TestTrue(TEXT("level 0: at most easy max + jitter (1700)"), Dist <= 1700.0 + 1.0);
			const float Rel = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(P.Y, P.X)) - E.AgentYawDeg);
			TestTrue(TEXT("level 0: within 45 + jitter (58.5) degrees of the facing"), FMath::Abs(Rel) <= 58.5f + 0.01f);
		}
	}
	for (int32 Seed = 0; Seed < 50; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FS0Episode E = SampleS0(Rng, 1.f, false, C, MaxRadius);
		TestEqual(TEXT("level 1: five bots"), E.BotOffsets.Num(), 5);
		for (const FVector2D& P : E.BotOffsets) { TestTrue(TEXT("inside the radius"), P.Size() <= MaxRadius + 1.0); }
	}

	FRandomStream A(42), B(42);
	const FS0Episode EA = SampleS0(A, 0.7f, true, C, MaxRadius);
	const FS0Episode EB = SampleS0(B, 0.7f, true, C, MaxRadius);
	TestEqual(TEXT("deterministic count"), EA.BotOffsets.Num(), EB.BotOffsets.Num());
	TestEqual(TEXT("deterministic yaw"), EA.AgentYawDeg, EB.AgentYawDeg);
	for (int32 i = 0; i < EA.BotOffsets.Num(); ++i) { TestEqual(TEXT("deterministic offsets"), EA.BotOffsets[i], EB.BotOffsets[i]); }

	int32 Reviews = 0;
	FRandomStream R(7);
	for (int32 i = 0; i < 2000; ++i)
	{
		const FS0Episode E = SampleS0(R, 0.6f, true, C, MaxRadius);
		if (E.bReview)
		{
			++Reviews;
			TestTrue(TEXT("review level is easier"), E.Level <= 0.6f + 1e-6f);
		}
		else
		{
			TestEqual(TEXT("counted episodes use the controller level"), E.Level, 0.6f);
		}
	}
	TestTrue(FString::Printf(TEXT("about 20%% reviews (%d of 2000)"), Reviews), Reviews >= 330 && Reviews <= 470);
	FRandomStream NoReview(7);
	for (int32 i = 0; i < 200; ++i) { TestFalse(TEXT("no reviews when not allowed (-Eval)"), SampleS0(NoReview, 0.6f, false, C, MaxRadius).bReview); }
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Learning/Curriculum.h'`.

- [ ] **Step 3: Implement** `Learning/Curriculum.h`:

```cpp
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
```

`Learning/Curriculum.cpp`:

```cpp
#include "Learning/Curriculum.h"

namespace
{
	/** Bots are kept at least this far apart (spawn geometry: a bean is about 70 cm wide). */
	constexpr float MinBotGapCm = 150.f;
	constexpr int32 PlacementTries = 16;
}

namespace FPSCurriculum
{
	void FDifficultyController::Init(float StartLevel)
	{
		Level = FMath::Clamp(StartLevel, 0.f, 1.f);
		Window.Reset();
	}

	float FDifficultyController::Record(bool bCleared, const FCurriculumSettings& C)
	{
		const int32 N = FMath::Max(1, C.WindowEpisodes);
		Window.Add(bCleared);
		if (Window.Num() < N)
		{
			return 0.f;
		}
		while (Window.Num() > N)
		{
			Window.RemoveAt(0);
		}
		int32 Clears = 0;
		for (const bool b : Window)
		{
			Clears += b ? 1 : 0;
		}
		const float Rate = static_cast<float>(Clears) / N;
		float Target = Level;
		if (Rate > C.RaiseAboveClearRate)
		{
			Target = FMath::Min(1.f, Level + C.Step);
		}
		else if (Rate < C.LowerBelowClearRate)
		{
			Target = FMath::Max(0.f, Level - C.Step);
		}
		const float Change = Target - Level;
		if (!FMath::IsNearlyZero(Change))
		{
			Level = Target;
			Window.Reset();
			return Change;
		}
		return 0.f;
	}

	float Knob(FRandomStream& Rng, float Easy, float Hard, float Level, float JitterFraction)
	{
		const float Base = FMath::Lerp(Easy, Hard, FMath::Clamp(Level, 0.f, 1.f));
		const float Jitter = JitterFraction * FMath::Abs(Hard - Easy);
		const float Value = Base + (Jitter > 0.f ? Rng.FRandRange(-Jitter, Jitter) : 0.f);
		return FMath::Clamp(Value, FMath::Min(Easy, Hard), FMath::Max(Easy, Hard));
	}

	FS0Episode SampleS0(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, float MaxRadiusCm)
	{
		const FCombatStageSpec& S = C.S0;
		FS0Episode E;
		E.bReview = bAllowReview && Rng.FRand() < C.ReviewFraction;
		E.Level = E.bReview ? Rng.FRandRange(0.f, ControllerLevel) : ControllerLevel;

		const int32 MaxBots = FMath::Max(S.BotsEasy, S.BotsHard);
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Knob(Rng, S.BotsEasy, S.BotsHard, E.Level, C.JitterFraction)), 1, MaxBots);
		const float MaxDist = FMath::Min(Knob(Rng, S.BotDistanceMaxCmEasy, S.BotDistanceMaxCmHard, E.Level, C.JitterFraction), MaxRadiusCm);
		const float MinDist = FMath::Min(S.BotDistanceMinCm, MaxDist);
		const float Spread = Knob(Rng, S.BotSpreadDegEasy, S.BotSpreadDegHard, E.Level, C.JitterFraction);
		const float CrouchChance = Knob(Rng, S.CrouchedFractionEasy, S.CrouchedFractionHard, E.Level, C.JitterFraction);
		E.AgentYawDeg = Rng.FRandRange(-180.f, 180.f);

		for (int32 i = 0; i < Count; ++i)
		{
			FVector2D P = FVector2D::ZeroVector;
			for (int32 Try = 0; Try < PlacementTries; ++Try)
			{
				const float Yaw = FMath::DegreesToRadians(E.AgentYawDeg + Rng.FRandRange(-Spread, Spread));
				const float Dist = Rng.FRandRange(MinDist, MaxDist);
				P = FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * Dist;
				bool bFree = true;
				for (const FVector2D& Q : E.BotOffsets)
				{
					if (FVector2D::Distance(P, Q) < MinBotGapCm) { bFree = false; break; }
				}
				if (bFree) { break; }
			}
			E.BotOffsets.Add(P);
			E.BotCrouched.Add(Rng.FRand() < CrouchChance);
		}
		return E;
	}
}
```

- [ ] **Step 4: Build and run the tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Combat.Curriculum"`. Expected: `Succeeded: 2  Failed: 0`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/Curriculum.* Game/FPSAgent/Source/FPSAgent/Tests/CurriculumTests.cpp
git commit -m "[local] Curriculum: per-arena difficulty controller (+/-0.05 at 70%/40%) and seeded S0 episode sampler with review episodes"
git pull --rebase && git push
```

---

### Task 5: Fair perception (pure math + geometry-only traces)

**Files:**
- Create: `Common/Perception.h`, `Common/Perception.cpp`
- Test: `Tests/PerceptionTests.cpp`

**Interfaces:**
- Produces (namespace `FPSPerception`):
  - `FVector2D AimErrorDeg(const FVector& Eye, const FRotator& View, const FVector& Target)`: (yaw, pitch); + = right / up.
  - `bool InFieldOfView(Eye, View, Target, float FovDeg)`.
  - `FVector ToViewFrame(const FVector&, float ViewYawDeg)`.
  - `TArray<FVector> RayDirections(float ViewYawDeg, int32 YawCount, const TArray<float>& PitchesDeg)`.
  - `struct FSighting { int32 BotIndex; bool bAlive; bool bVisible; FVector Head; FVector Velocity; bool bFacingAgent; bool bCrouched; }`.
  - `struct FTrack { int32 BotIndex; bool bVisible; double LastSeenTime; FVector Head; FVector Velocity; bool bFacingAgent; bool bCrouched; }`.
  - `void UpdateTracks(TArray<FTrack>&, const TArray<FSighting>&, double Now, float MemorySeconds)`.
  - `TArray<FTrack> SelectTracks(const TArray<FTrack>&, int32 Max)`.
  - `bool HasLineOfSight(const UWorld*, const FVector& From, const FVector& To)`.
  - `float RayFraction(const UWorld*, const FVector& Start, const FVector& Direction, float RangeCm)`.

- [ ] **Step 1: Write the failing test** `Tests/PerceptionTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Arena/ArenaBuilder.h"
#include "Common/Perception.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSPerception;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionMathTest, "FPSAgent.Perception.Math",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionMathTest::RunTest(const FString& Parameters)
{
	const FVector Eye(0.0, 0.0, 0.0);
	const FVector2D Right10 = AimErrorDeg(Eye, FRotator::ZeroRotator, FVector(1000.0, 1000.0 * FMath::Tan(FMath::DegreesToRadians(10.0)), 0.0));
	TestEqual(TEXT("10 deg right = +10 yaw"), static_cast<float>(Right10.X), 10.f, 1e-3f);
	TestEqual(TEXT("level = 0 pitch"), static_cast<float>(Right10.Y), 0.f, 1e-3f);
	const FVector2D FromLeft = AimErrorDeg(Eye, FRotator(0.0, 350.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("view yaw 350 -> turn +10"), static_cast<float>(FromLeft.X), 10.f, 1e-3f);
	const FVector2D Up5 = AimErrorDeg(Eye, FRotator(355.0, 0.0, 0.0), FVector(1000.0, 0.0, 0.0));
	TestEqual(TEXT("looking 5 deg down -> pitch up +5"), static_cast<float>(Up5.Y), 5.f, 1e-3f);

	auto AtYaw = [](double Deg) { return FVector(FMath::Cos(FMath::DegreesToRadians(Deg)) * 1000.0, FMath::Sin(FMath::DegreesToRadians(Deg)) * 1000.0, 0.0); };
	TestTrue(TEXT("51 deg is inside a 103 deg FOV"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(51.0), 103.f));
	TestFalse(TEXT("52 deg is outside"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(52.0), 103.f));
	TestFalse(TEXT("behind is outside"), InFieldOfView(Eye, FRotator::ZeroRotator, AtYaw(180.0), 103.f));

	const FVector V = ToViewFrame(FVector(0.0, 100.0, 5.0), 90.f);
	TestTrue(TEXT("world +Y is forward when facing yaw 90"), V.Equals(FVector(100.0, 0.0, 5.0), 1e-3));
	const FVector R = ToViewFrame(FVector(0.0, 100.0, 0.0), 0.f);
	TestTrue(TEXT("world +Y is right when facing yaw 0"), R.Equals(FVector(0.0, 100.0, 0.0), 1e-3));

	const TArray<float> Pitches = { -20.f, 0.f, 15.f };
	const TArray<FVector> Rays = RayDirections(0.f, 24, Pitches);
	TestEqual(TEXT("24 x 3 rays"), Rays.Num(), 72);
	TestTrue(TEXT("first ray: ahead, pitched down 20"), Rays[0].Equals(FRotator(-20.0, 0.0, 0.0).Vector(), 1e-4));
	TestTrue(TEXT("level ring starts straight ahead"), Rays[24].Equals(FVector(1.0, 0.0, 0.0), 1e-4));
	TestTrue(TEXT("15 deg steps"), Rays[25].Equals(FRotator(0.0, 15.0, 0.0).Vector(), 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionTracksTest, "FPSAgent.Perception.Tracks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionTracksTest::RunTest(const FString& Parameters)
{
	auto Sight = [](int32 Bot, bool bVisible, bool bAlive)
	{
		FSighting S;
		S.BotIndex = Bot;
		S.bVisible = bVisible;
		S.bAlive = bAlive;
		S.Head = FVector(100.0 * Bot, 0.0, 0.0);
		return S;
	};
	TArray<FTrack> T;
	UpdateTracks(T, { Sight(1, true, true) }, 10.0, 2.f);
	TestEqual(TEXT("visible bot gets a track"), T.Num(), 1);
	TestTrue(TEXT("track visible"), T[0].bVisible);
	UpdateTracks(T, { Sight(1, false, true) }, 11.0, 2.f);
	TestEqual(TEXT("remembered within 2 s"), T.Num(), 1);
	TestFalse(TEXT("now remembered, not visible"), T[0].bVisible);
	TestEqual(TEXT("last seen time kept"), T[0].LastSeenTime, 10.0);
	UpdateTracks(T, { Sight(1, false, true) }, 12.5, 2.f);
	TestEqual(TEXT("forgotten after 2 s"), T.Num(), 0);
	UpdateTracks(T, { Sight(2, true, true) }, 20.0, 2.f);
	UpdateTracks(T, { Sight(2, false, false) }, 20.1, 2.f);
	TestEqual(TEXT("dead bots drop out"), T.Num(), 0);
	UpdateTracks(T, { Sight(3, true, true) }, 30.0, 2.f);
	UpdateTracks(T, { Sight(4, false, true) }, 30.1, 2.f);
	TestEqual(TEXT("never-seen bots get no track"), T.Num(), 1);

	FTrack A; A.BotIndex = 0; A.bVisible = true;  A.LastSeenTime = 5.0;
	FTrack B; B.BotIndex = 1; B.bVisible = false; B.LastSeenTime = 9.0;
	FTrack C; C.BotIndex = 2; C.bVisible = false; C.LastSeenTime = 8.0;
	const TArray<FTrack> Top = SelectTracks({ C, B, A }, 2);
	TestEqual(TEXT("limited"), Top.Num(), 2);
	TestEqual(TEXT("visible first"), Top[0].BotIndex, 0);
	TestEqual(TEXT("then most recent"), Top[1].BotIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSPerceptionWorldTest, "FPSAgent.Perception.World",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSPerceptionWorldTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	W.SpawnBean(FVector(500.0, 0.0, 100.0), FRotator::ZeroRotator);                                // a pawn on the +X line
	FPSArena::SpawnBlock(W.World, FVector(500.0, 1000.0, 150.0), FVector(50.0, 400.0, 400.0));    // a wall on the y=1000 line
	FPSArena::SpawnBlock(W.World, FVector(1000.0, -1000.0, 150.0), FVector(50.0, 400.0, 400.0));  // a wall on the y=-1000 line
	W.Step();
	TestTrue(TEXT("pawns never block line of sight"), HasLineOfSight(W.World, FVector(0.0, 0.0, 100.0), FVector(1000.0, 0.0, 100.0)));
	TestFalse(TEXT("walls block line of sight"), HasLineOfSight(W.World, FVector(0.0, 1000.0, 100.0), FVector(1000.0, 1000.0, 100.0)));
	TestEqual(TEXT("rays pass through pawns"), RayFraction(W.World, FVector(0.0, 0.0, 100.0), FVector(1.0, 0.0, 0.0), 3000.f), 1.f);
	TestEqual(TEXT("rays stop at walls"), RayFraction(W.World, FVector(0.0, -1000.0, 100.0), FVector(1.0, 0.0, 0.0), 3000.f), 975.f / 3000.f, 0.01f);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Common/Perception.h'`.

- [ ] **Step 3: Implement** `Common/Perception.h`:

```cpp
#pragma once

#include "CoreMinimal.h"

class UWorld;

/**
 * Fair perception (hard rule 4, D-024), shared by the agent and (from build B) the bots. Pure helpers, plus two traces that
 * only hit level geometry: object-type queries on WorldStatic + WorldDynamic, so pawns (object type Pawn) never block a
 * ray and never show up in one.
 */
namespace FPSPerception
{
	/** How far View must turn to look at Target: (yaw, pitch) in degrees, [-180, 180]; + = right / up. */
	FPSAGENT_API FVector2D AimErrorDeg(const FVector& Eye, const FRotator& View, const FVector& Target);
	/** Target inside the horizontal field of view (|yaw error| <= FovDeg / 2). */
	FPSAGENT_API bool InFieldOfView(const FVector& Eye, const FRotator& View, const FVector& Target, float FovDeg);
	/** A world vector in the yaw-only view frame: X forward, Y right, Z up. */
	FPSAGENT_API FVector ToViewFrame(const FVector& WorldVector, float ViewYawDeg);
	/** For each pitch, YawCount directions evenly around ViewYawDeg (the first one straight ahead). */
	FPSAGENT_API TArray<FVector> RayDirections(float ViewYawDeg, int32 YawCount, const TArray<float>& PitchesDeg);

	/** One bot as seen on this decision. */
	struct FSighting
	{
		int32 BotIndex = INDEX_NONE;
		bool bAlive = true;
		/** Passed this decision's line-of-sight-in-FOV check. */
		bool bVisible = false;
		FVector Head = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		bool bFacingAgent = false;
		bool bCrouched = false;
	};

	/** What the agent knows about a bot: refreshed while visible, kept for MemorySeconds afterwards. */
	struct FTrack
	{
		int32 BotIndex = INDEX_NONE;
		bool bVisible = false;
		double LastSeenTime = 0.0;
		FVector Head = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		bool bFacingAgent = false;
		bool bCrouched = false;
	};

	/** Visible sightings refresh their track; unseen ones keep the last-seen data until MemorySeconds pass; dead bots drop out. */
	FPSAGENT_API void UpdateTracks(TArray<FTrack>& Tracks, const TArray<FSighting>& Sightings, double Now, float MemorySeconds);
	/** At most Max tracks: visible first, then the most recently seen. */
	FPSAGENT_API TArray<FTrack> SelectTracks(const TArray<FTrack>& Tracks, int32 Max);

	/** True if no level geometry blocks From -> To (pawns are ignored). */
	FPSAGENT_API bool HasLineOfSight(const UWorld* World, const FVector& From, const FVector& To);
	/** Fraction (0..1) of RangeCm a ray travels before it hits level geometry (pawns are ignored). */
	FPSAGENT_API float RayFraction(const UWorld* World, const FVector& Start, const FVector& Direction, float RangeCm);
}
```

`Common/Perception.cpp`:

```cpp
#include "Common/Perception.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"

namespace
{
	FCollisionObjectQueryParams LevelGeometry()
	{
		FCollisionObjectQueryParams Objects;
		Objects.AddObjectTypesToQuery(ECC_WorldStatic);
		Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
		return Objects;
	}
}

namespace FPSPerception
{
	FVector2D AimErrorDeg(const FVector& Eye, const FRotator& View, const FVector& Target)
	{
		const FRotator To = (Target - Eye).Rotation();
		return FVector2D(FRotator::NormalizeAxis(To.Yaw - View.Yaw), FRotator::NormalizeAxis(To.Pitch - FRotator::NormalizeAxis(View.Pitch)));
	}

	bool InFieldOfView(const FVector& Eye, const FRotator& View, const FVector& Target, float FovDeg)
	{
		return FMath::Abs(AimErrorDeg(Eye, View, Target).X) <= 0.5 * FovDeg;
	}

	FVector ToViewFrame(const FVector& WorldVector, float ViewYawDeg)
	{
		return FRotator(0.0, ViewYawDeg, 0.0).UnrotateVector(WorldVector);
	}

	TArray<FVector> RayDirections(float ViewYawDeg, int32 YawCount, const TArray<float>& PitchesDeg)
	{
		TArray<FVector> Out;
		const int32 N = FMath::Max(1, YawCount);
		Out.Reserve(N * PitchesDeg.Num());
		for (const float Pitch : PitchesDeg)
		{
			for (int32 k = 0; k < N; ++k)
			{
				Out.Add(FRotator(Pitch, ViewYawDeg + 360.0 * k / N, 0.0).Vector());
			}
		}
		return Out;
	}

	void UpdateTracks(TArray<FTrack>& Tracks, const TArray<FSighting>& Sightings, double Now, float MemorySeconds)
	{
		for (const FSighting& S : Sightings)
		{
			const int32 Idx = Tracks.IndexOfByPredicate([&S](const FTrack& T) { return T.BotIndex == S.BotIndex; });
			if (!S.bAlive)
			{
				if (Idx != INDEX_NONE) { Tracks.RemoveAt(Idx); }
				continue;
			}
			if (S.bVisible)
			{
				FTrack& T = Idx != INDEX_NONE ? Tracks[Idx] : Tracks.AddDefaulted_GetRef();
				T.BotIndex = S.BotIndex;
				T.bVisible = true;
				T.LastSeenTime = Now;
				T.Head = S.Head;
				T.Velocity = S.Velocity;
				T.bFacingAgent = S.bFacingAgent;
				T.bCrouched = S.bCrouched;
			}
			else if (Idx != INDEX_NONE)
			{
				Tracks[Idx].bVisible = false;
			}
		}
		Tracks.RemoveAll([Now, MemorySeconds](const FTrack& T) { return !T.bVisible && Now - T.LastSeenTime > MemorySeconds; });
	}

	TArray<FTrack> SelectTracks(const TArray<FTrack>& Tracks, int32 Max)
	{
		TArray<FTrack> Sorted = Tracks;
		Sorted.StableSort([](const FTrack& A, const FTrack& B)
		{
			if (A.bVisible != B.bVisible) { return A.bVisible; }
			return A.LastSeenTime > B.LastSeenTime;
		});
		if (Sorted.Num() > Max) { Sorted.SetNum(FMath::Max(0, Max)); }
		return Sorted;
	}

	bool HasLineOfSight(const UWorld* World, const FVector& From, const FVector& To)
	{
		FHitResult Hit;
		return !World->LineTraceSingleByObjectType(Hit, From, To, LevelGeometry(), FCollisionQueryParams(SCENE_QUERY_STAT(FPSLineOfSight), false));
	}

	float RayFraction(const UWorld* World, const FVector& Start, const FVector& Direction, float RangeCm)
	{
		FHitResult Hit;
		const FVector End = Start + Direction.GetSafeNormal() * RangeCm;
		if (World->LineTraceSingleByObjectType(Hit, Start, End, LevelGeometry(), FCollisionQueryParams(SCENE_QUERY_STAT(FPSRay), false)))
		{
			return FMath::Clamp(static_cast<float>(Hit.Distance / RangeCm), 0.f, 1.f);
		}
		return 1.f;
	}
}
```

- [ ] **Step 4: Build and run the tests.**
  - Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Perception"`.
  - Expected: `Succeeded: 3  Failed: 0`.
  - If "walls block" fails, the runtime blocks are not WorldStatic/WorldDynamic. Log `Block->GetStaticMeshComponent()->GetCollisionObjectType()` and add that object type to `LevelGeometry()`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Common/Perception.* Game/FPSAgent/Source/FPSAgent/Tests/PerceptionTests.cpp
git commit -m "[local] Fair perception: aim error/FOV/view frame/ray ring, 2 s enemy memory, geometry-only line of sight and rays (pawns ignored)"
git pull --rebase && git push
```

---
### Task 6: Open floor, an aim-keeping AI controller, and the scripted bot (Static)

**Files:**
- Create: `Arena/FloorGenerator.h`, `Arena/FloorGenerator.cpp`
- Create: `Character/BeanAIController.h`, `Character/BeanAIController.cpp`
- Create: `Bots/ScriptedBot.h`, `Bots/ScriptedBot.cpp`
- Modify: `Character/BeanCharacter.h` (add `IsWalking()`)
- Test: `Tests/CombatBodyTests.cpp`

**Interfaces:**
- Produces:
  - `FMapSpec FPSArena::MakeOpenFloorSpec(int32 Tiles, const FString& Name)`.
  - `ABeanAIController` (keeps the control rotation it is given).
  - `AScriptedBot::Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed)`, `Deactivate()`, `IsActive()`, `IsAlive()`.
  - `bool ABeanCharacter::IsWalking() const`.

- [ ] **Step 1: Write the failing test** `Tests/CombatBodyTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Character/BeanAIController.h"
#include "Tests/FPSTestWorld.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSOpenFloorTest, "FPSAgent.Arena.OpenFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSOpenFloorTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = FPSArena::MakeOpenFloorSpec(3, TEXT("floor3"));
	TestEqual(TEXT("name"), S.Name, FString(TEXT("floor3")));
	TestEqual(TEXT("rows = tiles + wall ring"), S.Rows(), 5);
	TestEqual(TEXT("cols"), S.Cols(), 5);
	TestEqual(TEXT("top wall row"), S.Tiles[0], FString(TEXT("#####")));
	TestEqual(TEXT("inner row"), S.Tiles[2], FString(TEXT("#...#")));
	TestTrue(TEXT("inside walkable"), S.IsWalkable(2, 2));
	TestFalse(TEXT("ring not walkable"), S.IsWalkable(0, 2));
	TestEqual(TEXT("level 0"), S.LevelAt(2, 2), 0);
	TestTrue(TEXT("centre"), S.Center().Equals(FVector(1000.0, 1000.0, 0.0), 1e-3));
	TestTrue(TEXT("builds blocks"), FPSArena::ComputeMapBlocks(S).Num() > 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSAIControllerAimTest, "FPSAgent.Character.AIControllerKeepsAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSAIControllerAimTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	ABeanCharacter* Bean = W.SpawnBean(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	ABeanAIController* C = W.World->SpawnActor<ABeanAIController>();
	C->Possess(Bean);
	C->SetControlRotation(FRotator(-20.0, 30.0, 0.0));
	C->UpdateControlRotation(1.f / 60.f, true); // AAIController would reset the pitch to 0 here
	TestEqual(TEXT("pitch kept"), static_cast<float>(FRotator::NormalizeAxis(C->GetControlRotation().Pitch)), -20.f, 1e-3f);
	TestEqual(TEXT("yaw kept"), static_cast<float>(C->GetControlRotation().Yaw), 30.f, 1e-3f);
	TestEqual(TEXT("pawn turned to the yaw"), static_cast<float>(Bean->GetActorRotation().Yaw), 30.f, 1e-3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSScriptedBotTest, "FPSAgent.Bots.ScriptedBot.Lifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSScriptedBotTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AScriptedBot* Bot = W.World->SpawnActor<AScriptedBot>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, P);
	Bot->Deactivate();
	TestFalse(TEXT("inactive"), Bot->IsActive());
	TestTrue(TEXT("inactive bots are hidden"), Bot->IsHidden());

	Bot->Activate(FVector(500.0, 0.0, 90.0), 180.f, true, 3);
	TestTrue(TEXT("active"), Bot->IsActive());
	TestTrue(TEXT("alive"), Bot->IsAlive());
	TestFalse(TEXT("visible"), Bot->IsHidden());
	TestTrue(TEXT("placed"), Bot->GetActorLocation().Equals(FVector(500.0, 0.0, 90.0), 1.0));
	TestEqual(TEXT("faces yaw 180"), FMath::Abs(static_cast<float>(FRotator::NormalizeAxis(Bot->GetActorRotation().Yaw))), 180.f, 1e-3f);
	TestTrue(TEXT("wants to crouch"), Bot->GetCharacterMovement()->bWantsToCrouch);

	Bot->GetHealth()->ApplyHit(500.f, nullptr);
	TestFalse(TEXT("dead"), Bot->IsAlive());
	TestTrue(TEXT("dead bots hide"), Bot->IsHidden());
	TestFalse(TEXT("dead bots have no collision"), Bot->GetActorEnableCollision());

	Bot->Activate(FVector(0.0, 500.0, 90.0), 0.f, false, 4);
	TestTrue(TEXT("alive again next episode"), Bot->IsAlive());
	TestEqual(TEXT("full health"), Bot->GetHealth()->GetHealth(), 100.f);
	TestFalse(TEXT("standing"), Bot->GetCharacterMovement()->bWantsToCrouch);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Arena/FloorGenerator.h'`.

- [ ] **Step 3: Implement the floor.** `Arena/FloorGenerator.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Arena/MapSpec.h"

namespace FPSArena
{
	/**
	 * Pure: an open floor of Tiles x Tiles walkable tiles (level 0) inside a one-tile wall ring, as a format v1 spec
	 * (README -> Maps). Used for S0 arenas; build B adds cover. Built with FPSArena::BuildMap like any other map.
	 */
	FPSAGENT_API FMapSpec MakeOpenFloorSpec(int32 Tiles, const FString& Name);
}
```

`Arena/FloorGenerator.cpp`:

```cpp
#include "Arena/FloorGenerator.h"

namespace FPSArena
{
	FMapSpec MakeOpenFloorSpec(int32 Tiles, const FString& Name)
	{
		FMapSpec S;
		S.Name = Name;
		const int32 N = FMath::Max(1, Tiles) + 2;
		for (int32 R = 0; R < N; ++R)
		{
			const bool bRing = (R == 0 || R == N - 1);
			S.Tiles.Add(bRing ? FString::ChrN(N, TEXT('#')) : TEXT("#") + FString::ChrN(N - 2, TEXT('.')) + TEXT("#"));
			S.Heights.Add(FString::ChrN(N, TEXT('0')));
			S.Zones.Add(FString::ChrN(N, TEXT('.')));
		}
		return S;
	}
}
```

- [ ] **Step 4: Implement the controller.** `Character/BeanAIController.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "BeanAIController.generated.h"

/**
 * AI controller for agents and scripted bots. AAIController::UpdateControlRotation resets the pitch to 0 every tick
 * (and the yaw to the pawn's) unless it focuses a pawn. Our agents and bots aim by setting the control rotation
 * themselves, so this keeps it and only turns the pawn's yaw to match (the bean uses the controller yaw).
 */
UCLASS()
class FPSAGENT_API ABeanAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABeanAIController(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;
};
```

`Character/BeanAIController.cpp`:

```cpp
#include "Character/BeanAIController.h"
#include "GameFramework/Pawn.h"

ABeanAIController::ABeanAIController(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bSetControlRotationFromPawnOrientation = false;
}

void ABeanAIController::UpdateControlRotation(float DeltaTime, bool bUpdatePawn)
{
	if (APawn* MyPawn = GetPawn(); MyPawn && bUpdatePawn)
	{
		MyPawn->FaceRotation(GetControlRotation(), DeltaTime);
	}
}
```

- [ ] **Step 5: Implement the bot.** In `Character/BeanCharacter.h`, after `void SetWalking(bool bWalk);`, add `bool IsWalking() const { return bWalking; }`.

`Bots/ScriptedBot.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Character/BeanCharacter.h"
#include "ScriptedBot.generated.h"

class UHealthComponent;

/**
 * Training opponent (spec §4). Spawned once per arena and re-used: Activate() per episode, Deactivate() when unused.
 * In training a dead bot stays dead until the next Activate(). Build A: Static behaviour only (standing or crouched).
 */
UCLASS()
class FPSAGENT_API AScriptedBot : public ABeanCharacter
{
	GENERATED_BODY()

public:
	AScriptedBot(const FObjectInitializer& ObjectInitializer);

	/** New episode: teleport, face YawDeg, full health, alive, crouched or standing. Seed feeds the bot's rifle. */
	void Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed);
	/** Not in this episode: hidden, no collision, no movement. */
	void Deactivate();
	bool IsActive() const { return bActive; }
	bool IsAlive() const;

	virtual void PostInitializeComponents() override;

private:
	void HandleDied(UHealthComponent* Victim, AActor* Killer);

	bool bActive = false;
};
```

`Bots/ScriptedBot.cpp`:

```cpp
#include "Bots/ScriptedBot.h"
#include "Camera/CameraComponent.h"
#include "Character/BeanAIController.h"
#include "Character/HealthComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Weapons/RifleComponent.h"

AScriptedBot::AScriptedBot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AutoPossessAI = EAutoPossessAI::Spawned;
	AIControllerClass = ABeanAIController::StaticClass();
	Camera->SetAutoActivate(false);
}

void AScriptedBot::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Bound here (not BeginPlay) so it also works in the automation test world, where actors never BeginPlay.
	GetHealth()->OnDied.AddUObject(this, &AScriptedBot::HandleDied);
}

void AScriptedBot::Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed)
{
	bActive = true;
	const FRotator Facing(0.0, YawDeg, 0.0);
	SetActorLocationAndRotation(Location, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = GetController())
	{
		C->SetControlRotation(Facing);
	}
	GetHealth()->ResetToFull();
	GetRifle()->ResetWeapon();
	GetRifle()->SeedRandom(Seed);
	SetDeadState(false);
	GetCharacterMovement()->StopMovementImmediately();
	if (bCrouched) { Crouch(); } else { UnCrouch(); }
}

void AScriptedBot::Deactivate()
{
	bActive = false;
	SetDeadState(true);
}

bool AScriptedBot::IsAlive() const
{
	return bActive && !GetHealth()->IsDead();
}

void AScriptedBot::HandleDied(UHealthComponent* Victim, AActor* Killer)
{
	SetDeadState(true); // stays dead until the next Activate (spec §4)
}
```

- [ ] **Step 6: Build and run the tests.**
  - Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Arena.OpenFloor"`, `-Filter "FPSAgent.Character.AIControllerKeepsAim"` and `-Filter "FPSAgent.Bots.ScriptedBot"`.
  - Expected: each `Failed: 0`.
  - If "pawn turned to the yaw" fails: `FaceRotation` relies on `bUseControllerRotationYaw = true` (set in `ABeanCharacter`'s constructor). Check it wasn't changed.

- [ ] **Step 7: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Arena/FloorGenerator.* Game/FPSAgent/Source/FPSAgent/Character/BeanAIController.* Game/FPSAgent/Source/FPSAgent/Character/BeanCharacter.h Game/FPSAgent/Source/FPSAgent/Bots/ScriptedBot.* Game/FPSAgent/Source/FPSAgent/Tests/CombatBodyTests.cpp
git commit -m "[local] Open-floor spec generator, aim-keeping BeanAIController, AScriptedBot (Static; activate/deactivate per episode, dead stays dead)"
git pull --rebase && git push
```

---

### Task 7: Observation packing, actions and the scripted reference agent

**Files:**
- Create: `Learning/CombatObservation.h`, `Learning/CombatObservation.cpp`
- Test: `Tests/CombatObservationTests.cpp`

**Interfaces:**
- Consumes: `FCombatPerceptionSettings` (Task 1).
- Produces (namespace `FPSCombatObs`):
  - Sizes: `SelfSize = 15`, `EnemySize = 13`, `HitSize = 4`, `SoundSize = 5`.
  - `struct FSelfState`, `TArray<float> PackSelf(const FSelfState&, const FCombatPerceptionSettings&)`.
  - `struct FEnemyState`, `TArray<float> PackEnemy(const FEnemyState&, const FCombatPerceptionSettings&)`.
  - `TArray<float> PackHit(const FVector& DirectionView, float SecondsAgo, const FCombatPerceptionSettings&)`.
  - `struct FObservationData { TArray<float> Self, Rays; TArray<TArray<float>> Enemies; bool bHit; TArray<float> Hit; TArray<TArray<float>> Sounds; static FObservationData Zero(int32 RayCount); }`.
  - `struct FAgentAction { FVector2D Move, Aim; bool bFire, bReload, bCrouch, bWalk, bJump; }`, `FAgentAction Sanitize(const FAgentAction&)`.
  - `FVector2D AimPerTickDeg(const FVector2D& Aim, const FCombatPerceptionSettings&, float TickSeconds)`.
  - `FVector MoveWorld(const FVector2D& Move, float ViewYawDeg)`.
  - `struct FScriptedTarget { bool bHasTarget; FVector2D AimErrorDeg; float DistanceCm; }`.
  - `FAgentAction ScriptedAction(const FScriptedTarget&, int32 Ammo, int32 MagazineSize, bool bReloading, float HeadRadiusCm, const FCombatPerceptionSettings&, float DecisionSeconds)`.

- [ ] **Step 1: Write the failing test** `Tests/CombatObservationTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Learning/CombatObservation.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatObs;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsPackTest, "FPSAgent.Combat.Obs.Pack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsPackTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P; // position 3000, velocity 600, memory 2 s, hit 1 s
	FSelfState S;
	S.VelocityView = FVector(600.0, 0.0, 0.0);
	S.bGrounded = true;
	S.Health = 50.f; S.MaxHealth = 100.f; S.Armor = 25.f; S.MaxArmor = 50.f;
	S.Ammo = 5; S.MagazineSize = 25; S.bReloading = false;
	S.SpreadDeg = 2.5f; S.SprayShots = 3; S.SecondsSinceShot = 0.5f; S.AimPitchDeg = -45.f; S.TimeLeftFraction = 0.25f;
	const TArray<float> Self = PackSelf(S, P);
	const TArray<float> Expected = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.5f, 0.5f, 0.2f, 0.f, 0.25f, 0.12f, 0.5f, -0.5f, 0.25f };
	TestEqual(TEXT("self size"), Self.Num(), SelfSize);
	for (int32 i = 0; i < Expected.Num() && i < Self.Num(); ++i) { TestEqual(FString::Printf(TEXT("self[%d]"), i), Self[i], Expected[i], 1e-5f); }

	FEnemyState E;
	E.HeadView = FVector(3000.0, 0.0, 0.0);
	E.AimErrorDeg = FVector2D(90.0, -45.0);
	E.bVisible = true;
	E.SecondsSinceSeen = 1.f;
	const TArray<float> Enemy = PackEnemy(E, P);
	TestEqual(TEXT("enemy size"), Enemy.Num(), EnemySize);
	TestEqual(TEXT("head x"), Enemy[0], 1.f, 1e-5f);
	TestEqual(TEXT("distance"), Enemy[3], 1.f, 1e-5f);
	TestEqual(TEXT("aim yaw"), Enemy[4], 0.5f, 1e-5f);
	TestEqual(TEXT("aim pitch"), Enemy[5], -0.25f, 1e-5f);
	TestEqual(TEXT("visible"), Enemy[11], 1.f);
	TestEqual(TEXT("age / memory"), Enemy[12], 0.5f, 1e-5f);

	const TArray<float> Hit = PackHit(FVector(0.0, 1.0, 0.0), 0.5f, P);
	TestEqual(TEXT("hit size"), Hit.Num(), HitSize);
	TestEqual(TEXT("hit dir y"), Hit[1], 1.f, 1e-5f);
	TestEqual(TEXT("hit age"), Hit[3], 0.5f, 1e-5f);

	const FObservationData Z = FObservationData::Zero(72);
	TestEqual(TEXT("zero self"), Z.Self.Num(), SelfSize);
	TestEqual(TEXT("zero rays"), Z.Rays.Num(), 72);
	TestEqual(TEXT("zero enemies"), Z.Enemies.Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsActionTest, "FPSAgent.Combat.Obs.Action",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsActionTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P; // 720 deg/s
	FAgentAction A;
	A.Move = FVector2D(3.0, 4.0);
	A.Aim = FVector2D(2.0, -3.0);
	const FAgentAction C = Sanitize(A);
	TestTrue(TEXT("move clamped to length 1"), C.Move.Equals(FVector2D(0.6, 0.8), 1e-5));
	TestTrue(TEXT("aim clamped to [-1, 1]"), C.Aim.Equals(FVector2D(1.0, -1.0), 1e-5));

	const FVector2D Step = AimPerTickDeg(FVector2D(0.5, -1.0), P, 1.f / 60.f);
	TestEqual(TEXT("half speed yaw per tick"), static_cast<float>(Step.X), 6.f, 1e-4f);
	TestEqual(TEXT("full speed pitch per tick"), static_cast<float>(Step.Y), -12.f, 1e-4f);

	TestTrue(TEXT("forward at yaw 90 = world +Y"), MoveWorld(FVector2D(1.0, 0.0), 90.f).Equals(FVector(0.0, 1.0, 0.0), 1e-4));
	TestTrue(TEXT("right at yaw 0 = world +Y"), MoveWorld(FVector2D(0.0, 1.0), 0.f).Equals(FVector(0.0, 1.0, 0.0), 1e-4));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatObsScriptedTest, "FPSAgent.Combat.Obs.Scripted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatObsScriptedTest::RunTest(const FString& Parameters)
{
	const FCombatPerceptionSettings P;      // 720 deg/s
	const float Decision = 4.f / 60.f;       // 48 deg per decision
	FScriptedTarget Far;
	Far.bHasTarget = true; Far.AimErrorDeg = FVector2D(10.0, 0.0); Far.DistanceCm = 2000.f;
	const FAgentAction Turn = ScriptedAction(Far, 25, 25, false, 12.f, P, Decision);
	TestEqual(TEXT("turns 10 of 48 degrees"), static_cast<float>(Turn.Aim.X), 10.f / 48.f, 1e-4f);
	TestFalse(TEXT("does not fire off target"), Turn.bFire);

	FScriptedTarget On;
	On.bHasTarget = true; On.AimErrorDeg = FVector2D(0.1, 0.05); On.DistanceCm = 2000.f; // head is 0.34 deg wide at 20 m
	TestTrue(TEXT("fires on target"), ScriptedAction(On, 25, 25, false, 12.f, P, Decision).bFire);
	TestFalse(TEXT("no fire while reloading"), ScriptedAction(On, 25, 25, true, 12.f, P, Decision).bFire);
	const FAgentAction Empty = ScriptedAction(On, 0, 25, false, 12.f, P, Decision);
	TestTrue(TEXT("reloads when empty"), Empty.bReload);
	TestFalse(TEXT("no fire when empty"), Empty.bFire);

	const FAgentAction Scan = ScriptedAction(FScriptedTarget(), 25, 25, false, 12.f, P, Decision);
	TestEqual(TEXT("no target: scan right"), static_cast<float>(Scan.Aim.X), 1.f);
	TestFalse(TEXT("no target: no fire"), Scan.bFire);
	TestTrue(TEXT("no target, half empty: reload"), ScriptedAction(FScriptedTarget(), 10, 25, false, 12.f, P, Decision).bReload);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Learning/CombatObservation.h'`.

- [ ] **Step 3: Implement** `Learning/CombatObservation.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/**
 * The agent's observation and action layout (spec §2), as plain float arrays the Learning Agents layer wraps. Pure.
 * Self, enemy, hit and sound vectors have fixed sizes; changing them changes the network layout (no resume across).
 */
namespace FPSCombatObs
{
	inline constexpr int32 SelfSize = 15;
	inline constexpr int32 EnemySize = 13;
	inline constexpr int32 HitSize = 4;
	inline constexpr int32 SoundSize = 5;

	struct FSelfState
	{
		FVector VelocityView = FVector::ZeroVector;
		bool bGrounded = true;
		bool bCrouched = false;
		bool bWalking = false;
		float Health = 0.f;
		float MaxHealth = 100.f;
		float Armor = 0.f;
		float MaxArmor = 50.f;
		int32 Ammo = 0;
		int32 MagazineSize = 25;
		bool bReloading = false;
		float SpreadDeg = 0.f;
		int32 SprayShots = 0;
		float SecondsSinceShot = 1.f;
		float AimPitchDeg = 0.f;
		float TimeLeftFraction = 1.f;
	};
	/** [vel x,y,z / VelocityScale, grounded, crouched, walking, health, armor, ammo (fractions), reloading,
	 *   spread / 10 deg, spray shots / magazine, min(time since shot, 1 s), pitch / 90, time left]. */
	FPSAGENT_API TArray<float> PackSelf(const FSelfState& S, const FCombatPerceptionSettings& P);

	struct FEnemyState
	{
		/** Head position relative to the agent's eye, in the view frame (cm). */
		FVector HeadView = FVector::ZeroVector;
		FVector2D AimErrorDeg = FVector2D::ZeroVector;
		FVector VelocityView = FVector::ZeroVector;
		bool bFacingAgent = false;
		bool bCrouched = false;
		bool bVisible = false;
		float SecondsSinceSeen = 0.f;
	};
	/** [head x,y,z / PositionScale, distance / PositionScale, aim yaw, pitch / 180, vel x,y,z / VelocityScale,
	 *   facing, crouched, visible, min(age / MemorySeconds, 1)]. */
	FPSAGENT_API TArray<float> PackEnemy(const FEnemyState& E, const FCombatPerceptionSettings& P);
	/** [direction x,y,z (view frame, unit), min(age / HitIndicatorSeconds, 1)]. */
	FPSAGENT_API TArray<float> PackHit(const FVector& DirectionView, float SecondsAgo, const FCombatPerceptionSettings& P);

	/** One decision's observation, ready for the Learning Agents layer. */
	struct FObservationData
	{
		TArray<float> Self;
		TArray<float> Rays;
		TArray<TArray<float>> Enemies;
		bool bHit = false;
		TArray<float> Hit;
		TArray<TArray<float>> Sounds;

		/** All zeros with the right sizes (fallback when an agent id is unknown). */
		static FObservationData Zero(int32 RayCount);
	};

	/** One decision's action. Move/Aim in [-1, 1]; held for the next TicksPerDecision ticks. */
	struct FAgentAction
	{
		FVector2D Move = FVector2D::ZeroVector;
		FVector2D Aim = FVector2D::ZeroVector;
		bool bFire = false;
		bool bReload = false;
		bool bCrouch = false;
		bool bWalk = false;
		bool bJump = false;
	};
	/** Policy samples can leave [-1, 1]: clamp Aim per axis and Move to length 1. */
	FPSAGENT_API FAgentAction Sanitize(const FAgentAction& A);
	/** Aim change per tick in degrees (yaw, pitch): Aim * MaxTurnRate * TickSeconds (spread evenly, never snapped). */
	FPSAGENT_API FVector2D AimPerTickDeg(const FVector2D& Aim, const FCombatPerceptionSettings& P, float TickSeconds);
	/** View-frame move input (forward, right) to a world direction (Z = 0). */
	FPSAGENT_API FVector MoveWorld(const FVector2D& Move, float ViewYawDeg);

	/** The nearest visible enemy for the scripted reference agent. */
	struct FScriptedTarget
	{
		bool bHasTarget = false;
		FVector2D AimErrorDeg = FVector2D::ZeroVector;
		float DistanceCm = 0.f;
	};
	/**
	 * -Scripted reference agent for S0: turn toward the target at the turn-rate cap; fire when the aim error is within
	 * the head's angular radius (at least 0.25 deg); scan right when nothing is visible; reload when empty (or below half
	 * with nothing in sight). Uses the same senses as the policy, nothing more.
	 */
	FPSAGENT_API FAgentAction ScriptedAction(const FScriptedTarget& Target, int32 Ammo, int32 MagazineSize, bool bReloading,
		float HeadRadiusCm, const FCombatPerceptionSettings& P, float DecisionSeconds);
}
```

`Learning/CombatObservation.cpp`:

```cpp
#include "Learning/CombatObservation.h"

namespace
{
	/** Observation normalizers that aren't mechanics: spread in units of 10 deg, pitch in units of 90 deg, time since shot capped at 1 s. */
	constexpr float SpreadScaleDeg = 10.f;
	constexpr float PitchScaleDeg = 90.f;
	constexpr float AngleScaleDeg = 180.f;
	constexpr float MinFireToleranceDeg = 0.25f;

	float Safe(float Num, float Den) { return Den > 0.f ? Num / Den : 0.f; }
}

namespace FPSCombatObs
{
	TArray<float> PackSelf(const FSelfState& S, const FCombatPerceptionSettings& P)
	{
		const FVector V = S.VelocityView / FMath::Max(1.f, P.VelocityScaleCmPerS);
		return {
			static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z),
			S.bGrounded ? 1.f : 0.f, S.bCrouched ? 1.f : 0.f, S.bWalking ? 1.f : 0.f,
			Safe(S.Health, S.MaxHealth), Safe(S.Armor, S.MaxArmor), Safe(static_cast<float>(S.Ammo), static_cast<float>(S.MagazineSize)),
			S.bReloading ? 1.f : 0.f,
			S.SpreadDeg / SpreadScaleDeg,
			Safe(static_cast<float>(S.SprayShots), static_cast<float>(S.MagazineSize)),
			FMath::Clamp(S.SecondsSinceShot, 0.f, 1.f),
			S.AimPitchDeg / PitchScaleDeg,
			FMath::Clamp(S.TimeLeftFraction, 0.f, 1.f) };
	}

	TArray<float> PackEnemy(const FEnemyState& E, const FCombatPerceptionSettings& P)
	{
		const float Pos = FMath::Max(1.f, P.PositionScaleCm);
		const FVector H = E.HeadView / Pos;
		const FVector V = E.VelocityView / FMath::Max(1.f, P.VelocityScaleCmPerS);
		return {
			static_cast<float>(H.X), static_cast<float>(H.Y), static_cast<float>(H.Z),
			static_cast<float>(E.HeadView.Size() / Pos),
			static_cast<float>(E.AimErrorDeg.X / AngleScaleDeg), static_cast<float>(E.AimErrorDeg.Y / AngleScaleDeg),
			static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z),
			E.bFacingAgent ? 1.f : 0.f, E.bCrouched ? 1.f : 0.f, E.bVisible ? 1.f : 0.f,
			FMath::Clamp(Safe(E.SecondsSinceSeen, P.MemorySeconds), 0.f, 1.f) };
	}

	TArray<float> PackHit(const FVector& DirectionView, float SecondsAgo, const FCombatPerceptionSettings& P)
	{
		const FVector D = DirectionView.GetSafeNormal();
		return { static_cast<float>(D.X), static_cast<float>(D.Y), static_cast<float>(D.Z),
			FMath::Clamp(Safe(SecondsAgo, P.HitIndicatorSeconds), 0.f, 1.f) };
	}

	FObservationData FObservationData::Zero(int32 RayCount)
	{
		FObservationData D;
		D.Self.Init(0.f, SelfSize);
		D.Rays.Init(0.f, FMath::Max(0, RayCount));
		return D;
	}

	FAgentAction Sanitize(const FAgentAction& A)
	{
		FAgentAction C = A;
		C.Aim = FVector2D(FMath::Clamp(A.Aim.X, -1.0, 1.0), FMath::Clamp(A.Aim.Y, -1.0, 1.0));
		C.Move = A.Move.GetClampedToMaxSize(1.0);
		return C;
	}

	FVector2D AimPerTickDeg(const FVector2D& Aim, const FCombatPerceptionSettings& P, float TickSeconds)
	{
		return Aim * (P.MaxTurnRateDegPerSec * TickSeconds);
	}

	FVector MoveWorld(const FVector2D& Move, float ViewYawDeg)
	{
		const FRotator Yaw(0.0, ViewYawDeg, 0.0);
		return Yaw.RotateVector(FVector(Move.X, Move.Y, 0.0));
	}

	FAgentAction ScriptedAction(const FScriptedTarget& Target, int32 Ammo, int32 MagazineSize, bool bReloading,
		float HeadRadiusCm, const FCombatPerceptionSettings& P, float DecisionSeconds)
	{
		FAgentAction A;
		const float MaxPerDecision = FMath::Max(1e-3f, P.MaxTurnRateDegPerSec * DecisionSeconds);
		if (!Target.bHasTarget)
		{
			A.Aim = FVector2D(1.0, 0.0);
			A.bReload = !bReloading && Ammo < MagazineSize / 2;
			return A;
		}
		A.Aim = FVector2D(FMath::Clamp(Target.AimErrorDeg.X / MaxPerDecision, -1.0, 1.0), FMath::Clamp(Target.AimErrorDeg.Y / MaxPerDecision, -1.0, 1.0));
		const float ToleranceDeg = FMath::Max(MinFireToleranceDeg, FMath::RadiansToDegrees(FMath::Atan2(HeadRadiusCm, FMath::Max(1.f, Target.DistanceCm))));
		const bool bOnTarget = FMath::Abs(Target.AimErrorDeg.X) <= ToleranceDeg && FMath::Abs(Target.AimErrorDeg.Y) <= ToleranceDeg;
		A.bReload = !bReloading && Ammo <= 0;
		A.bFire = bOnTarget && !bReloading && Ammo > 0;
		return A;
	}
}
```

- [ ] **Step 4: Build and run the tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Combat.Obs"`. Expected: `Succeeded: 3  Failed: 0`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatObservation.* Game/FPSAgent/Source/FPSAgent/Tests/CombatObservationTests.cpp
git commit -m "[local] Combat observation packing (self 15, enemy 13, hit 4), action sanitize/aim-per-tick/move, scripted S0 reference agent"
git pull --rebase && git push
```

---

### Task 8: Stats window, log line, CSV and eval JSON

**Files:**
- Create: `Learning/CombatStats.h`, `Learning/CombatStats.cpp`
- Test: `Tests/CombatStatsTests.cpp`

**Interfaces:**
- Consumes: `FPSCombatReward::TermCount`, `TermName` (Task 3).
- Produces (namespace `FPSCombatStats`):
  - `struct FEpisodeRecord { bool bCleared, bDied, bTimedOut, bReview; float Level, Return; float Terms[TermCount]; int32 Kills, Bots, Shots, Hits, Headshots; float Seconds; }`.
  - `struct FWindow { void Add(const FEpisodeRecord&); void Reset(); double ClearRate() const; ... }`.
  - `struct FLevelSummary { float Mean, Min, Max; }`.
  - `FString LogLine(const FWindow&, double GameTime, const FLevelSummary&, double DecisionsPerSecond)`, `FString CsvHeader()`, `FString CsvRow(...)` (same arguments as `LogLine`), `FString EvalJson(const FWindow&, const FString& Stage, float Level, int32 Seed, const FString& Snapshots, float PromotionClearRate)`.

- [ ] **Step 1: Write the failing test** `Tests/CombatStatsTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Dom/JsonObject.h"
#include "Learning/CombatStats.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSCombatStats;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatStatsTest, "FPSAgent.Combat.Stats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatStatsTest::RunTest(const FString& Parameters)
{
	FEpisodeRecord A;
	A.bCleared = true; A.Return = 3.f; A.Shots = 10; A.Hits = 4; A.Headshots = 1; A.Kills = 2; A.Bots = 2; A.Seconds = 8.f;
	A.Terms[static_cast<int32>(FPSCombatReward::ETerm::Kill)] = 2.f;
	FEpisodeRecord B;
	B.bDied = true; B.Return = -1.f; B.Shots = 5; B.Hits = 1; B.Seconds = 4.f;
	FWindow W;
	W.Add(A);
	W.Add(B);
	TestEqual(TEXT("episodes"), W.Episodes, 2);
	TestEqual(TEXT("clear rate"), static_cast<float>(W.ClearRate()), 0.5f, 1e-6f);
	TestEqual(TEXT("death rate"), static_cast<float>(W.DeathRate()), 0.5f, 1e-6f);
	TestEqual(TEXT("avg return"), static_cast<float>(W.AvgReturn()), 1.f, 1e-6f);
	TestEqual(TEXT("accuracy = hits / shots"), static_cast<float>(W.Accuracy()), 5.f / 15.f, 1e-6f);
	TestEqual(TEXT("headshot rate = headshots / hits"), static_cast<float>(W.HeadshotRate()), 1.f / 5.f, 1e-6f);
	TestEqual(TEXT("clear time counts cleared episodes only"), static_cast<float>(W.AvgClearSeconds()), 8.f, 1e-6f);
	TestEqual(TEXT("avg kill term"), static_cast<float>(W.AvgTerm(static_cast<int32>(FPSCombatReward::ETerm::Kill))), 1.f, 1e-6f);

	const FLevelSummary L{ 0.3f, 0.1f, 0.5f };
	TArray<FString> Header, Row;
	CsvHeader().ParseIntoArray(Header, TEXT(","), false);
	CsvRow(W, 12.0, L, 900.0).ParseIntoArray(Row, TEXT(","), false);
	TestEqual(TEXT("csv columns match"), Row.Num(), Header.Num());
	TestTrue(TEXT("csv has every term"), CsvHeader().Contains(TEXT("term_AimHelper")));
	TestTrue(TEXT("log line prefix"), LogLine(W, 12.0, L, 900.0).StartsWith(TEXT("CombatStats: ")));

	TSharedPtr<FJsonObject> Obj;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(EvalJson(W, TEXT("S0"), 1.f, 7, TEXT("/snap"), 0.7f));
	TestTrue(TEXT("eval json parses"), FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid());
	if (Obj.IsValid())
	{
		TestEqual(TEXT("eval episodes"), static_cast<int32>(Obj->GetNumberField(TEXT("episodes"))), 2);
		TestEqual(TEXT("eval clear rate"), static_cast<float>(Obj->GetNumberField(TEXT("clearRate"))), 0.5f, 1e-6f);
		TestFalse(TEXT("0.5 < 0.7 gate: not passed"), Obj->GetBoolField(TEXT("passed")));
		TestEqual(TEXT("stage"), Obj->GetStringField(TEXT("stage")), FString(TEXT("S0")));
		TestTrue(TEXT("terms object"), Obj->HasField(TEXT("terms")));
	}
	W.Reset();
	TestEqual(TEXT("reset"), W.Episodes, 0);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Learning/CombatStats.h'`.

- [ ] **Step 3: Implement** `Learning/CombatStats.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Learning/CombatReward.h"

/** Episode records, stats windows, the log line, -StatsCsv rows and the -Eval JSON (spec §1 point 5, §6). Pure. */
namespace FPSCombatStats
{
	struct FEpisodeRecord
	{
		bool bCleared = false;
		bool bDied = false;
		bool bTimedOut = false;
		bool bReview = false;
		float Level = 0.f;
		float Return = 0.f;
		float Terms[FPSCombatReward::TermCount] = {};
		int32 Kills = 0;
		int32 Bots = 0;
		int32 Shots = 0;
		int32 Hits = 0;
		int32 Headshots = 0;
		float Seconds = 0.f;
	};

	struct FPSAGENT_API FWindow
	{
		int32 Episodes = 0;
		int32 Clears = 0;
		int32 Deaths = 0;
		int32 Timeouts = 0;
		double ReturnSum = 0.0;
		double TermSums[FPSCombatReward::TermCount] = {};
		double ClearSecondsSum = 0.0;
		int64 Kills = 0;
		int64 Shots = 0;
		int64 Hits = 0;
		int64 Headshots = 0;

		void Add(const FEpisodeRecord& R);
		void Reset() { *this = FWindow(); }
		double ClearRate() const;
		double DeathRate() const;
		double TimeoutRate() const;
		double AvgReturn() const;
		double AvgTerm(int32 Term) const;
		/** Hits / shots: a metric, never a reward. */
		double Accuracy() const;
		/** Headshots / hits. */
		double HeadshotRate() const;
		double AvgClearSeconds() const;
		double KillsPerEpisode() const;
	};

	struct FLevelSummary
	{
		float Mean = 0.f;
		float Min = 0.f;
		float Max = 0.f;
	};

	FPSAGENT_API FString LogLine(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond);
	FPSAGENT_API FString CsvHeader();
	FPSAGENT_API FString CsvRow(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond);
	FPSAGENT_API FString EvalJson(const FWindow& W, const FString& Stage, float Level, int32 Seed, const FString& Snapshots, float PromotionClearRate);
}
```

`Learning/CombatStats.cpp`:

```cpp
#include "Learning/CombatStats.h"
#include "Dom/JsonObject.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

namespace
{
	double Ratio(double Num, double Den) { return Den > 0.0 ? Num / Den : 0.0; }
}

namespace FPSCombatStats
{
	void FWindow::Add(const FEpisodeRecord& R)
	{
		++Episodes;
		Clears += R.bCleared ? 1 : 0;
		Deaths += R.bDied ? 1 : 0;
		Timeouts += R.bTimedOut ? 1 : 0;
		ReturnSum += R.Return;
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i) { TermSums[i] += R.Terms[i]; }
		ClearSecondsSum += R.bCleared ? R.Seconds : 0.0;
		Kills += R.Kills;
		Shots += R.Shots;
		Hits += R.Hits;
		Headshots += R.Headshots;
	}

	double FWindow::ClearRate() const { return Ratio(Clears, Episodes); }
	double FWindow::DeathRate() const { return Ratio(Deaths, Episodes); }
	double FWindow::TimeoutRate() const { return Ratio(Timeouts, Episodes); }
	double FWindow::AvgReturn() const { return Ratio(ReturnSum, Episodes); }
	double FWindow::AvgTerm(int32 Term) const { return Term >= 0 && Term < FPSCombatReward::TermCount ? Ratio(TermSums[Term], Episodes) : 0.0; }
	double FWindow::Accuracy() const { return Ratio(static_cast<double>(Hits), static_cast<double>(Shots)); }
	double FWindow::HeadshotRate() const { return Ratio(static_cast<double>(Headshots), static_cast<double>(Hits)); }
	double FWindow::AvgClearSeconds() const { return Ratio(ClearSecondsSum, Clears); }
	double FWindow::KillsPerEpisode() const { return Ratio(static_cast<double>(Kills), Episodes); }

	FString LogLine(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond)
	{
		FString Line = FString::Printf(
			TEXT("CombatStats: gameTime=%.0fs episodes=%d clear=%.1f%% died=%.1f%% timeout=%.1f%% avgReturn=%.3f kills/ep=%.2f acc=%.1f%% hs=%.1f%% clearTime=%.1fs level=%.2f(%.2f..%.2f) decisions/s(wall)=%.0f terms:"),
			GameTime, W.Episodes, 100.0 * W.ClearRate(), 100.0 * W.DeathRate(), 100.0 * W.TimeoutRate(), W.AvgReturn(),
			W.KillsPerEpisode(), 100.0 * W.Accuracy(), 100.0 * W.HeadshotRate(), W.AvgClearSeconds(), Level.Mean, Level.Min, Level.Max,
			DecisionsPerSecond);
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			Line += FString::Printf(TEXT(" %s=%.3f"), FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)), W.AvgTerm(i));
		}
		return Line;
	}

	FString CsvHeader()
	{
		FString H = TEXT("gameTime,episodes,clearRate,deathRate,timeoutRate,avgReturn,killsPerEpisode,accuracy,headshotRate,avgClearSeconds,levelMean,levelMin,levelMax,decisionsPerSecond");
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			H += FString::Printf(TEXT(",term_%s"), FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)));
		}
		return H;
	}

	FString CsvRow(const FWindow& W, double GameTime, const FLevelSummary& Level, double DecisionsPerSecond)
	{
		FString R = FString::Printf(TEXT("%.1f,%d,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.3f,%.3f,%.3f,%.3f,%.1f"),
			GameTime, W.Episodes, W.ClearRate(), W.DeathRate(), W.TimeoutRate(), W.AvgReturn(), W.KillsPerEpisode(), W.Accuracy(),
			W.HeadshotRate(), W.AvgClearSeconds(), Level.Mean, Level.Min, Level.Max, DecisionsPerSecond);
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			R += FString::Printf(TEXT(",%.4f"), W.AvgTerm(i));
		}
		return R;
	}

	FString EvalJson(const FWindow& W, const FString& Stage, float Level, int32 Seed, const FString& Snapshots, float PromotionClearRate)
	{
		const TSharedRef<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("stage"), Stage);
		Obj->SetNumberField(TEXT("level"), Level);
		Obj->SetNumberField(TEXT("seed"), Seed);
		Obj->SetStringField(TEXT("snapshots"), Snapshots);
		Obj->SetNumberField(TEXT("episodes"), W.Episodes);
		Obj->SetNumberField(TEXT("clearRate"), W.ClearRate());
		Obj->SetNumberField(TEXT("deathRate"), W.DeathRate());
		Obj->SetNumberField(TEXT("timeoutRate"), W.TimeoutRate());
		Obj->SetNumberField(TEXT("avgReturn"), W.AvgReturn());
		Obj->SetNumberField(TEXT("killsPerEpisode"), W.KillsPerEpisode());
		Obj->SetNumberField(TEXT("accuracy"), W.Accuracy());
		Obj->SetNumberField(TEXT("headshotRate"), W.HeadshotRate());
		Obj->SetNumberField(TEXT("avgClearSeconds"), W.AvgClearSeconds());
		Obj->SetNumberField(TEXT("promotionClearRate"), PromotionClearRate);
		Obj->SetBoolField(TEXT("passed"), W.Episodes > 0 && W.ClearRate() >= PromotionClearRate);
		const TSharedRef<FJsonObject> Terms = MakeShared<FJsonObject>();
		for (int32 i = 0; i < FPSCombatReward::TermCount; ++i)
		{
			Terms->SetNumberField(FPSCombatReward::TermName(static_cast<FPSCombatReward::ETerm>(i)), W.AvgTerm(i));
		}
		Obj->SetObjectField(TEXT("terms"), Terms);
		FString Out;
		const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, Writer);
		return Out;
	}
}
```

- [ ] **Step 4: Build and run the tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Combat.Stats"`. Expected: `Succeeded: 1  Failed: 0`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatStats.* Game/FPSAgent/Source/FPSAgent/Tests/CombatStatsTests.cpp
git commit -m "[local] Combat stats: episode records, windows, CombatStats log line, -StatsCsv rows, -Eval JSON with promotion gate"
git pull --rebase && git push
```

---
### Task 9: `UCombatArena`, one arena's gameplay

**Files:**
- Create: `Learning/CombatArena.h`, `Learning/CombatArena.cpp`
- Modify: `Weapons/CombatMath.cpp` (cap the reported health damage at the health that was left, Step 2b)
- Test: `Tests/CombatArenaTests.cpp`, `Tests/CombatMathTests.cpp`

**Interfaces:**
- Consumes:
  - Task 3 (`FLedger`), Task 4 (`FS0Episode`), Task 5 (`FPSPerception`), Task 6 (`MakeOpenFloorSpec`, `ABeanAIController`, `AScriptedBot`), Task 7 (`FPSCombatObs`), Task 8 (`FEpisodeRecord`).
  - `FPSReach::EpisodeSeed` (existing).
- Produces:
  - `enum class ECombatEpisodeEnd : uint8 { Running, Died, Cleared, TimedOut }`.
  - `UCombatArena` methods:
    - `void Build(UWorld*, int32 ArenaIndex, const FVector& Center, int32 BaseSeed)`, `int32 NextEpisodeSeed() const`.
    - `void StartEpisode(const FPSCurriculum::FS0Episode&, int32 Seed, double Now)`.
    - `void TickAction(float DeltaSeconds)`, `void PrepareDecision(double Now)`.
    - `ECombatEpisodeEnd GetEpisodeEnd(double Now) const`, `float TakeDecisionReward()`.
    - `FPSCombatStats::FEpisodeRecord FinishEpisode(ECombatEpisodeEnd, double Now) const`.
    - `void BuildObservation(FPSCombatObs::FObservationData&, double Now) const`.
    - `void SetAction(const FPSCombatObs::FAgentAction&)`, `FPSCombatObs::FAgentAction ScriptedAction(float DecisionSeconds) const`.
    - Accessors: `GetAgent()`, `GetBot(int32)`, `GetBotCount()`, `GetLedger()`, `GetCenter()`, `GetBotRadiusCm()`.
    - `static float FloorHalfSizeCm()`.

- [ ] **Step 1: Write the failing test** `Tests/CombatArenaTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Bots/ScriptedBot.h"
#include "Common/FPSAgentSettings.h"
#include "Learning/CombatArena.h"
#include "Tests/FPSTestWorld.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

using FPSCombatReward::ETerm;

namespace
{
	/** Agent at the origin facing +X; bot 0 1000 cm ahead, bot 1 1500 cm to the right (outside the 103 deg view). */
	FPSCurriculum::FS0Episode TwoBots()
	{
		FPSCurriculum::FS0Episode E;
		E.AgentYawDeg = 0.f;
		E.BotOffsets = { FVector2D(1000.0, 0.0), FVector2D(0.0, 1500.0) };
		E.BotCrouched = { false, false };
		return E;
	}

	void AimAtHead(UCombatArena* A, int32 Bot)
	{
		FVector Eye; FRotator Unused;
		A->GetAgent()->GetActorEyesViewPoint(Eye, Unused);
		const FVector Head = A->GetBot(Bot)->GetHitbox(EHitZone::Head)->GetComponentLocation();
		A->GetAgent()->GetController()->SetControlRotation((Head - Eye).Rotation());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaEpisodeTest, "FPSAgent.Combat.Arena.Episode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaEpisodeTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	const float SpawnZ = UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f;
	TestTrue(TEXT("agent at the centre"), A->GetAgent()->GetActorLocation().Equals(FVector(0.0, 0.0, SpawnZ), 1.0));
	TestEqual(TEXT("bot pool = max bots"), A->GetBotCount(), 5);
	TestTrue(TEXT("bot 0 placed"), A->GetBot(0)->GetActorLocation().Equals(FVector(1000.0, 0.0, SpawnZ), 1.0));
	TestTrue(TEXT("bot 0 alive"), A->GetBot(0)->IsAlive());
	TestTrue(TEXT("bot 1 active"), A->GetBot(1)->IsActive());
	TestFalse(TEXT("bot 2 unused"), A->GetBot(2)->IsActive());
	TestTrue(TEXT("running"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Running);
	TestTrue(TEXT("times out at the 20 s limit"), A->GetEpisodeEnd(20.0) == ECombatEpisodeEnd::TimedOut);
	TestTrue(TEXT("end check has no side effects (D-022)"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Running);

	A->PrepareDecision(0.1);
	TestEqual(TEXT("first sighting of the bot in view"), A->GetLedger().PendingTerm(ETerm::FirstSighting), 0.2f, 1e-6f);
	A->TakeDecisionReward();
	A->PrepareDecision(0.2);
	TestEqual(TEXT("no second sighting reward"), A->GetLedger().PendingTerm(ETerm::FirstSighting), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaClearTest, "FPSAgent.Combat.Arena.KillAndClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaClearTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	AimAtHead(A.Get(), 0);
	const FShotResult First = A->GetAgent()->GetRifle()->FireShot(0);
	TestTrue(TEXT("headshot kill"), First.bKill);
	TestEqual(TEXT("kill reward"), A->GetLedger().PendingTerm(ETerm::Kill), 1.f, 1e-6f);
	TestEqual(TEXT("damage = health + armor removed (150/150)"), A->GetLedger().PendingTerm(ETerm::DamageDealt), 1.f, 1e-5f);
	AimAtHead(A.Get(), 1);
	A->GetAgent()->GetRifle()->FireShot(0);
	TestTrue(TEXT("bot 1 dead"), !A->GetBot(1)->IsAlive());
	TestTrue(TEXT("cleared"), A->GetEpisodeEnd(5.0) == ECombatEpisodeEnd::Cleared);
	A->PrepareDecision(5.0);
	TestEqual(TEXT("clear bonus 0.5 + 1.5 * 15/20"), A->GetLedger().PendingTerm(ETerm::Clear), 0.5f + 1.5f * 0.75f, 1e-5f);
	A->TakeDecisionReward();
	A->PrepareDecision(6.0);
	TestEqual(TEXT("clear bonus paid once (D-022)"), A->GetLedger().PendingTerm(ETerm::Clear), 0.f);
	const FPSCombatStats::FEpisodeRecord R = A->FinishEpisode(ECombatEpisodeEnd::Cleared, 6.0);
	TestTrue(TEXT("record cleared"), R.bCleared);
	TestEqual(TEXT("kills"), R.Kills, 2);
	TestEqual(TEXT("shots"), R.Shots, 2);
	TestEqual(TEXT("hits"), R.Hits, 2);
	TestEqual(TEXT("headshots"), R.Headshots, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaDeathTest, "FPSAgent.Combat.Arena.DeathBeatsClear",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaDeathTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	A->GetBot(0)->GetHealth()->ApplyHit(500.f, nullptr);
	A->GetBot(1)->GetHealth()->ApplyHit(500.f, nullptr);
	A->GetAgent()->GetHealth()->ApplyHit(500.f, A->GetBot(0));
	TestTrue(TEXT("a dead agent beats a clear (spec §6)"), A->GetEpisodeEnd(3.0) == ECombatEpisodeEnd::Died);
	A->PrepareDecision(3.0);
	TestEqual(TEXT("no clear bonus"), A->GetLedger().PendingTerm(ETerm::Clear), 0.f);
	TestEqual(TEXT("death penalty"), A->GetLedger().PendingTerm(ETerm::Death), -1.f, 1e-6f);
	TestEqual(TEXT("damage taken (150 of 150)"), A->GetLedger().PendingTerm(ETerm::DamageTaken), -1.f, 1e-5f);
	TestEqual(TEXT("bot kills by others don't pay"), A->GetLedger().PendingTerm(ETerm::Kill), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaObservationTest, "FPSAgent.Combat.Arena.Observation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaObservationTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7);
	A->StartEpisode(TwoBots(), 11, 0.0);
	W.Step();
	A->GetAgent()->GetController()->SetControlRotation(FRotator(60.0, 0.0, 0.0)); // look at the sky
	A->GetAgent()->GetRifle()->FireShot(0);
	TestEqual(TEXT("miss penalty"), A->GetLedger().PendingTerm(ETerm::Miss), -0.01f, 1e-6f);
	A->GetAgent()->GetController()->SetControlRotation(FRotator::ZeroRotator);
	A->PrepareDecision(0.1);

	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 0.1);
	const FCombatPerceptionSettings& P = UFPSAgentSettings::Get().Combat.Perception;
	TestEqual(TEXT("self size"), O.Self.Num(), FPSCombatObs::SelfSize);
	TestEqual(TEXT("ray count"), O.Rays.Num(), P.RayYawCount * P.RayPitchesDeg.Num());
	bool bInRange = true;
	bool bSomeShort = false;
	for (const float F : O.Rays) { bInRange &= (F >= 0.f && F <= 1.f); bSomeShort |= F < 1.f; }
	TestTrue(TEXT("ray fractions in [0, 1]"), bInRange);
	TestTrue(TEXT("down rays hit the floor"), bSomeShort);
	TestEqual(TEXT("only the bot in view is observed"), O.Enemies.Num(), 1);
	if (O.Enemies.Num() == 1)
	{
		TestEqual(TEXT("enemy size"), O.Enemies[0].Num(), FPSCombatObs::EnemySize);
		TestEqual(TEXT("visible flag"), O.Enemies[0][11], 1.f);
	}
	TestFalse(TEXT("not hit"), O.bHit);
	TestEqual(TEXT("no sounds in S0"), O.Sounds.Num(), 0);

	const FPSCombatObs::FAgentAction Act = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("scripted agent turns toward the bot"), FMath::Abs(Act.Aim.X) < 1.0);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `cannot open include file: 'Learning/CombatArena.h'`.

- [ ] **Step 2b: Report only the damage actually removed (spec §3).** Today `FPSCombat::ApplyDamage` reports `DamageToHealth = Damage - DamageToArmor` even past 0 health: a 160 headshot on 100 + 50 reports 110 + 50. The damage-dealt reward must be "health plus armor actually removed", which is 1.0 for a kill from full.
  - In `Tests/CombatMathTests.cpp`, add a test:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSApplyDamageOverkillTest, "FPSAgent.Combat.ApplyDamageOverkill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSApplyDamageOverkillTest::RunTest(const FString& Parameters)
{
	const FPSCombat::FDamageResult R = FPSCombat::ApplyDamage(100.f, 50.f, 160.f, 1.f);
	TestEqual(TEXT("armor removed"), R.DamageToArmor, 50.f);
	TestEqual(TEXT("health removed is capped at what was left"), R.DamageToHealth, 100.f);
	TestTrue(TEXT("killed"), R.bKilled);
	const FPSCombat::FDamageResult Partial = FPSCombat::ApplyDamage(100.f, 0.f, 40.f, 1.f);
	TestEqual(TEXT("non-lethal hit unchanged"), Partial.DamageToHealth, 40.f);
	return true;
}
```

  - Then, in `Weapons/CombatMath.cpp` → `ApplyDamage`, change `R.DamageToHealth = Damage - R.DamageToArmor;` to `R.DamageToHealth = FMath::Min(Damage - R.DamageToArmor, Health); // only what was actually removed (spec §3)`.
  - The only other reader is `UHealthComponent::ApplyHit`, which checks `> 0`; that's unaffected.

- [ ] **Step 3: Implement** `Learning/CombatArena.h`:

```cpp
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
```

`Learning/CombatArena.cpp`:

```cpp
#include "Learning/CombatArena.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Bots/ScriptedBot.h"
#include "Character/BeanAIController.h"
#include "Character/BeanCharacter.h"
#include "Character/HealthComponent.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Learning/ReachTaskLogic.h"
#include "Weapons/RifleComponent.h"

namespace
{
	using FPSCombatReward::ETerm;

	const UFPSAgentSettings& Settings() { return UFPSAgentSettings::Get(); }
	const FCombatStageSpec& Stage() { return Settings().Curriculum.S0; }
	/** Keeps bots off the walls (spawn geometry, not a mechanic). */
	constexpr float WallMarginCm = 200.f;

	FVector SpawnOffset() { return FVector(0.0, 0.0, Settings().Body.StandingHalfHeight + 2.f); }
}

float UCombatArena::FloorHalfSizeCm()
{
	return 0.5f * (Stage().FloorTiles + 2) * FMapSpec().TileCm;
}

float UCombatArena::GetBotRadiusCm() const
{
	return 0.5f * Stage().FloorTiles * FMapSpec().TileCm - WallMarginCm;
}

void UCombatArena::Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed)
{
	WorldPtr = World;
	ArenaIndex = InArenaIndex;
	Center = InCenter;
	BaseSeed = InBaseSeed;

	const FMapSpec Spec = FPSArena::MakeOpenFloorSpec(Stage().FloorTiles, FString::Printf(TEXT("s0floor_%d"), ArenaIndex));
	FPSArena::BuildMap(World, Spec, Center - Spec.Center());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Spawn = Center + SpawnOffset();
	Agent = World->SpawnActor<ABeanCharacter>(Spawn, FRotator::ZeroRotator, Params);
	if (!Agent)
	{
		UE_LOG(LogFPSAgent, Error, TEXT("Combat: could not spawn the agent for arena %d"), ArenaIndex);
		return;
	}
	Agent->AIControllerClass = ABeanAIController::StaticClass();
	Agent->SpawnDefaultController(); // CharacterMovement only simulates controlled pawns
	Agent->GetRifle()->OnShotFired.AddUObject(this, &UCombatArena::OnAgentShot);
	Agent->GetHealth()->OnDamaged.AddUObject(this, &UCombatArena::OnAgentDamaged);
	Agent->GetHealth()->OnDied.AddUObject(this, &UCombatArena::OnAgentDied);

	const int32 MaxBots = FMath::Max(Stage().BotsEasy, Stage().BotsHard);
	for (int32 i = 0; i < MaxBots; ++i)
	{
		AScriptedBot* Bot = World->SpawnActor<AScriptedBot>(Spawn + FVector(200.0 * (i + 1), 0.0, 0.0), FRotator::ZeroRotator, Params);
		if (!Bot)
		{
			UE_LOG(LogFPSAgent, Error, TEXT("Combat: could not spawn bot %d for arena %d"), i, ArenaIndex);
			continue;
		}
		Bot->GetHealth()->OnDamaged.AddUObject(this, &UCombatArena::OnBotDamaged);
		Bot->GetHealth()->OnDied.AddUObject(this, &UCombatArena::OnBotDied);
		Bot->Deactivate();
		Bots.Add(Bot);
	}
}

int32 UCombatArena::NextEpisodeSeed() const
{
	return FPSReach::EpisodeSeed(BaseSeed, ArenaIndex, EpisodeCount);
}

void UCombatArena::StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now)
{
	++EpisodeCount;
	bReview = Config.bReview;
	Level = Config.Level;
	EpisodeStart = Now;
	LastHitTime = -1.0e9;
	Held = FPSCombatObs::FAgentAction();
	Kills = 0;
	Shots = 0;
	Hits = 0;
	Headshots = 0;
	Tracks.Reset();
	if (!Agent)
	{
		return;
	}

	const FVector Spawn = Center + SpawnOffset();
	const FRotator Facing(0.0, Config.AgentYawDeg, 0.0);
	Agent->SetActorLocationAndRotation(Spawn, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = Agent->GetController())
	{
		C->SetControlRotation(Facing);
	}
	Agent->GetHealth()->ResetToFull();
	Agent->GetRifle()->ResetWeapon();
	Agent->GetRifle()->SeedRandom(Seed);
	Agent->SetDeadState(false);
	Agent->UnCrouch();
	Agent->SetWalking(false);
	Agent->StopJumping();
	Agent->GetCharacterMovement()->StopMovementImmediately();

	ActiveBots = 0;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot) { continue; }
		if (Config.BotOffsets.IsValidIndex(i))
		{
			const FVector Pos = Spawn + FVector(Config.BotOffsets[i].X, Config.BotOffsets[i].Y, 0.0);
			const float FaceAgentYaw = (Spawn - Pos).GetSafeNormal2D().Rotation().Yaw;
			const bool bCrouched = Config.BotCrouched.IsValidIndex(i) && Config.BotCrouched[i];
			Bot->Activate(Pos, FaceAgentYaw, bCrouched, static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), static_cast<uint32>(i + 1)) & 0x7fffffff));
			++ActiveBots;
		}
		else
		{
			Bot->Deactivate();
		}
	}

	// Initial tracks (no reward) so the first observation already sees what is in view; the aim helper starts from here.
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, Settings().Combat.Perception.MemorySeconds);
	Ledger.BeginEpisode(FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), Stage().AimHelperWeight, Settings().Combat.Reward));
}

void UCombatArena::TickAction(float DeltaSeconds)
{
	const UFPSAgentSettings& S = Settings();
	Ledger.Add(ETerm::Time, S.Combat.Reward.TimePerSecond * DeltaSeconds);
	if (!Agent || IsAgentDead())
	{
		return;
	}
	AController* C = Agent->GetController();
	if (!C)
	{
		return;
	}
	const FCombatPerceptionSettings& P = S.Combat.Perception;
	const FVector2D Step = FPSCombatObs::AimPerTickDeg(Held.Aim, P, DeltaSeconds);
	FRotator R = C->GetControlRotation();
	R.Yaw = FRotator::NormalizeAxis(R.Yaw + Step.X);
	R.Pitch = FMath::Clamp(FRotator::NormalizeAxis(R.Pitch) + Step.Y, -P.MaxPitchDeg, P.MaxPitchDeg);
	R.Roll = 0.0;
	C->SetControlRotation(R);
	if (Stage().bAgentCanMove)
	{
		Agent->AddMovementInput(FPSCombatObs::MoveWorld(Held.Move, R.Yaw), 1.f);
	}
}

void UCombatArena::SetAction(const FPSCombatObs::FAgentAction& Action)
{
	Held = FPSCombatObs::Sanitize(Action);
	if (!Agent || IsAgentDead())
	{
		return;
	}
	Agent->GetRifle()->SetTriggerHeld(Held.bFire);
	if (Held.bReload)
	{
		Agent->GetRifle()->RequestReload();
	}
	if (Stage().bAgentCanMove)
	{
		if (Held.bCrouch) { Agent->Crouch(); } else { Agent->UnCrouch(); }
		Agent->SetWalking(Held.bWalk);
		if (Held.bJump) { Agent->Jump(); } else { Agent->StopJumping(); }
	}
}

void UCombatArena::PrepareDecision(double Now)
{
	const UFPSAgentSettings& S = Settings();
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, S.Combat.Perception.MemorySeconds);
	for (const FPSPerception::FSighting& Si : Sightings)
	{
		if (Si.bVisible) { Ledger.TrySighting(Si.BotIndex, S.Combat.Reward); }
	}
	const ECombatEpisodeEnd End = GetEpisodeEnd(Now);
	if (End == ECombatEpisodeEnd::Cleared)
	{
		Ledger.TryClear(TimeLeftFraction(Now), S.Combat.Reward);
	}
	const float Potential = End == ECombatEpisodeEnd::Running
		? FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), Stage().AimHelperWeight, S.Combat.Reward)
		: 0.f; // terminal state: potential 0
	Ledger.ApplyPotential(Potential, S.Combat.Ppo.Discount);
}

ECombatEpisodeEnd UCombatArena::GetEpisodeEnd(double Now) const
{
	if (IsAgentDead()) { return ECombatEpisodeEnd::Died; }
	if (ActiveBots > 0 && AllActiveBotsDead()) { return ECombatEpisodeEnd::Cleared; }
	if (Now - EpisodeStart >= Stage().TimeLimitSeconds - 1e-6) { return ECombatEpisodeEnd::TimedOut; }
	return ECombatEpisodeEnd::Running;
}

FPSCombatStats::FEpisodeRecord UCombatArena::FinishEpisode(ECombatEpisodeEnd End, double Now) const
{
	FPSCombatStats::FEpisodeRecord R;
	R.bCleared = End == ECombatEpisodeEnd::Cleared;
	R.bDied = End == ECombatEpisodeEnd::Died;
	R.bTimedOut = End == ECombatEpisodeEnd::TimedOut;
	R.bReview = bReview;
	R.Level = Level;
	R.Return = Ledger.EpisodeReturn();
	for (int32 t = 0; t < FPSCombatReward::TermCount; ++t) { R.Terms[t] = Ledger.Episode[t]; }
	R.Kills = Kills;
	R.Bots = ActiveBots;
	R.Shots = Shots;
	R.Hits = Hits;
	R.Headshots = Headshots;
	R.Seconds = static_cast<float>(Now - EpisodeStart);
	return R;
}

void UCombatArena::BuildObservation(FPSCombatObs::FObservationData& Out, double Now) const
{
	const UFPSAgentSettings& S = Settings();
	const FCombatPerceptionSettings& P = S.Combat.Perception;
	if (!Agent)
	{
		Out = FPSCombatObs::FObservationData::Zero(P.RayYawCount * P.RayPitchesDeg.Num());
		return;
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	const float Yaw = View.Yaw;
	const FRifleFireState& Fire = Agent->GetRifle()->GetFireState();

	FPSCombatObs::FSelfState Self;
	Self.VelocityView = FPSPerception::ToViewFrame(Agent->GetVelocity(), Yaw);
	Self.bGrounded = !Agent->IsAirborne();
	Self.bCrouched = Agent->bIsCrouched;
	Self.bWalking = Agent->IsWalking();
	Self.Health = Agent->GetHealth()->GetHealth();
	Self.MaxHealth = S.Health.MaxHealth;
	Self.Armor = Agent->GetHealth()->GetArmor();
	Self.MaxArmor = S.Health.MaxArmor;
	Self.Ammo = Fire.Ammo;
	Self.MagazineSize = S.Rifle.MagazineSize;
	Self.bReloading = Fire.IsReloading();
	Self.SpreadDeg = Agent->GetRifle()->GetCurrentSpreadDegrees();
	Self.SprayShots = Fire.ShotIndex;
	Self.SecondsSinceShot = static_cast<float>(Now - Fire.LastShotTime);
	Self.AimPitchDeg = FRotator::NormalizeAxis(View.Pitch);
	Self.TimeLeftFraction = TimeLeftFraction(Now);
	Out.Self = FPSCombatObs::PackSelf(Self, P);

	Out.Rays.Reset();
	for (const FVector& Dir : FPSPerception::RayDirections(Yaw, P.RayYawCount, P.RayPitchesDeg))
	{
		Out.Rays.Add(FPSPerception::RayFraction(GetWorld(), Eye, Dir, P.RayRangeCm));
	}

	Out.Enemies.Reset();
	for (const FPSPerception::FTrack& T : FPSPerception::SelectTracks(Tracks, P.MaxEnemies))
	{
		FPSCombatObs::FEnemyState E;
		E.HeadView = FPSPerception::ToViewFrame(T.Head - Eye, Yaw);
		E.AimErrorDeg = FPSPerception::AimErrorDeg(Eye, View, T.Head);
		E.VelocityView = FPSPerception::ToViewFrame(T.Velocity, Yaw);
		E.bFacingAgent = T.bFacingAgent;
		E.bCrouched = T.bCrouched;
		E.bVisible = T.bVisible;
		E.SecondsSinceSeen = static_cast<float>(Now - T.LastSeenTime);
		Out.Enemies.Add(FPSCombatObs::PackEnemy(E, P));
	}

	const float HitAge = static_cast<float>(Now - LastHitTime);
	Out.bHit = HitAge >= 0.f && HitAge <= P.HitIndicatorSeconds;
	Out.Hit = Out.bHit ? FPSCombatObs::PackHit(FPSPerception::ToViewFrame(LastHitDirection, Yaw), HitAge, P) : TArray<float>();
	Out.Sounds.Reset(); // hearing arrives with moving and shooting bots (build B)
}

FPSCombatObs::FAgentAction UCombatArena::ScriptedAction(float DecisionSeconds) const
{
	const UFPSAgentSettings& S = Settings();
	FPSCombatObs::FScriptedTarget Target;
	if (!Agent)
	{
		return FPSCombatObs::FAgentAction();
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	double Best = TNumericLimits<double>::Max();
	for (const FPSPerception::FTrack& T : Tracks)
	{
		if (!T.bVisible) { continue; }
		const FVector2D Err = FPSPerception::AimErrorDeg(Eye, View, T.Head);
		if (Err.Size() < Best)
		{
			Best = Err.Size();
			Target.bHasTarget = true;
			Target.AimErrorDeg = Err;
			Target.DistanceCm = static_cast<float>(FVector::Dist(Eye, T.Head));
		}
	}
	const FRifleFireState& Fire = Agent->GetRifle()->GetFireState();
	return FPSCombatObs::ScriptedAction(Target, Fire.Ammo, S.Rifle.MagazineSize, Fire.IsReloading(), S.Body.HeadRadius, S.Combat.Perception, DecisionSeconds);
}

void UCombatArena::Sense(TArray<FPSPerception::FSighting>& Out) const
{
	Out.Reset();
	if (!Agent)
	{
		return;
	}
	const UWorld* World = GetWorld();
	const FCombatPerceptionSettings& P = Settings().Combat.Perception;
	const bool bAgentAlive = !IsAgentDead();
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	const double FacingCos = FMath::Cos(FMath::DegreesToRadians(0.5 * P.FieldOfViewDeg));
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		const AScriptedBot* Bot = Bots[i];
		if (!Bot || !Bot->IsActive()) { continue; }
		FPSPerception::FSighting S;
		S.BotIndex = i;
		S.bAlive = Bot->IsAlive();
		if (S.bAlive)
		{
			S.Head = Bot->GetHitbox(EHitZone::Head)->GetComponentLocation();
			const FVector Chest = Bot->GetHitbox(EHitZone::Body)->GetComponentLocation();
			S.bVisible = bAgentAlive && FPSPerception::InFieldOfView(Eye, View, S.Head, P.FieldOfViewDeg)
				&& (FPSPerception::HasLineOfSight(World, Eye, S.Head) || FPSPerception::HasLineOfSight(World, Eye, Chest));
			S.Velocity = Bot->GetVelocity();
			const FVector ToAgent = (Agent->GetActorLocation() - Bot->GetActorLocation()).GetSafeNormal2D();
			S.bFacingAgent = FVector::DotProduct(Bot->GetActorForwardVector().GetSafeNormal2D(), ToAgent) > FacingCos;
			S.bCrouched = Bot->bIsCrouched;
		}
		Out.Add(S);
	}
}

float UCombatArena::NearestVisibleAimErrorDeg(const TArray<FPSPerception::FSighting>& Sightings) const
{
	if (!Agent)
	{
		return -1.f;
	}
	FVector Eye;
	FRotator View;
	Agent->GetActorEyesViewPoint(Eye, View);
	float Best = -1.f;
	for (const FPSPerception::FSighting& S : Sightings)
	{
		if (!S.bVisible) { continue; }
		const float Err = static_cast<float>(FPSPerception::AimErrorDeg(Eye, View, S.Head).Size());
		if (Best < 0.f || Err < Best) { Best = Err; }
	}
	return Best;
}

bool UCombatArena::IsAgentDead() const
{
	return !Agent || Agent->GetHealth()->IsDead();
}

bool UCombatArena::AllActiveBotsDead() const
{
	for (const TObjectPtr<AScriptedBot>& Bot : Bots)
	{
		if (Bot && Bot->IsAlive()) { return false; }
	}
	return true;
}

float UCombatArena::TimeLeftFraction(double Now) const
{
	return FMath::Clamp(1.f - static_cast<float>((Now - EpisodeStart) / FMath::Max(1e-3f, Stage().TimeLimitSeconds)), 0.f, 1.f);
}

int32 UCombatArena::BotIndexOf(const AActor* Actor) const
{
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		if (Bots[i].Get() == Actor) { return i; }
	}
	return INDEX_NONE;
}

void UCombatArena::OnAgentShot(const FShotResult& Shot)
{
	++Shots;
	if (Shot.Zone != EHitZone::None && BotIndexOf(Shot.HitActor.Get()) != INDEX_NONE)
	{
		++Hits;
		Headshots += Shot.Zone == EHitZone::Head ? 1 : 0;
		return; // damage and kill rewards come from the bot's health events (damage actually applied)
	}
	Ledger.Add(ETerm::Miss, Settings().Combat.Reward.MissPerBullet);
}

void UCombatArena::OnAgentDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator)
{
	Ledger.Add(ETerm::DamageTaken, FPSCombatReward::DamageTaken(Result.DamageToHealth + Result.DamageToArmor, Settings().Combat.Reward));
	if (const UWorld* World = GetWorld())
	{
		LastHitTime = World->GetTimeSeconds();
	}
	LastHitDirection = (DamageInstigator && Agent) ? (DamageInstigator->GetActorLocation() - Agent->GetActorLocation()).GetSafeNormal() : FVector::ZeroVector;
}

void UCombatArena::OnAgentDied(UHealthComponent* Victim, AActor* Killer)
{
	Ledger.Add(ETerm::Death, Settings().Combat.Reward.Death);
	if (Agent)
	{
		Agent->SetDeadState(true);
	}
}

void UCombatArena::OnBotDamaged(UHealthComponent* Victim, const FPSCombat::FDamageResult& Result, AActor* DamageInstigator)
{
	if (Agent && DamageInstigator == Agent)
	{
		Ledger.Add(ETerm::DamageDealt, FPSCombatReward::DamageDealt(Result.DamageToHealth + Result.DamageToArmor, Settings().Combat.Reward));
	}
}

void UCombatArena::OnBotDied(UHealthComponent* Victim, AActor* Killer)
{
	if (Agent && Killer == Agent)
	{
		++Kills;
		Ledger.Add(ETerm::Kill, Settings().Combat.Reward.Kill);
	}
}
```

- [ ] **Step 4: Build and run the tests.**
  - Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Combat.Arena"`.
  - Expected: `Succeeded: 4  Failed: 0`.
  - If `KillAndClear` misses the head, check that `GetActorEyesViewPoint` uses the controller rotation: the agent must have a controller (`SpawnDefaultController` in `Build`).

- [ ] **Step 5: Run the whole suite** (`powershell -File tools/run_tests.ps1`). Expected: `Failed: 0`.

- [ ] **Step 6: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatArena.* Game/FPSAgent/Source/FPSAgent/Weapons/CombatMath.cpp Game/FPSAgent/Source/FPSAgent/Tests/CombatArenaTests.cpp Game/FPSAgent/Source/FPSAgent/Tests/CombatMathTests.cpp
git commit -m "[local] UCombatArena: S0 floor, agent + bot pool, events -> reward ledger, state-only episode end (death beats clear), fair observation, scripted action"
git pull --rebase && git push
```

---

### Task 10: Extract the shared training session from Spike A

**Files:**
- Create: `Learning/TrainingSession.h`, `Learning/TrainingSession.cpp`
- Modify: `Learning/ReachTaskActor.h`, `Learning/ReachTaskActor.cpp`

**Interfaces:**
- Produces:
  - `struct FTrainingSessionConfig { FString LogPrefix; TSubclassOf<ULearningAgentsInteractor> InteractorClass; TSubclassOf<ULearningAgentsTrainingEnvironment> EnvClass; FLearningAgentsPolicySettings Policy; FLearningAgentsCriticSettings Critic; FLearningAgentsPPOTrainerSettings Trainer; FLearningAgentsPPOTrainingSettings Training; bool bResetAgentsOnUpdate; bool bInfer; int32 Seed; FString ResumeFrom; }`.
  - `UTrainingSession` methods:
    - `bool Setup(ULearningAgentsManager&, const FTrainingSessionConfig&)`;
    - `enum class EStatus { Running, Finished, Failed }`, `EStatus RunTrainingStep()`;
    - `ULearningAgentsPolicy* GetPolicy() const`;
    - `static void ExitProcess(const FString& LogPrefix, int32 Code, const FString& Reason)`.
- Behaviour kept exactly as Spike A today:
  - the watchdog (`-LearningAgentsTimeout` + 15 s wall clock, exit 1);
  - snapshot checks (exit 2);
  - the external trainer only;
  - the log lines `<Prefix>: trainer settings: iterations=... (= ... batches of 32) snapshotEvery=... device=... tensorboard=... seed=...` and `<Prefix>: exiting (code N): ...`.

- [ ] **Step 1: Record the baseline before refactoring** (the existing tests are the regression net here).
  - Run `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Learning"`. Expected: `Succeeded: 6  Failed: 0`.
  - Run the headless checks from Step 5 below on the current code and note the numbers:
    - `-Scripted` reaches 100%;
    - untrained `-Infer` reaches 0%;
    - the no-trainer run exits 1 after about 20 s.

- [ ] **Step 2: Create** `Learning/TrainingSession.h`:

```cpp
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
```

- [ ] **Step 3: Create** `Learning/TrainingSession.cpp`. The body is moved from `AReachTaskActor::SetupLearning`, `LoadSnapshots`, `ExitWith` and the training branch of `Tick`:

```cpp
#include "Learning/TrainingSession.h"
#include "Async/Async.h"
#include "FPSAgent.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "LearningAgentsCommunicator.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsNeuralNetwork.h"
#include "LearningAgentsTrainer.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "Misc/CommandLine.h"
#include "Misc/OutputDeviceRedirector.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include <atomic>

void UTrainingSession::ExitProcess(const FString& LogPrefix, int32 Code, const FString& Reason)
{
	if (Code == 0)
	{
		UE_LOG(LogFPSAgent, Display, TEXT("%s: exiting (code 0): %s"), *LogPrefix, *Reason);
	}
	else
	{
		UE_LOG(LogFPSAgent, Error, TEXT("%s: exiting (code %d): %s"), *LogPrefix, Code, *Reason);
	}
	if (!GIsEditor)
	{
		FPlatformMisc::RequestExitWithStatus(false, static_cast<uint8>(Code));
	}
}

bool UTrainingSession::Setup(ULearningAgentsManager& Manager, const FTrainingSessionConfig& InConfig)
{
	Config = InConfig;
	// The Make* helpers take UPARAM(ref) pointers, so they need plain local variables of the base types.
	ULearningAgentsManager* Mgr = &Manager;
	ULearningAgentsInteractor* I = ULearningAgentsInteractor::MakeInteractor(Mgr, Config.InteractorClass, TEXT("Interactor"));
	ULearningAgentsPolicy* P = I ? ULearningAgentsPolicy::MakePolicy(Mgr, I, ULearningAgentsPolicy::StaticClass(), TEXT("Policy"),
		nullptr, nullptr, nullptr, true, true, true, Config.Policy, Config.Seed) : nullptr;
	Interactor = I;
	Policy = P;
	if (!I || !P)
	{
		ExitProcess(Config.LogPrefix, 1, TEXT("Learning Agents interactor/policy setup failed"));
		return false;
	}

	ULearningAgentsCritic* C = nullptr;
	if (!Config.bInfer)
	{
		C = ULearningAgentsCritic::MakeCritic(Mgr, I, P, ULearningAgentsCritic::StaticClass(), TEXT("Critic"), nullptr, true, Config.Critic, Config.Seed);
		Critic = C;
	}
	if (!Config.ResumeFrom.IsEmpty() && !LoadSnapshots(Config.ResumeFrom))
	{
		ExitProcess(Config.LogPrefix, 2, TEXT("-ResumeFrom is missing snapshot files"));
		return false;
	}
	if (Config.bInfer)
	{
		return true;
	}

	ULearningAgentsTrainingEnvironment* E = ULearningAgentsTrainingEnvironment::MakeTrainingEnvironment(Mgr, Config.EnvClass, TEXT("TrainingEnv"));
	Env = E;

	FLearningAgentsTrainerProcessSettings Process;
	Process.TaskName = TEXT("FPSAgent");
	FLearningAgentsSocketCommunicatorSettings Socket;
	Socket.bUseExternalTrainingProcess = true; // never spawn a local trainer; address/port come from -LearningAgents* flags

	// Watchdog. Learning Agents' connect loop counts only its 1 ms sleeps toward the timeout, not the time spent inside
	// each blocking connect(), so an unreachable trainer can block far longer than -LearningAgentsTimeout. A real
	// wall-clock deadline force-exits with code 1. Never probe the trainer port ourselves (D-020).
	float ConnectTimeout = Socket.Timeout;
	FParse::Value(FCommandLine::Get(), TEXT("LearningAgentsTimeout="), ConnectTimeout);
	const TSharedRef<std::atomic<bool>> bConnectDone = MakeShared<std::atomic<bool>>(false);
	if (!GIsEditor)
	{
		const double Deadline = FMath::Max(1.0, static_cast<double>(ConnectTimeout)) + 15.0;
		const FString Prefix = Config.LogPrefix;
		Async(EAsyncExecution::Thread, [bConnectDone, Deadline, Prefix]()
		{
			const double Start = FPlatformTime::Seconds();
			while (!bConnectDone->load())
			{
				if (FPlatformTime::Seconds() - Start > Deadline)
				{
					UE_LOG(LogFPSAgent, Error, TEXT("%s: exiting (code 1): no trainer connection after %.0f s (wall clock). Start the trainer first and wait for 'Server listening on'."), *Prefix, Deadline);
					GLog->Flush();
					FPlatformMisc::RequestExitWithStatus(true, 1);
					return;
				}
				FPlatformProcess::Sleep(0.2f);
			}
		});
	}
	const FLearningAgentsCommunicator Communicator = ULearningAgentsCommunicatorLibrary::MakeSocketTrainingProcess(Process, Socket);
	bConnectDone->store(true);

	Trainer = ULearningAgentsPPOTrainer::MakePPOTrainer(Mgr, I, E, P, C, Communicator,
		ULearningAgentsPPOTrainer::StaticClass(), TEXT("PPOTrainer"), Config.Trainer);
	if (!E || !C || !Trainer)
	{
		ExitProcess(Config.LogPrefix, 1, TEXT("Learning Agents trainer setup failed"));
		return false;
	}
	const FLearningAgentsPPOTrainingSettings& T = Config.Training;
	UE_LOG(LogFPSAgent, Display, TEXT("%s: trainer settings: iterations=%d (= %.1f batches of %d) snapshotEvery=%d device=%s tensorboard=%d seed=%d"),
		*Config.LogPrefix, T.NumberOfIterations, static_cast<double>(T.NumberOfIterations) / FMath::Max(1, T.IterationsPerGather),
		T.IterationsPerGather, T.IterationsPerSnapshot, T.Device == ELearningAgentsTrainingDevice::GPU ? TEXT("GPU") : TEXT("CPU"),
		T.bUseTensorboard ? 1 : 0, T.RandomSeed);
	return true;
}

UTrainingSession::EStatus UTrainingSession::RunTrainingStep()
{
	// RunTraining would start a NEW session if called after the trainer finished, so report Finished as soon as it ends.
	Trainer->RunTraining(Config.Training, FLearningAgentsTrainingGameSettings(), true, Config.bResetAgentsOnUpdate);
	if (Trainer->HasTrainingFailed())
	{
		return EStatus::Failed;
	}
	if (Trainer->IsTraining())
	{
		bTrainingStarted = true;
		return EStatus::Running;
	}
	return bTrainingStarted ? EStatus::Finished : EStatus::Running;
}

bool UTrainingSession::LoadSnapshots(const FString& Dir)
{
	TArray<TPair<FString, ULearningAgentsNeuralNetwork*>> Items;
	Items.Emplace(TEXT("encoder.bin"), Policy->GetEncoderNetworkAsset());
	Items.Emplace(TEXT("policy.bin"), Policy->GetPolicyNetworkAsset());
	Items.Emplace(TEXT("decoder.bin"), Policy->GetDecoderNetworkAsset());
	if (Critic)
	{
		Items.Emplace(TEXT("critic.bin"), Critic->GetCriticNetworkAsset());
	}
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		const FString Path = FPaths::Combine(Dir, Item.Key);
		if (!Item.Value || !FPaths::FileExists(Path))
		{
			UE_LOG(LogFPSAgent, Error, TEXT("%s: snapshot not found: %s"), *Config.LogPrefix, *Path);
			return false;
		}
	}
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		FFilePath File;
		File.FilePath = FPaths::Combine(Dir, Item.Key);
		Item.Value->LoadNetworkFromSnapshot(File);
	}
	UE_LOG(LogFPSAgent, Display, TEXT("%s: loaded %d snapshots from %s"), *Config.LogPrefix, Items.Num(), *Dir);
	return true;
}
```

- [ ] **Step 4: Switch `AReachTaskActor` to the session.**
  - In `Learning/ReachTaskActor.h`:
    - delete the members `Interactor`, `Policy`, `Critic`, `Env`, `Trainer`, `TrainingSettings` and `bTrainingStarted`;
    - delete the private functions `LoadSnapshots` and `ExitWith`;
    - delete the `#include "LearningAgentsPPOTrainer.h"` line;
    - add the forward declaration `class UTrainingSession;` and the member `UPROPERTY() TObjectPtr<UTrainingSession> Session;`;
    - add the private function `void ExitWith(int32 Code, const TCHAR* Reason);`. It stays, as a thin wrapper.
  - In `Learning/ReachTaskActor.cpp`:
    1. Add `#include "Learning/TrainingSession.h"`. Remove the includes that are now only used by the session: `Async/Async.h`, `<atomic>`, `LearningAgentsCommunicator.h`, `LearningAgentsCritic.h`, `LearningAgentsNeuralNetwork.h`, `Misc/OutputDeviceRedirector.h`, `HAL/PlatformProcess.h`. Keep `LearningAgentsManager.h`, `LearningAgentsPolicy.h`, `LearningAgentsTrainer.h`, `Misc/App.h`, `HAL/IConsoleManager.h`, `HAL/PlatformTime.h`, `HAL/PlatformMisc.h`.
    2. Replace the whole `SetupLearning()` body with:

```cpp
bool AReachTaskActor::SetupLearning()
{
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	Manager->SetMaxAgentNum(Agents.Num());
	FTrainingSessionConfig Config;
	Config.LogPrefix = TEXT("SpikeA");
	Config.InteractorClass = UReachInteractor::StaticClass();
	Config.EnvClass = UReachTrainingEnv::StaticClass();
	Config.bInfer = bInfer;
	Config.Seed = BaseSeed;
	Config.ResumeFrom = Args.ResumeFrom;
	Config.bResetAgentsOnUpdate = true;
	Config.Trainer.MaxEpisodeStepNum = Settings.MaxEpisodeSteps;
	FLearningAgentsPPOTrainingSettings& T = Config.Training;
	// The trainer counts gradient iterations, IterationsPerGather (32) per experience batch (C-005); -Batches converts.
	T.NumberOfIterations = Args.GradientIterations(T.IterationsPerGather, Settings.MaxIterations);
	T.bSaveSnapshots = true;
	T.IterationsPerSnapshot = FMath::Max(1, Settings.IterationsPerSnapshot);
	T.bUseTensorboard = Settings.bUseTensorboard;
	T.Device = Settings.bTrainOnGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
	T.RandomSeed = BaseSeed;
	Session = NewObject<UTrainingSession>(this);
	if (!Session->Setup(*Manager, Config))
	{
		bExiting = true;
		return false;
	}
	return true;
}
```

    3. Delete `AReachTaskActor::LoadSnapshots` entirely, and replace `ExitWith`'s body with:

```cpp
void AReachTaskActor::ExitWith(int32 Code, const TCHAR* Reason)
{
	bExiting = true;
	UTrainingSession::ExitProcess(TEXT("SpikeA"), Code, Reason);
}
```

    4. In `BeginPlay`, delete the `if (!bInfer) { UE_LOG(... "SpikeA: trainer settings: ...") }` block. The session logs that line now.
    5. In `Tick`, replace `Policy->RunInference(0.f);` with `Session->GetPolicy()->RunInference(0.f);`. Replace the whole training `else` branch with:

```cpp
	else
	{
		switch (Session->RunTrainingStep())
		{
		case UTrainingSession::EStatus::Failed:
			LogStats();
			ExitWith(1, TEXT("training failed: trainer unreachable, timed out or crashed (see LogLearning above)"));
			return;
		case UTrainingSession::EStatus::Finished:
			LogStats();
			ExitWith(0, TEXT("trainer finished"));
			return;
		default:
			break;
		}
	}
```

    6. In `SpawnAgents`, `Manager->AddAgent(Bean)` stays as it is (the session doesn't add agents).
  - Note: `-Scripted` never calls `Session->GetPolicy()`. The existing `if (bScripted) ... else Policy->RunInference` structure is kept.

- [ ] **Step 5: Build, test and re-run the headless checks.**
  - Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1`. Expected: `Failed: 0`.
  - Then run these PowerShell commands (each stops itself or is stopped after the sleep):

```powershell
$ue = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "$PWD\Game\FPSAgent\FPSAgent.uproject"
$tmp = $env:TEMP
# (a) scripted playback: expect "SpikeA stats ... reached=100.0%"
$p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended -Stage=SpikeA -Arenas=4 -Scripted -abslog=`"$tmp\sa_scripted.log`"" -PassThru -WindowStyle Hidden; Start-Sleep 60; Stop-Process -Id $p.Id -Force
Select-String "$tmp\sa_scripted.log" -Pattern "SpikeA stats" | Select-Object -Last 2
# (b) untrained policy playback through the session: expect "reached=0.0%" and no LogLearning errors
$p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended -Stage=SpikeA -Arenas=4 -Infer -abslog=`"$tmp\sa_infer.log`"" -PassThru -WindowStyle Hidden; Start-Sleep 45; Stop-Process -Id $p.Id -Force
Select-String "$tmp\sa_infer.log" -Pattern "SpikeA stats|LogLearning: Error" | Select-Object -Last 2
# (c) no trainer: expect exit code 1 after about 20 s and "SpikeA: exiting (code 1): no trainer connection"
$p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended -Stage=SpikeA -LearningAgentsUseExternalTrainingProcess -LearningAgentsPort=1 -LearningAgentsTimeout=5 -abslog=`"$tmp\sa_notrainer.log`"" -PassThru -WindowStyle Hidden; $p.WaitForExit(120000); "exit=$($p.ExitCode)"
Select-String "$tmp\sa_notrainer.log" -Pattern "SpikeA: (trainer settings|exiting)"
```

  - Expected: the same results as the Step 1 baseline. Run (c) also prints the `SpikeA: trainer settings:` line before the exit line.
  - Afterwards, `Get-Process UnrealEditor* | Stop-Process -Force` cleans up anything left over.

- [ ] **Step 6: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/TrainingSession.* Game/FPSAgent/Source/FPSAgent/Learning/ReachTaskActor.*
git commit -m "[local] Extract UTrainingSession (policy/critic/snapshots/external trainer + watchdog/exit codes) from Spike A; ReachTaskActor uses it (behaviour unchanged)"
git pull --rebase && git push
```

---
### Task 11: Learning Agents layer, `ACombatTaskActor` and `-Stage=S0` dispatch

**Files:**
- Create: `Learning/CombatLearningClasses.h`, `Learning/CombatLearningClasses.cpp`
- Create: `Learning/CombatTaskActor.h`, `Learning/CombatTaskActor.cpp`
- Modify: `Arena/ArenaGameMode.h`, `Arena/ArenaGameMode.cpp`

**Interfaces:**
- Consumes: everything above (`UCombatArena`, `UTrainingSession`, `FPSCurriculum`, `FPSCombatStats`, `FPSCombatObs`, `FFPSRunArgs`).
- Produces:
  - `UCombatInteractor`, `UCombatTrainingEnv` (Learning Agents overrides).
  - `ACombatTaskActor` methods:
    - per agent id: `BuildObservation(int32, FObservationData&) const`, `SetAction(int32, const FAgentAction&)`, `float TakeDecisionReward(int32)`, `ECombatEpisodeEnd GetEpisodeEnd(int32) const`, `void ResetAgentEpisode(int32)`;
    - `static FBox ComputeBounds(int32 ArenaCount, const FVector& Origin)`.
  - `static int32 AArenaGameMode::RequestedCombatStage()`.
- Log lines:
  - `Combat: ready. ...`
  - `Combat: ppo ...`
  - `Combat: trainer settings: ...` (from the session)
  - `CombatStats: ...` (one per stats window)
  - `CombatEval: {json}`
  - `Combat: exiting (code N): ...`

- [ ] **Step 1: Write the Learning Agents classes.** `Learning/CombatLearningClasses.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "LearningAgentsInteractor.h"
#include "LearningAgentsTrainingEnvironment.h"
#include "CombatLearningClasses.generated.h"

/**
 * Combat observations and actions (spec §2). Thin: all state lives in ACombatTaskActor / UCombatArena; this class only
 * converts float arrays to Learning Agents elements, so the RL stack could be swapped without touching gameplay.
 *
 * Observation "Combat" (struct):
 * - Self: continuous 15.
 * - Rays: continuous 72.
 * - Enemies: set of up to 5 x continuous 13.
 * - Hit: optional continuous 4.
 * - Sounds: set of up to 4 x continuous 5.
 *
 * Action "Combat" (struct):
 * - Move: continuous 2.
 * - Aim: continuous 2.
 * - Fire, Reload, Crouch, Walk, Jump: bool.
 */
UCLASS()
class FPSAGENT_API UCombatInteractor : public ULearningAgentsInteractor
{
	GENERATED_BODY()

public:
	virtual void SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement, ULearningAgentsObservationSchema* InObservationSchema) override;
	virtual void GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement, ULearningAgentsObservationObject* InObservationObject, const int32 AgentId) override;
	virtual void SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema) override;
	virtual void PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject, const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId) override;

	/**
	 * Initial probabilities of the on/off actions (network initialisation, not mechanics).
	 * - Fire: 0.5, so the agent explores shooting.
	 * - Rare actions start rare.
	 */
	static constexpr float FirePrior = 0.5f;
	static constexpr float ReloadPrior = 0.05f;
	static constexpr float CrouchPrior = 0.1f;
	static constexpr float WalkPrior = 0.1f;
	static constexpr float JumpPrior = 0.05f;
};

/** Combat rewards, completions and resets. Completion is a pure function of state (D-022). Thin: forwards to ACombatTaskActor. */
UCLASS()
class FPSAGENT_API UCombatTrainingEnv : public ULearningAgentsTrainingEnvironment
{
	GENERATED_BODY()

public:
	virtual void GatherAgentReward_Implementation(float& OutReward, const int32 AgentId) override;
	virtual void GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId) override;
	virtual void ResetAgentEpisode_Implementation(const int32 AgentId) override;
};
```

`Learning/CombatLearningClasses.cpp`:

```cpp
#include "Learning/CombatLearningClasses.h"
#include "Common/FPSAgentSettings.h"
#include "Learning/CombatObservation.h"
#include "Learning/CombatTaskActor.h"
#include "LearningAgentsActions.h"
#include "LearningAgentsObservations.h"

namespace
{
	// The Make* helpers create both classes with the manager component as Outer, whose owner is the task actor.
	ACombatTaskActor* TaskOf(const UObject* Listener)
	{
		return Listener ? Listener->GetTypedOuter<ACombatTaskActor>() : nullptr;
	}

	int32 RayCount()
	{
		const FCombatPerceptionSettings& P = UFPSAgentSettings::Get().Combat.Perception;
		return P.RayYawCount * P.RayPitchesDeg.Num();
	}
}

void UCombatInteractor::SpecifyAgentObservation_Implementation(FLearningAgentsObservationSchemaElement& OutObservationSchemaElement, ULearningAgentsObservationSchema* InObservationSchema)
{
	using O = ULearningAgentsObservations;
	const FCombatPerceptionSettings& P = UFPSAgentSettings::Get().Combat.Perception;
	TMap<FName, FLearningAgentsObservationSchemaElement> E;
	E.Add(TEXT("Self"), O::SpecifyContinuousObservation(InObservationSchema, FPSCombatObs::SelfSize, 1.f, TEXT("Self")));
	E.Add(TEXT("Rays"), O::SpecifyContinuousObservation(InObservationSchema, RayCount(), 1.f, TEXT("Rays")));
	E.Add(TEXT("Enemies"), O::SpecifySetObservation(InObservationSchema,
		O::SpecifyContinuousObservation(InObservationSchema, FPSCombatObs::EnemySize, 1.f, TEXT("Enemy")), P.MaxEnemies, 32, 4, 32, TEXT("Enemies")));
	E.Add(TEXT("Hit"), O::SpecifyOptionalObservation(InObservationSchema,
		O::SpecifyContinuousObservation(InObservationSchema, FPSCombatObs::HitSize, 1.f, TEXT("HitDirection")), 32, TEXT("Hit")));
	E.Add(TEXT("Sounds"), O::SpecifySetObservation(InObservationSchema,
		O::SpecifyContinuousObservation(InObservationSchema, FPSCombatObs::SoundSize, 1.f, TEXT("Sound")), P.MaxSounds, 32, 4, 32, TEXT("Sounds")));
	OutObservationSchemaElement = O::SpecifyStructObservation(InObservationSchema, E, TEXT("Combat"));
}

void UCombatInteractor::GatherAgentObservation_Implementation(FLearningAgentsObservationObjectElement& OutObservationObjectElement, ULearningAgentsObservationObject* InObservationObject, const int32 AgentId)
{
	using O = ULearningAgentsObservations;
	FPSCombatObs::FObservationData D = FPSCombatObs::FObservationData::Zero(RayCount());
	if (const ACombatTaskActor* Task = TaskOf(this))
	{
		Task->BuildObservation(AgentId, D);
	}
	TMap<FName, FLearningAgentsObservationObjectElement> E;
	E.Add(TEXT("Self"), O::MakeContinuousObservation(InObservationObject, D.Self, TEXT("Self")));
	E.Add(TEXT("Rays"), O::MakeContinuousObservation(InObservationObject, D.Rays, TEXT("Rays")));
	TSet<FLearningAgentsObservationObjectElement> Enemies;
	for (const TArray<float>& V : D.Enemies) { Enemies.Add(O::MakeContinuousObservation(InObservationObject, V, TEXT("Enemy"))); }
	E.Add(TEXT("Enemies"), O::MakeSetObservation(InObservationObject, Enemies, TEXT("Enemies")));
	E.Add(TEXT("Hit"), D.bHit
		? O::MakeOptionalObservation(InObservationObject, O::MakeContinuousObservation(InObservationObject, D.Hit, TEXT("HitDirection")), ELearningAgentsOptionalObservation::Valid, TEXT("Hit"))
		: O::MakeOptionalNullObservation(InObservationObject, TEXT("Hit")));
	TSet<FLearningAgentsObservationObjectElement> Sounds;
	for (const TArray<float>& V : D.Sounds) { Sounds.Add(O::MakeContinuousObservation(InObservationObject, V, TEXT("Sound"))); }
	E.Add(TEXT("Sounds"), O::MakeSetObservation(InObservationObject, Sounds, TEXT("Sounds")));
	OutObservationObjectElement = O::MakeStructObservation(InObservationObject, E, TEXT("Combat"));
}

void UCombatInteractor::SpecifyAgentAction_Implementation(FLearningAgentsActionSchemaElement& OutActionSchemaElement, ULearningAgentsActionSchema* InActionSchema)
{
	using A = ULearningAgentsActions;
	TMap<FName, FLearningAgentsActionSchemaElement> E;
	E.Add(TEXT("Move"), A::SpecifyContinuousAction(InActionSchema, 2, 1.f, TEXT("Move")));
	E.Add(TEXT("Aim"), A::SpecifyContinuousAction(InActionSchema, 2, 1.f, TEXT("Aim")));
	E.Add(TEXT("Fire"), A::SpecifyBoolAction(InActionSchema, FirePrior, TEXT("Fire")));
	E.Add(TEXT("Reload"), A::SpecifyBoolAction(InActionSchema, ReloadPrior, TEXT("Reload")));
	E.Add(TEXT("Crouch"), A::SpecifyBoolAction(InActionSchema, CrouchPrior, TEXT("Crouch")));
	E.Add(TEXT("Walk"), A::SpecifyBoolAction(InActionSchema, WalkPrior, TEXT("Walk")));
	E.Add(TEXT("Jump"), A::SpecifyBoolAction(InActionSchema, JumpPrior, TEXT("Jump")));
	OutActionSchemaElement = A::SpecifyStructAction(InActionSchema, E, TEXT("Combat"));
}

void UCombatInteractor::PerformAgentAction_Implementation(const ULearningAgentsActionObject* InActionObject, const FLearningAgentsActionObjectElement& InActionObjectElement, const int32 AgentId)
{
	using A = ULearningAgentsActions;
	TMap<FName, FLearningAgentsActionObjectElement> E;
	if (!A::GetStructAction(E, InActionObject, InActionObjectElement, TEXT("Combat")))
	{
		return;
	}
	FPSCombatObs::FAgentAction Act;
	TArray<float> V;
	if (const FLearningAgentsActionObjectElement* M = E.Find(TEXT("Move")); M && A::GetContinuousAction(V, InActionObject, *M, TEXT("Move")) && V.Num() == 2)
	{
		Act.Move = FVector2D(V[0], V[1]);
	}
	if (const FLearningAgentsActionObjectElement* M = E.Find(TEXT("Aim")); M && A::GetContinuousAction(V, InActionObject, *M, TEXT("Aim")) && V.Num() == 2)
	{
		Act.Aim = FVector2D(V[0], V[1]);
	}
	auto Bool = [&](const TCHAR* Name, bool& bOut)
	{
		if (const FLearningAgentsActionObjectElement* B = E.Find(FName(Name)))
		{
			A::GetBoolAction(bOut, InActionObject, *B, FName(Name));
		}
	};
	Bool(TEXT("Fire"), Act.bFire);
	Bool(TEXT("Reload"), Act.bReload);
	Bool(TEXT("Crouch"), Act.bCrouch);
	Bool(TEXT("Walk"), Act.bWalk);
	Bool(TEXT("Jump"), Act.bJump);
	if (ACombatTaskActor* Task = TaskOf(this))
	{
		Task->SetAction(AgentId, Act);
	}
}

void UCombatTrainingEnv::GatherAgentReward_Implementation(float& OutReward, const int32 AgentId)
{
	ACombatTaskActor* Task = TaskOf(this);
	OutReward = Task ? Task->TakeDecisionReward(AgentId) : 0.f;
}

void UCombatTrainingEnv::GatherAgentCompletion_Implementation(ELearningAgentsCompletion& OutCompletion, const int32 AgentId)
{
	// Learning Agents asks for completions BEFORE rewards: this must be a pure function of state (D-022).
	const ACombatTaskActor* Task = TaskOf(this);
	switch (Task ? Task->GetEpisodeEnd(AgentId) : ECombatEpisodeEnd::Running)
	{
	case ECombatEpisodeEnd::Died:
	case ECombatEpisodeEnd::Cleared:  OutCompletion = ELearningAgentsCompletion::Termination; break;
	case ECombatEpisodeEnd::TimedOut: OutCompletion = ELearningAgentsCompletion::Truncation; break;
	default:                          OutCompletion = ELearningAgentsCompletion::Running; break;
	}
}

void UCombatTrainingEnv::ResetAgentEpisode_Implementation(const int32 AgentId)
{
	if (ACombatTaskActor* Task = TaskOf(this)) { Task->ResetAgentEpisode(AgentId); }
}
```

- [ ] **Step 2: Write the task actor.** `Learning/CombatTaskActor.h`:

```cpp
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
	static FBox ComputeBounds(int32 ArenaCount, const FVector& Origin);

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
	double DecisionSeconds = 4.0 / 60.0;
	double NextDecisionTime = 0.0;
	double NextStatsTime = 0.0;
	double WindowWallStart = 0.0;
	int64 WindowDecisions = 0;
	bool bInfer = false;
	bool bScripted = false;
	bool bEval = false;
	bool bReady = false;
	bool bExiting = false;
	bool bCsvStarted = false;
};
```

`Learning/CombatTaskActor.cpp`:

```cpp
#include "Learning/CombatTaskActor.h"
#include "Arena/ArenaGameMode.h"
#include "Arena/MapSpec.h"
#include "Common/FPSAgentSettings.h"
#include "Common/FPSRunArgs.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Learning/CombatLearningClasses.h"
#include "Learning/ReachTaskLogic.h"
#include "Learning/TrainingSession.h"
#include "LearningAgentsManager.h"
#include "LearningAgentsPolicy.h"
#include "LearningAgentsTrainer.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"

ACombatTaskActor::ACombatTaskActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Manager = CreateDefaultSubobject<ULearningAgentsManager>(TEXT("LearningAgentsManager"));
}

FBox ACombatTaskActor::ComputeBounds(int32 ArenaCount, const FVector& Origin)
{
	FBox Box(ForceInit);
	const float Half = UCombatArena::FloorHalfSizeCm();
	for (const FVector& C : FPSReach::ArenaCenters(ArenaCount, UFPSAgentSettings::Get().Combat.ArenaSpacingCm, Origin))
	{
		Box += C + FVector(-Half, -Half, 0.0);
		Box += C + FVector(Half, Half, FMapSpec().WallCm);
	}
	return Box;
}

double ACombatTaskActor::Now() const
{
	return GetWorld()->GetTimeSeconds();
}

void ACombatTaskActor::Exit(int32 Code, const FString& Reason)
{
	bExiting = true;
	UTrainingSession::ExitProcess(TEXT("Combat"), Code, Reason);
}

void ACombatTaskActor::BeginPlay()
{
	Super::BeginPlay();
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	const int32 StageIndex = AArenaGameMode::RequestedCombatStage();
	if (StageIndex != 0)
	{
		Exit(2, FString::Printf(TEXT("stage S%d is not in this build (build A has S0 only)"), StageIndex));
		return;
	}
	bScripted = Args.bScripted;
	bEval = Args.bEval;
	bInfer = Args.bInfer || GIsEditor; // no local training (hard rule 6): the editor only plays back
	if (bEval && Args.ResumeFrom.IsEmpty() && !bScripted)
	{
		Exit(2, TEXT("-Eval needs -ResumeFrom=<snapshots> (or -Scripted for the reference score)"));
		return;
	}
	const float TickSeconds = 1.f / FMath::Max(1.f, FLearningAgentsTrainingGameSettings().FixedTimeStepFrequency);
	DecisionSeconds = TickSeconds * FMath::Max(1, S.Combat.Ppo.TicksPerDecision);
	if (bInfer && !GIsEditor)
	{
		// Headless playback / eval: the same fixed step as training, uncapped (D-019); training gets this from Learning Agents.
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(TickSeconds);
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps->Set(0.f); }
	}

	const TArray<FVector> Centers = FPSReach::ArenaCenters(Args.Arenas, S.Combat.ArenaSpacingCm, GetActorLocation());
	for (int32 i = 0; i < Centers.Num(); ++i)
	{
		UCombatArena* Arena = NewObject<UCombatArena>(this);
		Arena->Build(GetWorld(), i, Centers[i], Args.Seed);
		Arenas.Add(Arena);
		FPSCurriculum::FDifficultyController D;
		D.Init(Args.StartDifficulty);
		Difficulty.Add(D);
	}

	AgentToArena.Init(INDEX_NONE, Arenas.Num());
	if (bScripted)
	{
		for (int32 i = 0; i < Arenas.Num(); ++i) { AgentToArena[i] = i; }
	}
	else
	{
		Manager->SetMaxAgentNum(Arenas.Num());
		const FCombatPpoSettings& Ppo = S.Combat.Ppo;
		FTrainingSessionConfig Config;
		Config.LogPrefix = TEXT("Combat");
		Config.InteractorClass = UCombatInteractor::StaticClass();
		Config.EnvClass = UCombatTrainingEnv::StaticClass();
		Config.bInfer = bInfer;
		Config.Seed = Args.Seed;
		Config.ResumeFrom = Args.ResumeFrom;
		Config.bResetAgentsOnUpdate = false; // D-026: don't cut episodes (and the ramp's statistics) at every policy update
		Config.Policy.HiddenLayerNum = Ppo.HiddenLayers;
		Config.Policy.HiddenLayerSize = Ppo.HiddenSize;
		Config.Policy.MemoryStateSize = Ppo.MemorySize;
		Config.Critic.HiddenLayerNum = Ppo.HiddenLayers;
		Config.Critic.HiddenLayerSize = Ppo.HiddenSize;
		// Our own timeout (Truncation) fires first; this cap only guards against a stuck episode.
		Config.Trainer.MaxEpisodeStepNum = FMath::CeilToInt(S.Curriculum.S0.TimeLimitSeconds / DecisionSeconds) + 2;
		Config.Trainer.MaximumRecordedStepsPerIteration = FMath::Max(1, Ppo.ReplayMaxSteps);
		Config.Trainer.MaximumRecordedEpisodesPerIteration = FMath::Max(1, Ppo.ReplayMaxEpisodes);
		FLearningAgentsPPOTrainingSettings& T = Config.Training;
		T.IterationsPerGather = FMath::Max(1, Ppo.IterationsPerGather); // before converting -Batches (C-007)
		T.PolicyBatchSize = FMath::Max(1, Ppo.PolicyBatchSize);
		T.CriticBatchSize = FMath::Max(1, Ppo.CriticBatchSize);
		T.NumberOfIterations = Args.GradientIterations(T.IterationsPerGather, Ppo.MaxIterations);
		T.bSaveSnapshots = true;
		T.IterationsPerSnapshot = FMath::Max(1, Ppo.IterationsPerSnapshot);
		T.bUseTensorboard = Ppo.bUseTensorboard;
		T.Device = Ppo.bTrainOnGpu ? ELearningAgentsTrainingDevice::GPU : ELearningAgentsTrainingDevice::CPU;
		T.RandomSeed = Args.Seed;
		T.DiscountFactor = Ppo.Discount;
		T.GaeLambda = Ppo.GaeLambda;
		T.ActionEntropyWeight = Ppo.EntropyWeight;
		T.LearningRatePolicy = Ppo.LearningRatePolicy;
		T.LearningRateCritic = Ppo.LearningRateCritic;
		Session = NewObject<UTrainingSession>(this);
		if (!Session->Setup(*Manager, Config))
		{
			bExiting = true;
			return;
		}
		for (int32 i = 0; i < Arenas.Num(); ++i)
		{
			const int32 Id = Manager->AddAgent(Arenas[i]->GetAgent());
			if (!AgentToArena.IsValidIndex(Id))
			{
				Exit(1, FString::Printf(TEXT("manager returned agent id %d for arena %d"), Id, i));
				return;
			}
			AgentToArena[Id] = i;
		}
		UE_LOG(LogFPSAgent, Display, TEXT("Combat: ppo discount=%.4f gae=%.2f entropy=%.4f lr=%g/%g network=%dx%d+mem%d ticksPerDecision=%d resetOnUpdate=0"),
			Ppo.Discount, Ppo.GaeLambda, Ppo.EntropyWeight, Ppo.LearningRatePolicy, Ppo.LearningRateCritic, Ppo.HiddenLayers, Ppo.HiddenSize,
			Ppo.MemorySize, Ppo.TicksPerDecision);
	}

	for (int32 i = 0; i < Arenas.Num(); ++i) { StartNextEpisode(i); }
	bReady = true;
	NextDecisionTime = Now();
	NextStatsTime = Now() + S.Combat.Ppo.StatsLogIntervalSeconds;
	WindowWallStart = FPlatformTime::Seconds();
	UE_LOG(LogFPSAgent, Display, TEXT("Combat: ready. stage=S0 arenas=%d mode=%s resume=%s startDifficulty=%.2f decisionsPerSecond=%.1f timeLimit=%.0fs bots=%d..%d"),
		Arenas.Num(), bScripted ? TEXT("scripted (no policy)") : bEval ? TEXT("eval") : bInfer ? TEXT("infer") : TEXT("train (external trainer)"),
		Args.ResumeFrom.IsEmpty() ? TEXT("(none)") : *Args.ResumeFrom, Args.StartDifficulty, 1.0 / DecisionSeconds,
		S.Curriculum.S0.TimeLimitSeconds, S.Curriculum.S0.BotsEasy, S.Curriculum.S0.BotsHard);
}

UCombatArena* ACombatTaskActor::ArenaOf(int32 AgentId) const
{
	const int32 Index = AgentToArena.IsValidIndex(AgentId) ? AgentToArena[AgentId] : INDEX_NONE;
	return Arenas.IsValidIndex(Index) ? Arenas[Index].Get() : nullptr;
}

void ACombatTaskActor::StartNextEpisode(int32 ArenaIndex)
{
	UCombatArena* Arena = Arenas[ArenaIndex];
	const int32 Seed = Arena->NextEpisodeSeed();
	FRandomStream Rng(Seed);
	const FPSCurriculum::FS0Episode Config = FPSCurriculum::SampleS0(Rng, Difficulty[ArenaIndex].Level, /*bAllowReview=*/!bEval,
		UFPSAgentSettings::Get().Curriculum, Arena->GetBotRadiusCm());
	Arena->StartEpisode(Config, Seed, Now());
}

void ACombatTaskActor::RecordEpisode(int32 ArenaIndex, ECombatEpisodeEnd End)
{
	const FPSCombatStats::FEpisodeRecord R = Arenas[ArenaIndex]->FinishEpisode(End, Now());
	Window.Add(R);
	if (bEval)
	{
		EvalTotals.Add(R);
		if (EvalTotals.Episodes >= FFPSRunArgs::Get().EvalEpisodes)
		{
			FinishEval();
		}
		return;
	}
	if (!R.bReview)
	{
		Difficulty[ArenaIndex].Record(R.bCleared, UFPSAgentSettings::Get().Curriculum);
	}
}

void ACombatTaskActor::BuildObservation(int32 AgentId, FPSCombatObs::FObservationData& Out) const
{
	if (const UCombatArena* Arena = ArenaOf(AgentId))
	{
		Arena->BuildObservation(Out, Now());
	}
}

void ACombatTaskActor::SetAction(int32 AgentId, const FPSCombatObs::FAgentAction& Action)
{
	if (UCombatArena* Arena = ArenaOf(AgentId)) { Arena->SetAction(Action); }
}

float ACombatTaskActor::TakeDecisionReward(int32 AgentId)
{
	UCombatArena* Arena = ArenaOf(AgentId);
	return Arena ? Arena->TakeDecisionReward() : 0.f;
}

ECombatEpisodeEnd ACombatTaskActor::GetEpisodeEnd(int32 AgentId) const
{
	const UCombatArena* Arena = ArenaOf(AgentId);
	return Arena ? Arena->GetEpisodeEnd(Now()) : ECombatEpisodeEnd::Running;
}

void ACombatTaskActor::ResetAgentEpisode(int32 AgentId)
{
	const int32 Index = AgentToArena.IsValidIndex(AgentId) ? AgentToArena[AgentId] : INDEX_NONE;
	if (!Arenas.IsValidIndex(Index))
	{
		return;
	}
	const ECombatEpisodeEnd End = Arenas[Index]->GetEpisodeEnd(Now());
	// Running = a reset Learning Agents started itself (at the start of training, or its step cap): not a real episode end.
	if (End != ECombatEpisodeEnd::Running)
	{
		RecordEpisode(Index, End);
	}
	if (!bExiting)
	{
		StartNextEpisode(Index);
	}
}

void ACombatTaskActor::Decide()
{
	const double T = Now();
	for (UCombatArena* Arena : Arenas) { Arena->PrepareDecision(T); }
	WindowDecisions += Arenas.Num();

	if (!bInfer)
	{
		// Learning Agents: completions -> rewards -> resets -> observations -> actions (our callbacks above).
		switch (Session->RunTrainingStep())
		{
		case UTrainingSession::EStatus::Failed:
			LogStats();
			Exit(1, TEXT("training failed: trainer unreachable, timed out or crashed (see LogLearning above)"));
			return;
		case UTrainingSession::EStatus::Finished:
			LogStats();
			Exit(0, TEXT("trainer finished"));
			return;
		default:
			return;
		}
	}

	// Playback / eval / scripted: the same order as training, run by us.
	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		Arenas[i]->TakeDecisionReward();
		const ECombatEpisodeEnd End = Arenas[i]->GetEpisodeEnd(T);
		if (End != ECombatEpisodeEnd::Running)
		{
			RecordEpisode(i, End);
			if (bExiting) { return; }
			StartNextEpisode(i);
		}
	}
	if (bScripted)
	{
		for (UCombatArena* Arena : Arenas) { Arena->SetAction(Arena->ScriptedAction(static_cast<float>(DecisionSeconds))); }
	}
	else
	{
		Session->GetPolicy()->RunInference(0.f); // mean actions, no exploration noise
	}
}

void ACombatTaskActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady || bExiting)
	{
		return;
	}
	if (Now() + 1e-6 >= NextDecisionTime)
	{
		NextDecisionTime += DecisionSeconds;
		if (NextDecisionTime < Now()) { NextDecisionTime = Now() + DecisionSeconds; } // editor frame hitches: don't pile up decisions
		Decide();
		if (bExiting) { return; }
	}
	for (UCombatArena* Arena : Arenas) { Arena->TickAction(DeltaSeconds); }
	if (Now() >= NextStatsTime)
	{
		LogStats();
		NextStatsTime += UFPSAgentSettings::Get().Combat.Ppo.StatsLogIntervalSeconds;
	}
}

FPSCombatStats::FLevelSummary ACombatTaskActor::LevelSummary() const
{
	FPSCombatStats::FLevelSummary L;
	if (Difficulty.Num() == 0) { return L; }
	L.Min = 1.f;
	L.Max = 0.f;
	for (const FPSCurriculum::FDifficultyController& D : Difficulty)
	{
		L.Mean += D.Level;
		L.Min = FMath::Min(L.Min, D.Level);
		L.Max = FMath::Max(L.Max, D.Level);
	}
	L.Mean /= Difficulty.Num();
	return L;
}

void ACombatTaskActor::LogStats()
{
	const double Wall = FMath::Max(1e-6, FPlatformTime::Seconds() - WindowWallStart);
	const double DecisionsPerSecond = WindowDecisions / Wall;
	const FPSCombatStats::FLevelSummary L = LevelSummary();
	UE_LOG(LogFPSAgent, Display, TEXT("%s"), *FPSCombatStats::LogLine(Window, Now(), L, DecisionsPerSecond));
	const FString& Csv = FFPSRunArgs::Get().StatsCsv;
	if (!Csv.IsEmpty())
	{
		if (!bCsvStarted)
		{
			FFileHelper::SaveStringToFile(FPSCombatStats::CsvHeader() + TEXT("\n"), *Csv);
			bCsvStarted = true;
		}
		FFileHelper::SaveStringToFile(FPSCombatStats::CsvRow(Window, Now(), L, DecisionsPerSecond) + TEXT("\n"), *Csv,
			FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
	Window.Reset();
	WindowDecisions = 0;
	WindowWallStart = FPlatformTime::Seconds();
}

void ACombatTaskActor::FinishEval()
{
	const FFPSRunArgs& Args = FFPSRunArgs::Get();
	const float Gate = UFPSAgentSettings::Get().Curriculum.S0.PromotionClearRate;
	const FString Json = FPSCombatStats::EvalJson(EvalTotals, TEXT("S0"), Args.StartDifficulty, Args.Seed,
		bScripted ? FString(TEXT("(scripted)")) : Args.ResumeFrom, Gate);
	UE_LOG(LogFPSAgent, Display, TEXT("CombatEval: %s"), *Json);
	if (!Args.EvalJson.IsEmpty())
	{
		FFileHelper::SaveStringToFile(Json, *Args.EvalJson);
	}
	LogStats();
	Exit(0, FString::Printf(TEXT("eval finished: %d episodes, clear rate %.1f%% (gate %.0f%%)"),
		EvalTotals.Episodes, 100.0 * EvalTotals.ClearRate(), 100.0 * Gate));
}
```

- [ ] **Step 3: Dispatch `-Stage=S0..S3` in the game mode.**
  - In `Arena/ArenaGameMode.h`, add:
    - after `WantsSpikeA()`: `static int32 RequestedCombatStage();` with the comment `/** -Stage=S0..S3 or fps.Stage S0..S3: the combat stage index, else -1. */`;
    - under `protected:`: `void BuildCombat(UWorld* World);`.
  - In `Arena/ArenaGameMode.cpp`:
    1. Add `#include "Learning/CombatTaskActor.h"`.
    2. Change the `CVarStage` help text to `TEXT("Stage for the next Play: SpikeA = toy reach task, S0 = combat aim gallery (policy playback only in the editor). -Stage= does the same.")`.
    3. Add:

```cpp
int32 AArenaGameMode::RequestedCombatStage()
{
	const int32 FromArgs = FFPSRunArgs::Get().CombatStageIndex();
	return FromArgs >= 0 ? FromArgs : FFPSRunArgs::CombatStageIndexOf(CVarStage.GetValueOnGameThread());
}

void AArenaGameMode::BuildCombat(UWorld* World)
{
	// The combat task builds its own arenas, agents and bots. The local player only watches (spectator, no bean).
	ArenaName = TEXT("combat");
	bStartPlayersAsSpectators = true;
	Bounds = ACombatTaskActor::ComputeBounds(FFPSRunArgs::Get().Arenas, FVector::ZeroVector);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ACombatTaskActor>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	// Watch arena 0 (centred on the origin) from above its -X edge.
	const float Half = UCombatArena::FloorHalfSizeCm();
	PlayerStart = World->SpawnActor<APlayerStart>(FVector(-Half - 1500.0, 0.0, 2500.0), FRotator(-45.0, 0.0, 0.0), Params);
}
```

    4. In `InitGame`, right after the SpikeA `if (WantsSpikeA()) { ... return; }` block, add:

```cpp
	if (RequestedCombatStage() >= 0)
	{
		BuildCombat(GetWorld());
		FPSArena::SpawnPreviewLighting(GetWorld());
		UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s %s"), *ArenaName, *Args.ToString());
		return;
	}
```

    5. In `StartPlay`, change `if (ArenaName == TEXT("spikeA"))` to `if (ArenaName == TEXT("spikeA") || ArenaName == TEXT("combat"))` and its comment to `// the training tasks spawn their own agents and bots`.

- [ ] **Step 4: Build and run all tests.** Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1`. Expected: `Failed: 0`.

- [ ] **Step 5: Headless verification (no training).** Run these and keep the real output for Task 13:

```powershell
$ue = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "$PWD\Game\FPSAgent\FPSAgent.uproject"
$tmp = $env:TEMP
function Run($name, $extra, $seconds) {
  $log = "$tmp\$name.log"
  $p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended $extra -abslog=`"$log`"" -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit($seconds * 1000)) { Stop-Process -Id $p.Id -Force; "($name stopped after $seconds s)" } else { "($name exit=$($p.ExitCode))" }
  Select-String $log -Pattern "Combat: |CombatStats|CombatEval|LogLearning: Error|Error: " | Select-Object -Last 6 | ForEach-Object { $_.Line.Substring([Math]::Max(0, $_.Line.IndexOf('Log'))) }
}
# (a) scripted reference agent, 4 arenas, 90 s: CombatStats lines; the clear rate should be high and the ramp level should rise above 0
Run "s0_scripted" "-Stage=S0 -Arenas=4 -Seed=1 -Scripted -StatsCsv=`"$tmp\s0_scripted.csv`"" 90
Get-Content "$tmp\s0_scripted.csv" -TotalCount 3
# (b) scripted eval: exits 0 with a CombatEval JSON line; the JSON file exists
Run "s0_eval" "-Stage=S0 -Arenas=4 -Seed=1000 -Scripted -Eval -EvalEpisodes=40 -StartDifficulty=1 -EvalJson=`"$tmp\s0_eval.json`"" 180
Get-Content "$tmp\s0_eval.json"
# (c) untrained policy playback: no LogLearning errors (the observation/action layout is accepted); clear rate near 0
Run "s0_infer" "-Stage=S0 -Arenas=2 -Seed=2 -Infer" 45
# (d) no trainer: exit 1 after about 20 s with "Combat: exiting (code 1): no trainer connection"
Run "s0_notrainer" "-Stage=S0 -Arenas=2 -LearningAgentsUseExternalTrainingProcess -LearningAgentsPort=1 -LearningAgentsTimeout=5" 120
# (e) bad arguments: exit 2
Run "s0_badset" "-Stage=S0 -Scripted -Set=Combat.Reward.Nope=1" 60
Run "s1_missing" "-Stage=S1 -Scripted" 60
Run "s0_eval_nosnap" "-Stage=S0 -Eval" 60
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
```

  - Expected:
    - (a) `CombatStats: ... clear=` well above 50% and `level=` rising over time.
    - (b) `exit=0` with `"passed":` in the JSON.
    - (c) `CombatStats` lines and no `LogLearning: Error`.
    - (d) `exit=1`.
    - (e) all three `exit=2`.
  - Note (a)'s clear rate and the episode time; Task 13 records them as the S0 reference numbers.
  - **If (a) clears little:** look at `acc=` and `hs=` first. A head-height or eye-height mismatch shows up as many shots and few hits; fix it in `UCombatArena`/`ScriptedAction`, not in the reward.

- [ ] **Step 6: Watch it once in the editor (optional, visual).**
  - Open the project; in the console run `fps.Stage S0`, then Play.
  - Expected: you spectate arena 0. The untrained agent looks around while static bots stand on the floor.
  - Close the editor before building again.

- [ ] **Step 7: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatLearningClasses.* Game/FPSAgent/Source/FPSAgent/Learning/CombatTaskActor.* Game/FPSAgent/Source/FPSAgent/Arena/ArenaGameMode.*
git commit -m "[local] -Stage=S0 combat task: Learning Agents layer (struct/set/optional obs, struct action), K arenas at 15 decisions/s, per-arena ramp, CombatStats + -StatsCsv, -Eval JSON, -Scripted reference"
git pull --rebase && git push
```

---

### Task 12: `-Stage=SpikeC` headless mechanics probe (request C-006)

**Why:**
- Spike C on the cluster confirmed the fixed 60 Hz step, the uncapped tick and run speed.
- It could not measure walk/crouch speed or fire rate: no headless code path walks, crouches or fires, and our automation tests are editor-only.
- This probe is option (a) of C-006. It ships in build A, so the cluster can rerun Spike C without waiting for another build.

**Files:**
- Create: `Eval/SpikeCProbe.h`, `Eval/SpikeCProbe.cpp`
- Modify: `Common/FPSRunArgs.h` (`IsSpikeC()`), `Arena/ArenaGameMode.h/.cpp` (dispatch), `Tests/SettingsTests.cpp` (one assertion)

**Interfaces:**
- Consumes:
  - `FPSArena::MakeOpenFloorSpec` and `FPSArena::BuildMap` (Task 6 and existing);
  - `ABeanAIController` (Task 6);
  - `UTrainingSession::ExitProcess` (Task 10);
  - `URifleComponent::OnShotFired` (existing).
- Produces:
  - `ASpikeCProbe`;
  - `bool FFPSRunArgs::IsSpikeC() const`;
  - `static bool AArenaGameMode::WantsSpikeC()`.
- Log lines (for the cluster):
  - `SpikeC probe: started (tick ... s, fixed step 1)`;
  - `SpikeC probe: run=... (cfg ...) walk=... (cfg ...) crouch=... (cfg ...) cm/s; fireRate=... (cfg ...) rounds/s over N shots; taps=T/T; tick=... s; verdict=PASS|MISMATCH`;
  - `SpikeC: exiting (code 0): probe finished: PASS|MISMATCH ...`.

- [ ] **Step 1: Write the failing assertion.** In `Tests/SettingsTests.cpp` → `FFPSRunArgsTest`, before `return true;`, add:

```cpp
	TestTrue(TEXT("SpikeC probe stage"), FFPSRunArgs::Parse(TEXT("-Stage=spikec")).IsSpikeC());
	TestFalse(TEXT("SpikeC is not combat"), FFPSRunArgs::Parse(TEXT("-Stage=SpikeC")).IsCombatStage());
	TestFalse(TEXT("no stage: not the probe"), D.IsSpikeC());
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `'IsSpikeC': is not a member of 'FFPSRunArgs'`.

- [ ] **Step 3: Add the argument.** In `Common/FPSRunArgs.h`, after `IsSpikeA()`, add:

```cpp
	/** -Stage=SpikeC: the headless mechanics probe (C-006). */
	bool IsSpikeC() const { return Stage.Equals(TEXT("SpikeC"), ESearchCase::IgnoreCase); }
```

- [ ] **Step 4: Write the probe.** `Eval/SpikeCProbe.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpikeCProbe.generated.h"

class ABeanCharacter;
struct FShotResult;

/**
 * -Stage=SpikeC (request C-006): measures movement and rifle mechanics headless, in the packaged game, at the fixed
 * 60 Hz step used for training. One bean on an open floor:
 * - runs, walks and crouches in straight lines; the speed is averaged over the last second of each 3 s phase;
 * - holds the trigger: rounds/s over the first 20 shots;
 * - taps 8 times: shots per tap.
 * It logs measured vs configured values with a verdict, then exits 0. Pure measurement: no learning.
 */
UCLASS()
class FPSAGENT_API ASpikeCProbe : public AActor
{
	GENERATED_BODY()

public:
	ASpikeCProbe();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	enum class EPhase : uint8 { Settle, Run, Walk, Crouch, Stop, Hold, Taps, Done };

	void Enter(EPhase Next);
	void MoveAndMeasure(double PhaseTime, float& OutSpeed, EPhase Next);
	void OnShot(const FShotResult& Shot);
	void Finish();

	UPROPERTY() TObjectPtr<ABeanCharacter> Bean;

	EPhase Phase = EPhase::Settle;
	float TickSeconds = 1.f / 60.f;
	double PhaseStart = 0.0;
	double SpeedSum = 0.0;
	int32 SpeedSamples = 0;
	float RunSpeed = 0.f;
	float WalkSpeed = 0.f;
	float CrouchSpeed = 0.f;
	TArray<double> HoldShotTimes;
	int32 TapsRequested = 0;
	int32 TapShots = 0;
	double NextTapTime = 0.0;
	bool bTapHeld = false;
};
```

`Eval/SpikeCProbe.cpp`:

```cpp
#include "Eval/SpikeCProbe.h"
#include "Arena/FloorGenerator.h"
#include "Arena/MapBuilder.h"
#include "Character/BeanAIController.h"
#include "Character/BeanCharacter.h"
#include "Common/FPSAgentSettings.h"
#include "Engine/World.h"
#include "FPSAgent.h"
#include "HAL/IConsoleManager.h"
#include "Learning/TrainingSession.h"
#include "LearningAgentsTrainer.h"
#include "Misc/App.h"
#include "Weapons/RifleComponent.h"

namespace
{
	// The probe procedure (not mechanics): phase lengths, how many shots and taps are measured, and the pass tolerance.
	constexpr int32 ProbeFloorTiles = 20;        // 80 m floor
	constexpr float StartBackCm = 3000.f;        // start 30 m behind the centre; the three move phases cover about 31 m
	constexpr double SettleSeconds = 0.5;
	constexpr double MovePhaseSeconds = 3.0;
	constexpr double MeasureSeconds = 1.0;       // average speed over the last second of each move phase
	constexpr double StopSeconds = 1.0;
	constexpr double HoldSeconds = 3.0;
	constexpr int32 FireRateShots = 20;          // fewer than the 25-round magazine
	constexpr int32 TapCount = 8;
	constexpr double TapIntervalSeconds = 0.5;   // far slower than the fire rate: every tap should fire exactly one shot
	constexpr double ToleranceFraction = 0.02;   // 60 Hz tick quantisation is under 1% over 20 shots
}

ASpikeCProbe::ASpikeCProbe()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ASpikeCProbe::BeginPlay()
{
	Super::BeginPlay();
	TickSeconds = 1.f / FMath::Max(1.f, FLearningAgentsTrainingGameSettings().FixedTimeStepFrequency);
	if (!GIsEditor)
	{
		// The same fixed step as training and headless playback (D-019), uncapped.
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(TickSeconds);
		if (IConsoleVariable* MaxFps = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"))) { MaxFps->Set(0.f); }
	}
	const FMapSpec Spec = FPSArena::MakeOpenFloorSpec(ProbeFloorTiles, TEXT("spikeC"));
	FPSArena::BuildMap(GetWorld(), Spec, GetActorLocation() - Spec.Center());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector Start = GetActorLocation() + FVector(-StartBackCm, 0.0, UFPSAgentSettings::Get().Body.StandingHalfHeight + 2.f);
	Bean = GetWorld()->SpawnActor<ABeanCharacter>(Start, FRotator::ZeroRotator, Params);
	if (!Bean)
	{
		UTrainingSession::ExitProcess(TEXT("SpikeC"), 1, TEXT("could not spawn the probe bean"));
		Phase = EPhase::Done;
		return;
	}
	Bean->AIControllerClass = ABeanAIController::StaticClass();
	Bean->SpawnDefaultController(); // CharacterMovement only simulates controlled pawns
	Bean->GetRifle()->OnShotFired.AddUObject(this, &ASpikeCProbe::OnShot);
	Enter(EPhase::Settle);
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeC probe: started (tick %.5f s, fixed step %d)"), TickSeconds, FApp::UseFixedTimeStep() ? 1 : 0);
}

void ASpikeCProbe::Enter(EPhase Next)
{
	Phase = Next;
	PhaseStart = GetWorld()->GetTimeSeconds();
	SpeedSum = 0.0;
	SpeedSamples = 0;
	URifleComponent* Rifle = Bean->GetRifle();
	switch (Next)
	{
	case EPhase::Walk:
		Bean->SetWalking(true);
		break;
	case EPhase::Crouch:
		Bean->SetWalking(false);
		Bean->Crouch();
		break;
	case EPhase::Stop:
		Bean->UnCrouch();
		break;
	case EPhase::Hold:
		HoldShotTimes.Reset();
		Rifle->ResetWeapon();
		Rifle->SetTriggerHeld(true);
		break;
	case EPhase::Taps:
		Rifle->ResetWeapon(); // full magazine, trigger released
		TapsRequested = 0;
		TapShots = 0;
		bTapHeld = false;
		NextTapTime = PhaseStart + TapIntervalSeconds;
		break;
	default:
		break;
	}
}

void ASpikeCProbe::MoveAndMeasure(double PhaseTime, float& OutSpeed, EPhase Next)
{
	Bean->AddMovementInput(FVector::ForwardVector, 1.f);
	if (PhaseTime >= MovePhaseSeconds - MeasureSeconds)
	{
		SpeedSum += Bean->GetHorizontalSpeed();
		++SpeedSamples;
	}
	if (PhaseTime >= MovePhaseSeconds)
	{
		OutSpeed = SpeedSamples > 0 ? static_cast<float>(SpeedSum / SpeedSamples) : 0.f;
		Enter(Next);
	}
}

void ASpikeCProbe::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Bean || Phase == EPhase::Done)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const double T = Now - PhaseStart;
	switch (Phase)
	{
	case EPhase::Settle: if (T >= SettleSeconds) { Enter(EPhase::Run); } break;
	case EPhase::Run:    MoveAndMeasure(T, RunSpeed, EPhase::Walk); break;
	case EPhase::Walk:   MoveAndMeasure(T, WalkSpeed, EPhase::Crouch); break;
	case EPhase::Crouch: MoveAndMeasure(T, CrouchSpeed, EPhase::Stop); break;
	case EPhase::Stop:   if (T >= StopSeconds) { Enter(EPhase::Hold); } break;
	case EPhase::Hold:   if (T >= HoldSeconds) { Enter(EPhase::Taps); } break;
	case EPhase::Taps:
		if (bTapHeld)
		{
			Bean->GetRifle()->SetTriggerHeld(false); // a tap is one tick of trigger
			bTapHeld = false;
		}
		else if (TapsRequested < TapCount && Now >= NextTapTime)
		{
			Bean->GetRifle()->SetTriggerHeld(true);
			bTapHeld = true;
			++TapsRequested;
			NextTapTime = Now + TapIntervalSeconds;
		}
		else if (TapsRequested >= TapCount && Now >= NextTapTime)
		{
			Finish();
		}
		break;
	default:
		break;
	}
}

void ASpikeCProbe::OnShot(const FShotResult& Shot)
{
	if (Phase == EPhase::Hold)
	{
		HoldShotTimes.Add(GetWorld()->GetTimeSeconds());
	}
	else if (Phase == EPhase::Taps)
	{
		++TapShots;
	}
}

void ASpikeCProbe::Finish()
{
	Phase = EPhase::Done;
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	const int32 N = FMath::Min(HoldShotTimes.Num(), FireRateShots);
	const double Span = N >= 2 ? HoldShotTimes[N - 1] - HoldShotTimes[0] : 0.0;
	const double FireRate = Span > 0.0 ? (N - 1) / Span : 0.0;
	auto Within = [](double Measured, double Config) { return FMath::Abs(Measured - Config) <= ToleranceFraction * FMath::Max(1e-6, FMath::Abs(Config)); };
	const bool bPass = Within(RunSpeed, S.Movement.RunSpeed) && Within(WalkSpeed, S.Movement.WalkSpeed)
		&& Within(CrouchSpeed, S.Movement.CrouchSpeed) && Within(FireRate, S.Rifle.FireRate) && TapShots == TapsRequested;
	UE_LOG(LogFPSAgent, Display, TEXT("SpikeC probe: run=%.2f (cfg %.2f) walk=%.2f (cfg %.2f) crouch=%.2f (cfg %.2f) cm/s; fireRate=%.3f (cfg %.3f) rounds/s over %d shots; taps=%d/%d; tick=%.5f s; verdict=%s"),
		RunSpeed, S.Movement.RunSpeed, WalkSpeed, S.Movement.WalkSpeed, CrouchSpeed, S.Movement.CrouchSpeed,
		FireRate, S.Rifle.FireRate, N, TapShots, TapsRequested, TickSeconds, bPass ? TEXT("PASS") : TEXT("MISMATCH"));
	UTrainingSession::ExitProcess(TEXT("SpikeC"), 0, bPass ? TEXT("probe finished: PASS") : TEXT("probe finished: MISMATCH (see the probe line)"));
}
```

- [ ] **Step 5: Dispatch it in the game mode.**
  - In `Arena/ArenaGameMode.h`:
    - after `RequestedCombatStage()`, add `static bool WantsSpikeC();` with the comment `/** -Stage=SpikeC or fps.Stage SpikeC: the headless mechanics probe (C-006). */`;
    - under `protected:`, add `void BuildSpikeC(UWorld* World);`.
  - In `Arena/ArenaGameMode.cpp`:
    1. Add `#include "Eval/SpikeCProbe.h"`.
    2. Add:

```cpp
bool AArenaGameMode::WantsSpikeC()
{
	return FFPSRunArgs::Get().IsSpikeC() || CVarStage.GetValueOnGameThread().Equals(TEXT("SpikeC"), ESearchCase::IgnoreCase);
}

void AArenaGameMode::BuildSpikeC(UWorld* World)
{
	// The probe builds its own floor and bean; the local player only watches.
	ArenaName = TEXT("spikeC");
	bStartPlayersAsSpectators = true;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	World->SpawnActor<ASpikeCProbe>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	Bounds = FBox(FVector(-4400.0, -4400.0, 0.0), FVector(4400.0, 4400.0, 600.0));
	PlayerStart = World->SpawnActor<APlayerStart>(FVector(-3000.0, -1500.0, 800.0), FRotator(-25.0, 45.0, 0.0), Params);
}
```

    3. In `InitGame`, right after the SpikeA block, add:

```cpp
	if (WantsSpikeC())
	{
		BuildSpikeC(GetWorld());
		FPSArena::SpawnPreviewLighting(GetWorld());
		UE_LOG(LogFPSAgent, Log, TEXT("ArenaGameMode: arena=%s %s"), *ArenaName, *Args.ToString());
		return;
	}
```

    4. In `StartPlay`, extend the early return to `ArenaName == TEXT("spikeA") || ArenaName == TEXT("combat") || ArenaName == TEXT("spikeC")`.

- [ ] **Step 6: Build, test, and run the probe headless.**
  - Run `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Settings"`. Expected: `Failed: 0`.
  - Then:

```powershell
$ue = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "$PWD\Game\FPSAgent\FPSAgent.uproject"
$log = "$env:TEMP\spikec.log"
$p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended -Stage=SpikeC -abslog=`"$log`"" -PassThru -WindowStyle Hidden
$p.WaitForExit(180000); "exit=$($p.ExitCode)"
Select-String $log -Pattern "SpikeC" | ForEach-Object { $_.Line.Substring($_.Line.IndexOf('SpikeC')) }
```

  - Expected:
    - `exit=0`;
    - `run≈540 walk≈300 crouch≈190`;
    - `fireRate≈9.75`;
    - `taps=8/8`;
    - `verdict=PASS`.
  - **If a value is off:** report it as found (that's what the probe is for). Don't tune the probe to make it pass. Record the real line for Task 13; the cluster compares it against the Linux run.

- [ ] **Step 7: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Eval Game/FPSAgent/Source/FPSAgent/Common/FPSRunArgs.h Game/FPSAgent/Source/FPSAgent/Arena/ArenaGameMode.* Game/FPSAgent/Source/FPSAgent/Tests/SettingsTests.cpp
git commit -m "[local] -Stage=SpikeC headless mechanics probe (C-006): run/walk/crouch speeds, hold fire rate, taps; logs measured vs config and exits 0"
git pull --rebase && git push
```

---

### Task 13: Document, package and ship build A

**Files:**
- Modify: `README.md`, `docs/decisions.md`, `docs/superpowers/specs/2026-11-12-combat-training-design.md`, `docs/builds.md`, `docs/STATUS.md`

- [ ] **Step 1: README → *Game command-line arguments*.** Replace the `-Stage=S0..S3` row with the rows below:

```markdown
| `-Stage=S0` | **Combat stage S0 (aim gallery)**: `-Arenas` generated 80 m floors, one agent plus 1-5 static bots each, 15 decisions per second (D-023), automatic difficulty ramp per arena. Trains against the external trainer unless `-Infer` / `-Eval` / `-Scripted`. S1-S3 come in later builds (asking for them exits with code 2). No `-Stage` = the play arena (range or map). |
| `-StartDifficulty=X` | Initial ramp level of every arena, 0-1 (default 0). Chained jobs pass the last logged mean `level=`. |
| `-Eval` | Promotion test (implies `-Infer`): `-EvalEpisodes=N` (default 200) fixed-seed episodes at `-StartDifficulty`, mean actions, no ramp, no review episodes. Writes `-EvalJson=<path>`, logs `CombatEval: {...}`, exits 0. Needs `-ResumeFrom` unless `-Scripted`. |
| `-StatsCsv=<path>` | One CSV row per stats window: clear/death/timeout rates, average return, every reward term, accuracy, headshot rate, clear time, ramp level (mean/min/max), decisions/s. |
| `-Set=Path=Value;...` | Override any setting at startup, e.g. `-Set="Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997"` (D-025). Logged as `Settings overrides (-Set): ...`. An unknown path or bad value exits with code 2. Changing ray counts or network sizes changes the network, so older snapshots can't be resumed. |
```

  Also add this row:

```markdown
| `-Stage=SpikeC` | Headless mechanics probe (C-006): one bean runs, walks and crouches (speed over the last second of each 3 s phase), then holds the trigger (rounds/s over 20 shots) and taps 8 times. Logs `SpikeC probe: run=... (cfg ...) ... verdict=PASS|MISMATCH` and exits 0. |
```

  Then make these edits:
  - Change the `-Scripted` row to: `(implies -Infer) A reference agent instead of the policy. SpikeA: walks straight to the target. S0: turns to the nearest visible enemy at the turn-rate cap and fires when on the head. Same senses as the policy.`
  - Change the exit-codes line to: `**Game exit codes (training stages):** 0 = the trainer finished or -Eval completed · 1 = trainer unreachable, timed out or crashed · 2 = bad arguments (-ResumeFrom files, -Set, a stage not in this build, -Eval without snapshots).`
  - Under *Game log lines to grep*, add:
    - `Combat: ready. ...`
    - `Combat: ppo ...`
    - `Combat: trainer settings: ...`
    - `CombatStats: gameTime=... episodes=... clear=..% died=..% timeout=..% avgReturn=... kills/ep=... acc=..% hs=..% clearTime=..s level=mean(min..max) decisions/s(wall)=... terms: DamageDealt=... Kill=... FirstSighting=... Clear=... AimHelper=... DamageTaken=... Death=... Miss=... Time=...` (every 10 s of game time)
    - `CombatEval: {json}`
    - `Combat: exiting (code N): ...`
  - In *How a training job works*, delete the sentence "`-Stage=S0` doesn't exist yet; today only `-Stage=SpikeA` trains." and add `-StatsCsv="$RUN/stats_$i.csv"` to the game command in the sketch.

- [ ] **Step 2: Decisions.** Append to `docs/decisions.md`:

```markdown
| D-025 | 2026-11-12 | **`-Set=Path=Value;...` overrides any `UFPSAgentSettings` field at startup** (reflection; numbers, bools, struct text). It's applied once in `AArenaGameMode::InitGame`, before anything reads the settings. An unknown path or bad value exits with code 2. | Sweeps over reward weights and PPO knobs need no new build (spec §6); a typo must fail loudly, not train with defaults. |
| D-026 | 2026-11-12 | **Combat training passes `bResetAgentsOnUpdate=false`** to `RunTraining`. A reset that Learning Agents starts while our episode is still running (start of training, its step cap) is not recorded as an episode and not counted by the ramp. Spike A keeps `true`. | Combat episodes last up to 20-90 s. Resetting every arena at each policy update would cut most of them short and feed the difficulty ramp false failures. |
```

- [ ] **Step 3: Spec.**
  - In the spec's §2 *Human-likeness settings* line, append: `Build A clarification: implemented in a later build. They don't change the observation or action layout, so S0 snapshots stay valid.`
  - In the spec's status block, add: `- Build A (S0) implemented 2026-11-12 (plan docs/superpowers/plans/2026-11-12-combat-build-a-s0.md); shipped as the build in docs/builds.md.`

- [ ] **Step 4: Commit the docs (before packaging, so the build's commit includes them).**

```bash
git add README.md docs/decisions.md docs/superpowers/specs/2026-11-12-combat-training-design.md
git commit -m "[local] Docs for build A: -Stage=S0, -StartDifficulty, -Eval, -StatsCsv, -Set; Combat log lines; D-025, D-026"
git pull --rebase && git push
```

- [ ] **Step 5: Package and publish** (editor closed, clean tree, laptop on AC power).
  - Run: `powershell -File tools/package_linux.ps1 -Publish`
  - Expected:
    - `BUILD SUCCESSFUL`;
    - `Tarball: ...FPSAgent-Linux-<sha>.tar.gz (... MB)`;
    - `Published build-<sha>`.
  - Then check the release: `gh release view build-<sha> --json assets --jq '.assets[].name'`. It should list the tarball and `SHA256SUMS`.

- [ ] **Step 6: Record the build and file the cluster request.**
  - In `docs/builds.md`, add a row above `build-07eac52` with the real sha, size and hash. In "What changed / what to test", include:
    - Combat build A (`-Stage=S0`), all new args, reward terms and the ramp;
    - **checked locally**: test count; the scripted S0 numbers from Task 11 Step 5 (a) and (b); untrained playback ran clean; exit codes 1 and 2 verified.
  - In `docs/STATUS.md`:
    - set *Latest build* to the new tag;
    - set the LOCAL focus to "build A shipped";
    - add, under *Requests: LOCAL → CLUSTER*:

```markdown
| L-007 | 2026-11-12 | **Combat build A (S0 aim gallery) on `build-<sha>`.** (1) Smoke: 2 games × 16 arenas, `-Stage=S0 -Batches=3 -StatsCsv=...`; expect `Combat: ready`, `CombatStats` lines and the 4 snapshots. (2) **Spike B on the combat build:** N games × K arenas (e.g. N ∈ {4, 8, 16}, K ∈ {8, 16, 32}) with `--batch-size N`; record decisions/s, seconds per batch and GPU use; pick defaults. (3) **S0 training:** chained jobs with `-Batches`, `-ResumeFrom=<previous snapshots>` and `-StartDifficulty=<last mean level>`, `-StatsCsv`. Promote when the mean `level=` stays ≥ 0.9 for 10 windows. (4) **Promotion test:** `-Eval -EvalEpisodes=200 -StartDifficulty=1 -Seed=1000 -ResumeFrom=<snapshots> -EvalJson=...`, plus the reference `-Scripted -Eval` with the same seed. Pass = clear rate ≥ 70%. Report in `docs/runs/`, publish the promoted snapshot as `run-<id>`. New args and log lines: README → *Game command-line arguments*. Sweeps can use `-Set=` (D-025). | open | |
```

  - Also in `docs/STATUS.md`, answer the CLUSTER requests (Status/Answer cells only):
    - **C-006 → done:** `-Stage=SpikeC` probe in the new build. Paste the local probe line from Task 12 Step 6 and ask CLUSTER to rerun Spike C with it.
    - **C-007 → done for (2) and (3); (1) waits on the user:**
      - (2) `Combat.Ppo.IterationsPerGather`, `PolicyBatchSize`, `CriticBatchSize`, `ReplayMaxSteps` and `ReplayMaxEpisodes` are settings, sweepable with `-Set=`.
      - (3) The 5.7 trainer only puts *finished* episodes into the replay buffer (`ULearningAgentsPPOTrainer::ProcessExperience`). It adds whole episodes while they fit, then one truncated part (`FReplayBuffer::AddEpisodes`), then sends. With `bResetAgentsOnUpdate=false` (D-026), unfinished episodes stay in the per-agent episode buffer across the update and are added when they end. S0 episodes are at most 300 decisions (20 s × 15 Hz), so 32 arenas give about 9,600 steps per round, under `ReplayMaxSteps` 10,000. Raise `ReplayMaxSteps` for longer stages or more arenas.
      - (1) The poll patch changes Epic's verbatim trainer files, so it is the user's decision; say so.
  - In `docs/builds.md`, change the `build-07eac52` status to `runs on Linux (L-005, Spike B/C)`.

- [ ] **Step 7: Commit and push**

```bash
git add docs/builds.md docs/STATUS.md
git commit -m "[local] Publish build-<sha> (combat build A, S0); L-007 smoke + Spike B on combat + S0 training + promotion test"
git pull --rebase && git push
```

---

## Self-review (done while writing)
- **Spec coverage (build A row of spec §6):**
  - shared session helper → Task 10
  - settings → Task 1
  - generated open floors → Task 6
  - `CombatTaskActor` → Task 11
  - the full §2 layout including hearing and the hit indicator, empty in S0 → Tasks 7, 9, 11
  - every §3 reward term → Tasks 3, 9
  - ramp and sampler → Task 4
  - Static bots → Task 6
  - `-Scripted`, `-Eval`, `-Set`, `-StatsCsv`, `-StartDifficulty` → Tasks 1, 2, 7, 8, 11
  - tests → every task
  - C-006 (SpikeC probe) → Task 12; C-007 (2) batch settings → Tasks 1 and 11; C-007 (3) answered in Task 13
  - docs before and after → Tasks 0, 12
  - D-022 (state-only completion) → Tasks 9 and 11 (`GetEpisodeEnd`, env completion)
  - death beats a clear → Task 9 test
  - geometry-only rays and line of sight → Task 5 world test
- **Deferred, with a reason recorded in the spec (Task 13):** human-likeness reaction delay and aim noise. No layout impact.
- **Type consistency:** names used across tasks have been checked against their definitions:
  - `FPSCombatObs::FObservationData` / `FAgentAction` / `ScriptedAction`
  - `UCombatArena::ScriptedAction(float)`
  - `ECombatEpisodeEnd`
  - `FPSCurriculum::FS0Episode`
  - `FTrainingSessionConfig` / `UTrainingSession::EStatus`
  - `FFPSRunArgs::CombatStageIndexOf`
  - `AArenaGameMode::RequestedCombatStage`
  - `UCombatArena::FloorHalfSizeCm`
