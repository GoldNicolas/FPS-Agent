# Combat build B (S1 "Movers") Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship build B: combat stage **S1 "Movers"**, plus three fixes found in S0:
- **The stage:** the agent moves and fights 1–5 non-shooting scripted bots on generated floors with cover.
- **The bots:** they strafe, peek, crouch, patrol, explore and investigate.
- **Senses:** fair sight and hearing on both sides.
- **Carry-over fixes:**
  - a gate-level clear rate for the promotion trigger;
  - a `-ResumeFrom` network shape check;
  - beans teleporting with the wrong crouch capsule.

**Architecture:**
- **Pure, world-free modules carry the logic and are unit-tested:** `TileGraph` with A*, the cover floor generator, bot senses, the bot brain, and the S1 episode sampler.
- **`UCombatArena`** wires them to the beans, a pool of movable cover blocks and per-arena sound events. `ACombatTaskActor` dispatches S0 or S1.
- **The observation and action layouts don't change** (D-023, D-028), so S0 snapshots resume in S1.

**Tech Stack:** Unreal Engine 5.7.4 C++, Learning Agents 5.7 (PPO, external trainer), headless automation tests, PowerShell tools.

**Spec:** `docs/superpowers/specs/2026-11-12-combat-training-design.md`. This plan implements:
- §4 *Scripted bots*;
- the S1 row of §5;
- the build B row of §6: TileGraph and A*, the floor generator with cover, Strafe / Peek / Crouch / Patrol / Explore / Investigate, footsteps and hearing.

## Global Constraints
- **Unreal Engine 5.7.4.** Check Learning Agents names against the 5.7 headers (`C:\Program Files\Epic Games\UE_5.7\Engine\Plugins\Experimental\LearningAgents\Source`).
- **C++ only** for logic.
  - Includes are relative to the module root (`"Arena/TileGraph.h"`).
  - `FVector`, `FVector2D` and `FRotator` are double: cast to float in `TestEqual`.
  - New UPROPERTY values carry a source or `verify` note (hard rule 7).
- **Everything works under `-nullrhi -nosound`** (hard rule 2).
- **Fair observations** (hard rule 4 and D-024):
  - Enemies are seen only through line of sight inside the 103° × 70.5° view cone, or a memory of 2 s or less.
  - **Hearing** covers enemy *running* footsteps within 15 m and gunshots within 40 m. Walking and crouching are silent.
  - Bots use the same rules (spec §4).
  - Rays and line-of-sight traces hit level geometry only.
- **Arenas never interact** (hard rule 3). Sounds and senses use per-arena buffers; never filter them by distance alone.
- **Determinism.** All episode randomness comes from the episode seed (`FPSReach::EpisodeSeed`): cover layout, spawns, behaviours, and each bot's RNGs (`HashCombineFast(seed, bot index + 1)` plus fixed salts).
- **Layout frozen.** Self 15, rays 72, enemy 15 (×5), hit 4, sound 5 (×4); the action struct is unchanged. No network change, so S0 and S0-move snapshots resume in S1.
- **Inherited decisions:**
  - D-022: an episode's end is a pure function of state, and one-off rewards are paid once.
  - D-023: 15 decisions/s, discount 0.995.
  - D-026: `bResetAgentsOnUpdate=false`.
  - D-028: every combat end is a Termination.
- **No local training** (hard rule 6). Local checks are tests plus headless `-Scripted`, `-Infer` and `-Eval` runs.
- **Build and test** with the editor closed: `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1 [-Filter <prefix>]`.
- **Commits:**
  - Prefix `[local]`. Use your session's `Co-Authored-By` trailer.
  - Run `git pull --rebase`, then `git push`.
  - Never force-push or amend a pushed commit.
  - Leave `docs/STATUS.md` to the controller.
- **Exit codes:**
  - `0` = finished.
  - `1` = trainer failure.
  - `2` = bad arguments: `-ResumeFrom` files, a snapshot that doesn't match the network, `-Set`, a stage not in this build (S2/S3), `-Eval` without snapshots, or overlapping arenas.
- **Test-world facts** (`Tests/FPSTestWorld.h`). The test world never initializes actors, so:
  - `PostInitializeComponents`, `BeginPlay` and component ticks never run;
  - bots get no controller;
  - `W.Step()` moves nothing.

  World tests therefore assert *pending movement input* (`GetPendingMovementInputVector()`), placement and state, never walked distances. Real movement is checked in the headless runs.

## Design rulings for this build (recorded as D-031 in Task 0)
1. **S1 spawn rule** ("the agent spawns on a tile no bot can see", spec §5):
   - Bots are placed relative to the agent with the stage's distance and spread knobs, as in S0.
   - Each bot then **faces away**, so the agent is outside its 103° view cone.
   - This holds even on an open floor at difficulty 0, where cover can't hide the agent.
2. **Cover changes per episode without spawning geometry.**
   - Each arena builds its open floor once.
   - It spawns a pool of movable cover blocks, sized for the densest layout.
   - Each episode places them on that episode's cover tiles.
3. **Investigate moves only the movers** (Patrol, Explore). Static, Strafe, Peek and Crouch bots turn to face the stimulus and hold their spot, so easy bots stay predictable.
4. **Peek needs cover.** A peeking bot needs a hidden tile and an adjacent exposed tile, judged by tile line of sight from the agent's spawn tile. Without such a pair nearby, it strafes instead.
5. **Agent hearing.** An enemy sound stays in the agent's observation for `Bots.SoundMemorySeconds` (1 s): newest first, up to 4, direction in the view frame, rough distance and age.
6. **Gate-level clear rate.**
   - `CombatStats` logs `clearTop`: the clear rate of non-review episodes at a level of at least `Curriculum.GateLevel` (0.95).
   - The promotion trigger becomes "mean level ≥ 0.9 **and** `clearTop` ≥ the stage's gate, for 10 windows". This answers L-007 note (a).
7. **`-Scripted` in S1** ("moves with the bots' Hunt logic", spec §6; Hunt itself is S3):
   - With an enemy in sight, the reference agent stands still and shoots, using the S0 logic.
   - Otherwise it explores like an Explore bot and investigates the newest enemy sound it hears.

## File structure
| File | Responsibility |
|---|---|
| `Common/FPSAgentTypes.h` (modify) | `FBotBehaviourMix`, the S1 knobs in `FCombatStageSpec`, `MakeS1StageSpec()`, `FBotBehaviourSettings`, `FCurriculumSettings::S1/GateLevel/Stage()` |
| `Common/FPSAgentSettings.h` (modify) | `UFPSAgentSettings::Bots` |
| `Arena/TileGraph.h/.cpp` (new) | `FPSNav`: walkable graph, one-way edges, strongly connected components, A*, 2.5D tile line of sight, tile↔position helpers |
| `Arena/FloorGenerator.h/.cpp` (modify) | `MakeCoverFloorSpec` |
| `Bots/BotSenses.h/.cpp` (new) | `FPSBots`: sound events, audibility, hearing radius, `FBotSenses` (reaction delay, memory, noisy hearing) |
| `Bots/BotBrain.h/.cpp` (new) | `FPSBots`: behaviours, intent, path following, investigate |
| `Learning/CombatObservation.h/.cpp` (modify) | `PackSound` |
| `Learning/Curriculum.h/.cpp` (modify) | `FBotSpawn`, `FCombatEpisode`, `FromS0`, `SampleS1` |
| `Character/BeanCharacter.h/.cpp` (modify) | `SetSpeedScale`, `ApplyCrouchNow` |
| `Bots/ScriptedBot.h/.cpp` (modify) | `Activate(..., SpeedScale)`, crouch applied at once, `Drive(intent)` |
| `Learning/CombatArena.h/.cpp` (modify) | Stage index, cover pool, per-episode tile graph, bot brains and senses, sound events, agent hearing, scripted S1 reference |
| `Learning/CombatTaskActor.h/.cpp`, `Arena/ArenaGameMode.cpp` (modify) | S0/S1 dispatch, stage names, S1 sampler |
| `Learning/CombatStats.h/.cpp` (modify) | `clearTop` |
| `Learning/TrainingSession.cpp` (modify) | Snapshot shape check |
| Tests | `TileGraphTests.cpp`, `BotSensesTests.cpp`, `BotBrainTests.cpp` (new); additions to `SettingsTests`, `CombatBodyTests`, `CombatObservationTests`, `CurriculumTests`, `CombatArenaTests`, `CombatStatsTests` |

---

### Task 0: Document before coding (controller)
- [ ] Commit this plan.
- [ ] Add **D-031** (the design rulings above) to `docs/decisions.md`.
- [ ] In `docs/STATUS.md`, set the LOCAL focus to "build B plan committed; implementing".
- [ ] Push.

---

### Task 1: S1 stage settings and the bot behaviour settings

**Files:**
- Modify: `Common/FPSAgentTypes.h`, `Common/FPSAgentSettings.h`
- Test: `Tests/SettingsTests.cpp`

**Interfaces:**
- Produces:
  - `struct FBotBehaviourMix { float Static, Strafe, Peek, Crouch, Patrol, Explore; }`.
  - `FCombatStageSpec` gains `CoverDensityEasy/Hard`, `LowCoverFraction`, `BotSpeedScaleEasy/Hard`, `BehaviourMixEasy/Hard`.
  - `FCombatStageSpec MakeS1StageSpec()`.
  - `struct FBotBehaviourSettings` (fields below).
  - `FCurriculumSettings::S1`, `GateLevel` and `const FCombatStageSpec& Stage(int32 Index) const`.
  - `UFPSAgentSettings::Bots`.

- [ ] **Step 1: Write the failing test.** In `Tests/SettingsTests.cpp` → `FFPSSettingsDefaultsTest`, before `return true;`, add:

```cpp
	const FCombatStageSpec& S1 = S.Curriculum.S1;
	TestEqual(TEXT("S1 time limit 30 s"), S1.TimeLimitSeconds, 30.f);
	TestTrue(TEXT("S1 agent moves"), S1.bAgentCanMove);
	TestEqual(TEXT("S1 bots at 60% speed when easy"), S1.BotSpeedScaleEasy, 0.6f);
	TestEqual(TEXT("S1 bots at full speed when hard"), S1.BotSpeedScaleHard, 1.f);
	TestTrue(TEXT("S1 cover 0 -> medium"), S1.CoverDensityEasy == 0.f && S1.CoverDensityHard > 0.f && S1.CoverDensityHard <= 0.2f);
	TestEqual(TEXT("S1 easy mix has no movers"), S1.BehaviourMixEasy.Patrol + S1.BehaviourMixEasy.Explore, 0.f);
	TestTrue(TEXT("S1 hard mix adds patrol and explore"), S1.BehaviourMixHard.Patrol > 0.f && S1.BehaviourMixHard.Explore > 0.f);
	TestEqual(TEXT("S0 bots are static"), S.Curriculum.S0.BehaviourMixHard.Static, 1.f);
	TestTrue(TEXT("Stage(1) is S1"), &S.Curriculum.Stage(1) == &S.Curriculum.S1);
	TestTrue(TEXT("Stage(0) is S0"), &S.Curriculum.Stage(0) == &S.Curriculum.S0);
	TestEqual(TEXT("gate level 0.95"), S.Curriculum.GateLevel, 0.95f);
	const FBotBehaviourSettings& B = S.Bots;
	TestEqual(TEXT("bot view cone = the agent's"), B.SightFovDeg, C.Perception.FieldOfViewDeg);
	TestEqual(TEXT("bots remember 3 s"), B.SightMemorySeconds, 3.f);
	TestEqual(TEXT("gunshots heard within 40 m"), B.GunshotHearingCm, 4000.f);
	TestEqual(TEXT("running footsteps heard within 15 m"), B.FootstepHearingCm, 1500.f);
	TestTrue(TEXT("arenas far enough apart for S1"), C.ArenaSpacingCm >= (S1.FloorTiles + 2) * 400.f + 5000.f);
```

- [ ] **Step 2: Build to verify it fails.** Run `powershell -File tools/build.ps1`. Expected: `'S1': is not a member of 'FCurriculumSettings'` (or similar).

- [ ] **Step 3: Implement.** In `Common/FPSAgentTypes.h`, directly **before** `struct FCombatStageSpec`, add:

```cpp
/** Sampling weights of the scripted-bot behaviours (spec §4), lerped between a stage's easy and hard mix by the difficulty level. */
USTRUCT(BlueprintType)
struct FBotBehaviourMix
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="Bots") float Static = 1.f;
	UPROPERTY(EditAnywhere, Category="Bots") float Strafe = 0.f;
	UPROPERTY(EditAnywhere, Category="Bots") float Peek = 0.f;
	UPROPERTY(EditAnywhere, Category="Bots") float Crouch = 0.f;
	UPROPERTY(EditAnywhere, Category="Bots") float Patrol = 0.f;
	UPROPERTY(EditAnywhere, Category="Bots") float Explore = 0.f;
};
```

  Inside `struct FCombatStageSpec`, after `CrouchedFractionHard`, add:

```cpp
	/** Cover tiles as a fraction of the interior floor (spec §5, S1: 0 -> "medium"; verify in S1 training). S0: none. */
	UPROPERTY(EditAnywhere, Category="Stage") float CoverDensityEasy = 0.f;
	UPROPERTY(EditAnywhere, Category="Stage") float CoverDensityHard = 0.f;
	/** Share of the cover tiles that are low (120 cm, crouch to hide); the rest are high (250 cm). */
	UPROPERTY(EditAnywhere, Category="Stage") float LowCoverFraction = 0.5f;
	/** Bot movement speed as a fraction of the run speed (spec §5, S1: 60% -> 100%). */
	UPROPERTY(EditAnywhere, Category="Stage") float BotSpeedScaleEasy = 1.f;
	UPROPERTY(EditAnywhere, Category="Stage") float BotSpeedScaleHard = 1.f;
	/** Behaviour mix at difficulty 0 and 1 (default: static only, as in S0). */
	UPROPERTY(EditAnywhere, Category="Stage") FBotBehaviourMix BehaviourMixEasy;
	UPROPERTY(EditAnywhere, Category="Stage") FBotBehaviourMix BehaviourMixHard;
```

  Directly **after** `struct FCombatStageSpec { ... };`, add:

```cpp
/** S1 "Movers" (spec §5): the agent moves; 1 -> 5 bots that strafe/peek/crouch, then also patrol/explore; speed 60% -> 100%; cover 0 -> medium; 30 s. */
inline FCombatStageSpec MakeS1StageSpec()
{
	FCombatStageSpec S;
	S.TimeLimitSeconds = 30.f;
	S.AimHelperWeight = 0.2f;
	S.bAgentCanMove = true;
	S.CoverDensityEasy = 0.f;
	S.CoverDensityHard = 0.12f; // "medium": 48 of the 400 interior tiles (verify in S1 training)
	S.BotSpeedScaleEasy = 0.6f;
	S.BotSpeedScaleHard = 1.f;
	S.BehaviourMixEasy.Static = 0.f;
	S.BehaviourMixEasy.Strafe = 1.f;
	S.BehaviourMixEasy.Peek = 1.f;
	S.BehaviourMixEasy.Crouch = 1.f;
	S.BehaviourMixHard = S.BehaviourMixEasy;
	S.BehaviourMixHard.Patrol = 1.5f;
	S.BehaviourMixHard.Explore = 1.5f;
	return S;
}

/** Scripted-bot senses and behaviour tuning (spec §4). The senses use the same rules as the agent's (hard rule 4, D-024). */
USTRUCT(BlueprintType)
struct FBotBehaviourSettings
{
	GENERATED_BODY()
	/** View cone for spotting the agent: the same 103 deg as the agent's (spec §4). */
	UPROPERTY(EditAnywhere, Category="Senses") float SightFovDeg = 103.f;
	/** A sighting or a heard sound is acted on only after this delay (S1 constant; S2 makes it a stage knob; verify). */
	UPROPERTY(EditAnywhere, Category="Senses") float ReactionSeconds = 0.4f;
	/** How long a bot remembers where it last saw or heard the agent (spec §4: about 3 s). */
	UPROPERTY(EditAnywhere, Category="Senses") float SightMemorySeconds = 3.f;
	/** Gunshots are heard within 40 m and running footsteps within 15 m; walking and crouching are silent (spec §4; verify). */
	UPROPERTY(EditAnywhere, Category="Senses") float GunshotHearingCm = 4000.f;
	UPROPERTY(EditAnywhere, Category="Senses") float FootstepHearingCm = 1500.f;
	/** A heard sound gives its position with this much seeded noise (radius). */
	UPROPERTY(EditAnywhere, Category="Senses") float HearingNoiseCm = 200.f;
	/** A running bean makes one footstep sound event per this many seconds. */
	UPROPERTY(EditAnywhere, Category="Senses") float FootstepIntervalSeconds = 0.35f;
	/** "Running" = faster than WalkSpeed x this, while not walking and not crouched. */
	UPROPERTY(EditAnywhere, Category="Senses") float SilentSpeedFraction = 1.05f;
	/** The agent keeps a heard enemy sound in its observation this long (D-031). */
	UPROPERTY(EditAnywhere, Category="Senses") float SoundMemorySeconds = 1.f;
	/** Strafe: ADAD flip period and how far from the spawn spot it may drift sideways. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float StrafePeriodMinSeconds = 0.3f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float StrafePeriodMaxSeconds = 0.9f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float StrafeHalfWidthCm = 250.f;
	/** Peek: time spent out of cover / behind cover before switching. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float PeekPeriodMinSeconds = 0.8f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float PeekPeriodMaxSeconds = 2.f;
	/** Crouch: crouch-spam toggle period. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float CrouchPeriodMinSeconds = 0.3f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float CrouchPeriodMaxSeconds = 1.2f;
	/** Patrol: number of waypoints in the loop and the pause at each. */
	UPROPERTY(EditAnywhere, Category="Behaviour") int32 PatrolWaypoints = 4;
	UPROPERTY(EditAnywhere, Category="Behaviour") float PatrolPauseMinSeconds = 0.5f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float PatrolPauseMaxSeconds = 2.f;
	/** Explore: random candidate tiles per pick; the least recently visited wins. */
	UPROPERTY(EditAnywhere, Category="Behaviour") int32 ExploreCandidates = 6;
	/** Investigate: look around for this long at the target, turning at this rate. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float InvestigateLookSeconds = 3.f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float InvestigateTurnDegPerSec = 120.f;
	/** Path following: a waypoint counts as reached within this distance; waypoints get a seeded offset up to this size. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float WaypointToleranceCm = 60.f;
	UPROPERTY(EditAnywhere, Category="Behaviour") float PathOffsetCm = 50.f;
	/** No progress toward a waypoint for this long: skip it. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float StuckSeconds = 2.f;
	/** How fast a bot turns its body toward where it wants to look. */
	UPROPERTY(EditAnywhere, Category="Behaviour") float TurnRateDegPerSec = 360.f;
};
```

  Inside `struct FCurriculumSettings`, after `S0`, add:

```cpp
	UPROPERTY(EditAnywhere, Category="Curriculum") FCombatStageSpec S1 = MakeS1StageSpec();
	/** Non-review episodes at or above this level count toward the gate-level clear rate `clearTop` (promotion trigger, D-031). */
	UPROPERTY(EditAnywhere, Category="Curriculum") float GateLevel = 0.95f;

	/** The spec of combat stage Index (0 = S0, 1 = S1). */
	const FCombatStageSpec& Stage(int32 Index) const { return Index == 1 ? S1 : S0; }
```

  In `Common/FPSAgentSettings.h`, after the `Curriculum` line, add:

```cpp
	UPROPERTY(Config, EditAnywhere, Category="Learning") FBotBehaviourSettings Bots;
```

- [ ] **Step 4: Build and test.**
  - Run `powershell -File tools/build.ps1`.
  - Run `powershell -File tools/run_tests.ps1 -Filter "FPSAgent.Settings"`. Expected: `Failed: 0`.
  - Then run the full suite. Expected: 55 tests, 0 failed.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Common/FPSAgentTypes.h Game/FPSAgent/Source/FPSAgent/Common/FPSAgentSettings.h Game/FPSAgent/Source/FPSAgent/Tests/SettingsTests.cpp
git commit -m "[local] S1 stage settings (cover, bot speed, behaviour mix, 30 s) and bot senses/behaviour settings; gate level"
git pull --rebase && git push
```

---

### Task 2: `TileGraph`: walkable graph, A* and tile line of sight

**Files:**
- Create: `Arena/TileGraph.h`, `Arena/TileGraph.cpp`
- Test: `Tests/TileGraphTests.cpp`

**Interfaces:**
- Consumes: `FMapSpec` (`IsWalkable`, `IsRamp`, `RampDir`, `LevelAt`, `TileAt`, `FloorTopCm`, `TileCenter`, `Rows`, `Cols`, `TileCm`) and `FPSMapFormat::LowCoverCm/HighCoverCm`.
- Produces (namespace `FPSNav`, tiles as `FIntPoint(X = row, Y = column)`):
  - `struct FTileGraph`:
    - data: `{ int32 Rows, Cols; TArray<bool> Walkable; TArray<TArray<int32>> Out; }`;
    - `static FTileGraph Build(const FMapSpec&)`;
    - `Index`, `TileOf`, `InBounds`, `IsWalkable`, `HasEdge`;
    - `int32 CountStronglyConnected() const`, `TArray<FIntPoint> FindPath(Start, Goal) const`, `TArray<FIntPoint> WalkableTiles() const`.
  - `bool CanMove(const FMapSpec&, From, To)`.
  - `bool HasTileLos(const FMapSpec&, A, B, float EyeCm = StandingEyeCm, float Step = 0.25f)`.
  - `FIntPoint TileAt(const FMapSpec&, const FVector2D& Local)`, `FVector2D TileCenterLocal(const FMapSpec&, const FIntPoint&)`.
  - `inline constexpr float StandingEyeCm = 158.f`.

- [ ] **Step 1: Write the failing test** `Tests/TileGraphTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Arena/TileGraph.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSNav;

namespace
{
	FMapSpec MakeSpec(const TArray<FString>& Tiles, const TArray<FString>& Heights)
	{
		FMapSpec S;
		S.Name = TEXT("test");
		S.Tiles = Tiles;
		S.Heights = Heights;
		for (const FString& Row : Tiles) { S.Zones.Add(FString::ChrN(Row.Len(), TEXT('.'))); }
		return S;
	}

	/** Row 1: floor (L0), a ramp rising east (L0 -> L1), then L1 floor. Row 2: L0, L0, then L1: a 1 m drop westward between columns 3 and 2. */
	FMapSpec RampSpec(TCHAR RampTile = TEXT('>'))
	{
		FString Row1 = TEXT("#.>..#");
		Row1[2] = RampTile;
		return MakeSpec({ TEXT("######"), Row1, TEXT("#....#"), TEXT("######") },
		                { TEXT("000000"), TEXT("000110"), TEXT("000110"), TEXT("000000") });
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphMovesTest, "FPSAgent.Nav.TileGraph.Moves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphMovesTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = RampSpec();
	TestTrue(TEXT("onto the ramp from its low end"), CanMove(S, FIntPoint(1, 1), FIntPoint(1, 2)));
	TestTrue(TEXT("up the ramp"), CanMove(S, FIntPoint(1, 2), FIntPoint(1, 3)));
	TestTrue(TEXT("down the ramp (ramps work both ways)"), CanMove(S, FIntPoint(1, 3), FIntPoint(1, 2)));
	TestTrue(TEXT("off the ramp's low end"), CanMove(S, FIntPoint(1, 2), FIntPoint(1, 1)));
	TestTrue(TEXT("a drop is allowed"), CanMove(S, FIntPoint(2, 3), FIntPoint(2, 2)));
	TestFalse(TEXT("drops are one-way"), CanMove(S, FIntPoint(2, 2), FIntPoint(2, 3)));
	TestFalse(TEXT("no entering a ramp from its side"), CanMove(S, FIntPoint(2, 2), FIntPoint(1, 2)));
	TestFalse(TEXT("walls are not walkable"), CanMove(S, FIntPoint(1, 1), FIntPoint(0, 1)));

	const FTileGraph G = FTileGraph::Build(S);
	TestEqual(TEXT("8 walkable tiles"), G.WalkableTiles().Num(), 8);
	TestTrue(TEXT("graph edge matches CanMove"), G.HasEdge(FIntPoint(2, 3), FIntPoint(2, 2)) && !G.HasEdge(FIntPoint(2, 2), FIntPoint(2, 3)));
	TestEqual(TEXT("ramp map is strongly connected"), G.CountStronglyConnected(), 1);
	TestEqual(TEXT("without the ramp the upper level is a one-way trap"), FTileGraph::Build(RampSpec(TEXT('.'))).CountStronglyConnected(), 2);

	FMapSpec Map01;
	FString Error;
	TestTrue(TEXT("map01 loads"), FMapSpec::LoadFromContent(TEXT("map01"), Map01, Error));
	TestEqual(TEXT("map01 is one strongly connected component (validator rule 6)"), FTileGraph::Build(Map01).CountStronglyConnected(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphPathTest, "FPSAgent.Nav.TileGraph.Path",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphPathTest::RunTest(const FString& Parameters)
{
	const FMapSpec S = RampSpec();
	const FTileGraph G = FTileGraph::Build(S);
	const TArray<FIntPoint> Up = G.FindPath(FIntPoint(2, 2), FIntPoint(1, 4));
	TestEqual(TEXT("up: around through the ramp (6 tiles)"), Up.Num(), 6);
	const TArray<FIntPoint> Down = G.FindPath(FIntPoint(1, 4), FIntPoint(2, 2));
	TestEqual(TEXT("down: via the drop (4 tiles)"), Down.Num(), 4);
	for (const TArray<FIntPoint>* P : { &Up, &Down })
	{
		for (int32 i = 1; i < P->Num(); ++i) { TestTrue(TEXT("every step is a legal move"), CanMove(S, (*P)[i - 1], (*P)[i])); }
	}
	TestTrue(TEXT("deterministic"), G.FindPath(FIntPoint(1, 4), FIntPoint(2, 2)) == Down);
	TestEqual(TEXT("start == goal"), G.FindPath(FIntPoint(1, 1), FIntPoint(1, 1)).Num(), 1);
	TestEqual(TEXT("unreachable"), FTileGraph::Build(RampSpec(TEXT('.'))).FindPath(FIntPoint(2, 2), FIntPoint(1, 4)).Num(), 0);

	FMapSpec Map01;
	FString Error;
	FMapSpec::LoadFromContent(TEXT("map01"), Map01, Error);
	const FTileGraph M = FTileGraph::Build(Map01);
	const TArray<FIntPoint> Tiles = M.WalkableTiles();
	const TArray<FIntPoint> Long = M.FindPath(Tiles[0], Tiles.Last());
	TestTrue(TEXT("map01: a path across the map"), Long.Num() > 1);
	for (int32 i = 1; i < Long.Num(); ++i) { TestTrue(TEXT("map01 path steps are legal"), CanMove(Map01, Long[i - 1], Long[i])); }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSTileGraphLosTest, "FPSAgent.Nav.TileGraph.Los",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSTileGraphLosTest::RunTest(const FString& Parameters)
{
	FMapSpec S = FPSArena::MakeOpenFloorSpec(6, TEXT("los"));
	TestTrue(TEXT("open floor: visible"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	S.Tiles[3][3] = TEXT('h');
	TestFalse(TEXT("high cover blocks standing eyes"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	S.Tiles[3][3] = TEXT('l');
	TestTrue(TEXT("standing eyes see over low cover"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5)));
	TestFalse(TEXT("low cover hides a crouched eye (100 cm)"), HasTileLos(S, FIntPoint(3, 1), FIntPoint(3, 5), 100.f));

	TestEqual(TEXT("TileAt inverts TileCenterLocal"), TileAt(S, TileCenterLocal(S, FIntPoint(2, 5))), FIntPoint(2, 5));
	TestEqual(TEXT("TileAt clamps to the grid"), TileAt(S, FVector2D(-5000.0, -5000.0)), FIntPoint(S.Rows() - 1, 0));
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Expected: `cannot open include file: 'Arena/TileGraph.h'`.

- [ ] **Step 3: Implement** `Arena/TileGraph.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Arena/MapSpec.h"

/**
 * Bot navigation without a navmesh (spec §4): walkable tiles, one-way edges and A* on an FMapSpec. Pure (no world).
 * A C++ port of tools/mapgen/mapgen/grid.py (same move and line-of-sight rules). Tiles are FIntPoint(X = row, Y = column).
 */
namespace FPSNav
{
	/** Standing bean eye above the floor top: tools/mapgen/mapgen/spec.py EYE_CM. */
	inline constexpr float StandingEyeCm = 158.f;

	struct FPSAGENT_API FTileGraph
	{
		int32 Rows = 0;
		int32 Cols = 0;
		TArray<bool> Walkable;
		/** Out-neighbours of each tile index (Row * Cols + Col), in N, S, E, W order. */
		TArray<TArray<int32>> Out;

		static FTileGraph Build(const FMapSpec& Spec);

		int32 Index(const FIntPoint& Tile) const { return Tile.X * Cols + Tile.Y; }
		FIntPoint TileOf(int32 InIndex) const { return FIntPoint(InIndex / Cols, InIndex % Cols); }
		bool InBounds(const FIntPoint& Tile) const { return Tile.X >= 0 && Tile.X < Rows && Tile.Y >= 0 && Tile.Y < Cols; }
		bool IsWalkable(const FIntPoint& Tile) const { return InBounds(Tile) && Walkable[Index(Tile)]; }
		bool HasEdge(const FIntPoint& From, const FIntPoint& To) const;
		/** Strongly connected components among walkable tiles (iterative Kosaraju, like grid.py). */
		int32 CountStronglyConnected() const;
		/** A*: unit step cost, Manhattan heuristic, ties broken by lower f, then lower h, then lower index (deterministic). Start and Goal included; empty if unreachable. */
		TArray<FIntPoint> FindPath(const FIntPoint& Start, const FIntPoint& Goal) const;
		/** Walkable tiles in row-major order. */
		TArray<FIntPoint> WalkableTiles() const;
	};

	/** grid.py can_move: one 4-neighbour step, same level or a drop; ramps only along their axis (leaving one sideways = a drop). */
	FPSAGENT_API bool CanMove(const FMapSpec& Spec, const FIntPoint& From, const FIntPoint& To);
	/** grid.py has_los: the segment between eye points above two tile centres is blocked where it passes at or below an obstacle top. */
	FPSAGENT_API bool HasTileLos(const FMapSpec& Spec, const FIntPoint& A, const FIntPoint& B, float EyeCm = StandingEyeCm, float Step = 0.25f);
	/** The tile containing a map-local XY position (cm, relative to the map origin), clamped to the grid. */
	FPSAGENT_API FIntPoint TileAt(const FMapSpec& Spec, const FVector2D& Local);
	/** Map-local XY of a tile's centre. */
	FPSAGENT_API FVector2D TileCenterLocal(const FMapSpec& Spec, const FIntPoint& Tile);
}
```

  `Arena/TileGraph.cpp`:

```cpp
#include "Arena/TileGraph.h"
#include "Algo/Reverse.h"

namespace
{
	const FIntPoint Dirs[4] = { FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	/** grid.py _edge_level: the floor level of a tile along its edge facing D; INDEX_NONE = a ramp's side (no walkable edge). */
	int32 EdgeLevel(const FMapSpec& S, const FIntPoint& T, const FIntPoint& D)
	{
		const int32 H = S.LevelAt(T.X, T.Y);
		if (S.IsRamp(T.X, T.Y))
		{
			const FIntPoint R = S.RampDir(T.X, T.Y);
			if (D == R) { return H + 1; }
			if (D == FIntPoint(-R.X, -R.Y)) { return H; }
			return INDEX_NONE;
		}
		return H;
	}

	/** grid.py obstacle_top_cm. */
	float ObstacleTopCm(const FMapSpec& S, int32 R, int32 C)
	{
		const TCHAR Ch = S.TileAt(R, C);
		if (Ch == TEXT('#')) { return TNumericLimits<float>::Max(); }
		if (Ch == TEXT('x')) { return -TNumericLimits<float>::Max(); }
		const float Top = S.FloorTopCm(R, C);
		if (Ch == TEXT('h')) { return Top + FPSMapFormat::HighCoverCm; }
		if (Ch == TEXT('l')) { return Top + FPSMapFormat::LowCoverCm; }
		return Top;
	}

	bool InGrid(const FMapSpec& S, const FIntPoint& T)
	{
		return T.X >= 0 && T.Y >= 0 && T.X < S.Rows() && T.Y < S.Cols();
	}
}

namespace FPSNav
{
	bool CanMove(const FMapSpec& S, const FIntPoint& U, const FIntPoint& V)
	{
		if (!InGrid(S, U) || !InGrid(S, V) || !S.IsWalkable(U.X, U.Y) || !S.IsWalkable(V.X, V.Y))
		{
			return false;
		}
		const FIntPoint D = V - U;
		const int32 Target = EdgeLevel(S, V, FIntPoint(-D.X, -D.Y));
		if (Target == INDEX_NONE)
		{
			return false; // cannot enter a ramp from its side
		}
		int32 Source = EdgeLevel(S, U, D);
		if (Source == INDEX_NONE)
		{
			Source = S.LevelAt(U.X, U.Y); // stepping off a ramp's side: you drop from its low level at best
		}
		return Target <= Source;
	}

	FTileGraph FTileGraph::Build(const FMapSpec& S)
	{
		FTileGraph G;
		G.Rows = S.Rows();
		G.Cols = S.Cols();
		G.Walkable.Init(false, G.Rows * G.Cols);
		G.Out.SetNum(G.Rows * G.Cols);
		for (int32 R = 0; R < G.Rows; ++R)
		{
			for (int32 C = 0; C < G.Cols; ++C)
			{
				if (!S.IsWalkable(R, C))
				{
					continue;
				}
				const FIntPoint U(R, C);
				G.Walkable[G.Index(U)] = true;
				for (const FIntPoint& D : Dirs)
				{
					const FIntPoint V = U + D;
					if (G.InBounds(V) && CanMove(S, U, V))
					{
						G.Out[G.Index(U)].Add(G.Index(V));
					}
				}
			}
		}
		return G;
	}

	bool FTileGraph::HasEdge(const FIntPoint& From, const FIntPoint& To) const
	{
		return InBounds(From) && InBounds(To) && Out[Index(From)].Contains(Index(To));
	}

	TArray<FIntPoint> FTileGraph::WalkableTiles() const
	{
		TArray<FIntPoint> Tiles;
		for (int32 i = 0; i < Walkable.Num(); ++i)
		{
			if (Walkable[i]) { Tiles.Add(TileOf(i)); }
		}
		return Tiles;
	}

	int32 FTileGraph::CountStronglyConnected() const
	{
		const int32 N = Rows * Cols;
		TArray<int32> Order;
		Order.Reserve(N);
		TArray<bool> Seen;
		Seen.Init(false, N);
		for (int32 Start = 0; Start < N; ++Start)
		{
			if (!Walkable[Start] || Seen[Start])
			{
				continue;
			}
			TArray<TPair<int32, int32>> Stack; // node, next out-edge to look at
			Stack.Emplace(Start, 0);
			Seen[Start] = true;
			while (Stack.Num() > 0)
			{
				const int32 Node = Stack.Last().Key;
				const int32 Next = Stack.Last().Value;
				if (Next < Out[Node].Num())
				{
					Stack.Last().Value = Next + 1;
					const int32 V = Out[Node][Next];
					if (!Seen[V])
					{
						Seen[V] = true;
						Stack.Emplace(V, 0);
					}
				}
				else
				{
					Order.Add(Node);
					Stack.Pop();
				}
			}
		}
		TArray<TArray<int32>> In;
		In.SetNum(N);
		for (int32 U = 0; U < N; ++U)
		{
			for (const int32 V : Out[U]) { In[V].Add(U); }
		}
		TArray<bool> Assigned;
		Assigned.Init(false, N);
		int32 Count = 0;
		for (int32 i = Order.Num() - 1; i >= 0; --i)
		{
			const int32 Root = Order[i];
			if (Assigned[Root])
			{
				continue;
			}
			++Count;
			TArray<int32> Stack;
			Stack.Add(Root);
			Assigned[Root] = true;
			while (Stack.Num() > 0)
			{
				const int32 U = Stack.Pop();
				for (const int32 V : In[U])
				{
					if (!Assigned[V])
					{
						Assigned[V] = true;
						Stack.Add(V);
					}
				}
			}
		}
		return Count;
	}

	TArray<FIntPoint> FTileGraph::FindPath(const FIntPoint& Start, const FIntPoint& Goal) const
	{
		TArray<FIntPoint> Path;
		if (!IsWalkable(Start) || !IsWalkable(Goal))
		{
			return Path;
		}
		const int32 N = Rows * Cols;
		const int32 S = Index(Start);
		const int32 G = Index(Goal);
		TArray<int32> Cost;
		Cost.Init(MAX_int32, N);
		TArray<int32> Parent;
		Parent.Init(INDEX_NONE, N);
		TArray<bool> Closed;
		Closed.Init(false, N);
		auto H = [this, &Goal](int32 I)
		{
			const FIntPoint T = TileOf(I);
			return FMath::Abs(T.X - Goal.X) + FMath::Abs(T.Y - Goal.Y);
		};
		// Maps are at most a few hundred tiles: a linear scan over the open set is fast and keeps the tie-break explicit.
		TArray<int32> Open;
		Open.Add(S);
		Cost[S] = 0;
		while (Open.Num() > 0)
		{
			int32 BestK = 0;
			for (int32 k = 1; k < Open.Num(); ++k)
			{
				const int32 A = Open[k];
				const int32 B = Open[BestK];
				const int32 FA = Cost[A] + H(A);
				const int32 FB = Cost[B] + H(B);
				if (FA < FB || (FA == FB && (H(A) < H(B) || (H(A) == H(B) && A < B))))
				{
					BestK = k;
				}
			}
			const int32 U = Open[BestK];
			Open.RemoveAtSwap(BestK);
			if (U == G)
			{
				break;
			}
			if (Closed[U])
			{
				continue;
			}
			Closed[U] = true;
			for (const int32 V : Out[U])
			{
				if (Closed[V])
				{
					continue;
				}
				const int32 C = Cost[U] + 1;
				if (C < Cost[V])
				{
					Cost[V] = C;
					Parent[V] = U;
					Open.AddUnique(V);
				}
			}
		}
		if (Cost[G] == MAX_int32)
		{
			return Path;
		}
		for (int32 I = G; I != INDEX_NONE; I = Parent[I])
		{
			Path.Add(TileOf(I));
		}
		Algo::Reverse(Path);
		return Path;
	}

	bool HasTileLos(const FMapSpec& S, const FIntPoint& A, const FIntPoint& B, float EyeCm, float Step)
	{
		const double Za = S.FloorTopCm(A.X, A.Y) + EyeCm;
		const double Zb = S.FloorTopCm(B.X, B.Y) + EyeCm;
		const FVector2D Pa(A.X + 0.5, A.Y + 0.5);
		const FVector2D Pb(B.X + 0.5, B.Y + 0.5);
		const double Dist = FVector2D::Distance(Pa, Pb);
		const int32 N = FMath::Max(1, static_cast<int32>(Dist / FMath::Max(1e-3f, Step)));
		for (int32 i = 1; i < N; ++i)
		{
			const double T = static_cast<double>(i) / N;
			const int32 R = FMath::FloorToInt32(Pa.X + (Pb.X - Pa.X) * T);
			const int32 C = FMath::FloorToInt32(Pa.Y + (Pb.Y - Pa.Y) * T);
			if ((R == A.X && C == A.Y) || (R == B.X && C == B.Y))
			{
				continue;
			}
			if (Za + (Zb - Za) * T <= ObstacleTopCm(S, R, C))
			{
				return false;
			}
		}
		return true;
	}

	FIntPoint TileAt(const FMapSpec& S, const FVector2D& Local)
	{
		// Inverse of FMapSpec::TileCenter: X = (Rows - 1 - R + 0.5) * Tile, Y = (C + 0.5) * Tile.
		const int32 R = S.Rows() - 1 - FMath::FloorToInt32(Local.X / S.TileCm);
		const int32 C = FMath::FloorToInt32(Local.Y / S.TileCm);
		return FIntPoint(FMath::Clamp(R, 0, S.Rows() - 1), FMath::Clamp(C, 0, S.Cols() - 1));
	}

	FVector2D TileCenterLocal(const FMapSpec& S, const FIntPoint& T)
	{
		const FVector C = S.TileCenter(T.X, T.Y);
		return FVector2D(C.X, C.Y);
	}
}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Nav"`. Expected: `Succeeded: 3  Failed: 0`. Then run the full suite: `Failed: 0`.
  - **If map01's SCC count isn't 1:** compare the move rules with `tools/mapgen/mapgen/grid.py` line by line. The Python validator guarantees 1 for `map01`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Arena/TileGraph.* Game/FPSAgent/Source/FPSAgent/Tests/TileGraphTests.cpp
git commit -m "[local] TileGraph: walkable graph with ramps and one-way drops, SCC count, deterministic A*, 2.5D tile line of sight (port of mapgen grid.py)"
git pull --rebase && git push
```

---

### Task 3: Cover floor generator

**Files:**
- Modify: `Arena/FloorGenerator.h`, `Arena/FloorGenerator.cpp`
- Test: `Tests/TileGraphTests.cpp` (one more test case)

**Interfaces:**
- Consumes: `MakeOpenFloorSpec` and `FPSNav::FTileGraph` (Task 2).
- Produces: `FMapSpec FPSArena::MakeCoverFloorSpec(int32 Tiles, float CoverDensity, float LowFraction, FRandomStream& Rng, const FString& Name)`.

- [ ] **Step 1: Write the failing test.** Append to `Tests/TileGraphTests.cpp`, before `#endif`:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCoverFloorTest, "FPSAgent.Nav.CoverFloor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCoverFloorTest::RunTest(const FString& Parameters)
{
	auto Count = [](const FMapSpec& S, TCHAR Ch)
	{
		int32 N = 0;
		for (const FString& Row : S.Tiles) { for (const TCHAR C : Row) { N += C == Ch ? 1 : 0; } }
		return N;
	};
	for (int32 Seed = 0; Seed < 20; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FMapSpec S = FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, Rng, TEXT("cover"));
		TestEqual(TEXT("12% of 400 tiles are cover"), Count(S, TEXT('l')) + Count(S, TEXT('h')), 48);
		TestEqual(TEXT("half of them low"), Count(S, TEXT('l')), 24);
		TestEqual(TEXT("wall ring kept"), S.Tiles[0], FString::ChrN(22, TEXT('#')));
		TestEqual(TEXT("all walkable tiles connected"), FTileGraph::Build(S).CountStronglyConnected(), 1);
	}
	FRandomStream A(7), B(7);
	TestTrue(TEXT("deterministic per seed"), FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, A, TEXT("x")).Tiles == FPSArena::MakeCoverFloorSpec(20, 0.12f, 0.5f, B, TEXT("x")).Tiles);
	FRandomStream Z(1);
	TestTrue(TEXT("density 0 = the open floor"), FPSArena::MakeCoverFloorSpec(20, 0.f, 0.5f, Z, TEXT("o")).Tiles == FPSArena::MakeOpenFloorSpec(20, TEXT("o")).Tiles);
	FRandomStream Dense(3);
	TestEqual(TEXT("dense layouts stay connected (or fall back to open)"), FTileGraph::Build(FPSArena::MakeCoverFloorSpec(20, 0.45f, 0.5f, Dense, TEXT("d"))).CountStronglyConnected(), 1);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'MakeCoverFloorSpec': is not a member of 'FPSArena'`.

- [ ] **Step 3: Implement.** In `Arena/FloorGenerator.h`, after `MakeOpenFloorSpec`, add:

```cpp
	/**
	 * Pure: MakeOpenFloorSpec plus random cover tiles (S1/S2 floors, spec §6).
	 * - Count: round(CoverDensity x Tiles^2) cover tiles, at most half the interior.
	 * - Height: LowFraction of them are low ('l', 120 cm), the rest high ('h', 250 cm).
	 * - Connectivity: every walkable tile stays in one strongly connected component (FPSNav::FTileGraph).
	 * - Fallback: it tries up to 8 layouts, then returns the open floor.
	 * - Deterministic for a given Rng state.
	 */
	FPSAGENT_API FMapSpec MakeCoverFloorSpec(int32 Tiles, float CoverDensity, float LowFraction, FRandomStream& Rng, const FString& Name);
```

  In `Arena/FloorGenerator.cpp`, add `#include "Arena/TileGraph.h"`. Then, inside `namespace FPSArena` after `MakeOpenFloorSpec`, add:

```cpp
	FMapSpec MakeCoverFloorSpec(int32 Tiles, float CoverDensity, float LowFraction, FRandomStream& Rng, const FString& Name)
	{
		const FMapSpec Open = MakeOpenFloorSpec(Tiles, Name);
		const int32 N = FMath::Max(1, Tiles);
		const int32 Interior = N * N;
		const int32 Count = FMath::Clamp(FMath::RoundToInt(FMath::Max(0.f, CoverDensity) * Interior), 0, Interior / 2);
		if (Count == 0)
		{
			return Open;
		}
		const int32 LowCount = FMath::RoundToInt(FMath::Clamp(LowFraction, 0.f, 1.f) * Count);
		constexpr int32 MaxLayouts = 8;
		for (int32 Attempt = 0; Attempt < MaxLayouts; ++Attempt)
		{
			TArray<FIntPoint> Cells;
			Cells.Reserve(Interior);
			for (int32 R = 1; R <= N; ++R)
			{
				for (int32 C = 1; C <= N; ++C) { Cells.Add(FIntPoint(R, C)); }
			}
			for (int32 i = Cells.Num() - 1; i > 0; --i) // seeded Fisher-Yates
			{
				Cells.Swap(i, Rng.RandRange(0, i));
			}
			FMapSpec S = Open;
			for (int32 k = 0; k < Count; ++k)
			{
				S.Tiles[Cells[k].X][Cells[k].Y] = k < LowCount ? TEXT('l') : TEXT('h');
			}
			if (FPSNav::FTileGraph::Build(S).CountStronglyConnected() == 1)
			{
				return S;
			}
		}
		return Open;
	}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Nav"`. Expected: `Succeeded: 4  Failed: 0`. Then run the full suite.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Arena/FloorGenerator.* Game/FPSAgent/Source/FPSAgent/Tests/TileGraphTests.cpp
git commit -m "[local] Cover floor generator: seeded low/high cover at a density, kept strongly connected (regenerate, else open floor)"
git pull --rebase && git push
```

---

### Task 4: Bot senses, sound events and the agent's hearing vector

**Files:**
- Create: `Bots/BotSenses.h`, `Bots/BotSenses.cpp`
- Modify: `Learning/CombatObservation.h`, `Learning/CombatObservation.cpp` (`PackSound`)
- Test: `Tests/BotSensesTests.cpp`, `Tests/CombatObservationTests.cpp` (more assertions)

**Interfaces:**
- Consumes: `FBotBehaviourSettings` (Task 1).
- Produces (namespace `FPSBots`):
  - `enum class ESoundKind : uint8 { Footstep, Gunshot }`.
  - `struct FSoundEvent { FVector Position; double Time; ESoundKind Kind; int32 Source; }` (Source = `INDEX_NONE` for the agent, else the bot index).
  - Free functions: `bool IsAudibleMovement(float Speed, bool bCrouched, bool bWalking, float WalkSpeed, const FBotBehaviourSettings&)`, `float HearingRadiusCm(ESoundKind, const FBotBehaviourSettings&)`, `bool CanHear(const FSoundEvent&, const FVector& Listener, double Now, float MaxAgeSeconds, const FBotBehaviourSettings&)`.
  - `struct FBotSenses`:
    - `void Reset()`;
    - `bool Update(bool bSeesAgent, const FVector& AgentPos, const TArray<FSoundEvent>& Sounds, const FVector& BotPos, double Now, const FBotBehaviourSettings&, FRandomStream&, FVector& OutTarget)`;
    - `bool Remembers(double Now, const FBotBehaviourSettings&) const`.
- Produces: `TArray<float> FPSCombatObs::PackSound(const FVector& DirectionView, float DistanceCm, float AgeSeconds, const FCombatPerceptionSettings& P, float SoundMemorySeconds)`.

- [ ] **Step 1: Write the failing tests.** Create `Tests/BotSensesTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Bots/BotSenses.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSBots;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotSensesSightTest, "FPSAgent.Bots.Senses.Sight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotSensesSightTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B; // reaction 0.4 s, memory 3 s
	FRandomStream Rng(1);
	const FVector Agent(1000.0, 0.0, 0.0);
	const FVector Bot(0.0, 0.0, 0.0);
	const TArray<FSoundEvent> None;
	FVector T;
	FBotSenses S;
	TestFalse(TEXT("a new sighting waits for the reaction delay"), S.Update(true, Agent, None, Bot, 0.0, B, Rng, T));
	TestFalse(TEXT("still waiting at 0.3 s"), S.Update(true, Agent, None, Bot, 0.3, B, Rng, T));
	TestTrue(TEXT("acts at 0.4 s"), S.Update(true, Agent, None, Bot, 0.4, B, Rng, T));
	TestTrue(TEXT("target = the agent's position"), T.Equals(Agent, 1e-3));
	TestTrue(TEXT("remembers for 3 s"), S.Remembers(3.39, B));
	TestFalse(TEXT("forgets after 3 s"), S.Remembers(3.41, B));

	FBotSenses L;
	L.Update(true, Agent, None, Bot, 0.0, B, Rng, T);
	L.Update(false, Agent, None, Bot, 0.2, B, Rng, T);
	TestFalse(TEXT("losing sight restarts the delay"), L.Update(true, Agent, None, Bot, 0.5, B, Rng, T));
	TestTrue(TEXT("acts 0.4 s after the new sighting"), L.Update(true, Agent, None, Bot, 0.9, B, Rng, T));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotSensesHearingTest, "FPSAgent.Bots.Senses.Hearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotSensesHearingTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B; // gunshots 40 m, running footsteps 15 m, noise 2 m
	const FVector Bot(0.0, 0.0, 0.0);
	auto Event = [](double X, double Time, ESoundKind Kind, int32 Source) { FSoundEvent E; E.Position = FVector(X, 0.0, 0.0); E.Time = Time; E.Kind = Kind; E.Source = Source; return E; };
	TestTrue(TEXT("gunshot at 39.9 m"), CanHear(Event(3990.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("gunshot at 40.1 m"), CanHear(Event(4010.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestTrue(TEXT("footstep at 14.9 m"), CanHear(Event(1490.0, 0.0, ESoundKind::Footstep, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("footstep at 15.1 m"), CanHear(Event(1510.0, 0.0, ESoundKind::Footstep, INDEX_NONE), Bot, 0.1, 1.f, B));
	TestFalse(TEXT("too old"), CanHear(Event(100.0, 0.0, ESoundKind::Gunshot, INDEX_NONE), Bot, 1.5, 1.f, B));

	TestTrue(TEXT("running is audible"), IsAudibleMovement(540.f, false, false, 300.f, B));
	TestTrue(TEXT("a 60% bot (324 cm/s) is audible"), IsAudibleMovement(324.f, false, false, 300.f, B));
	TestFalse(TEXT("walking is silent"), IsAudibleMovement(300.f, false, true, 300.f, B));
	TestFalse(TEXT("crouching is silent"), IsAudibleMovement(190.f, true, false, 300.f, B));
	TestFalse(TEXT("standing still is silent"), IsAudibleMovement(0.f, false, false, 300.f, B));

	FRandomStream Rng(5);
	FVector T;
	FBotSenses H;
	const TArray<FSoundEvent> Shot = { Event(3000.0, 1.0, ESoundKind::Gunshot, INDEX_NONE) };
	TestFalse(TEXT("not before the reaction delay"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.2, B, Rng, T));
	TestTrue(TEXT("investigates the gunshot"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.4, B, Rng, T));
	TestTrue(TEXT("noisy position within 2 m"), FVector::Dist(T, FVector(3000.0, 0.0, 0.0)) <= 200.0 + 1e-3);
	TestFalse(TEXT("each sound only once"), H.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, Rng, T));
	FBotSenses O;
	const TArray<FSoundEvent> BotStep = { Event(100.0, 1.0, ESoundKind::Footstep, 2) };
	TestFalse(TEXT("bots ignore other bots' sounds"), O.Update(false, FVector::ZeroVector, BotStep, Bot, 2.0, B, Rng, T));

	FRandomStream R1(9), R2(9);
	FVector T1, T2;
	FBotSenses A1, A2;
	A1.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, R1, T1);
	A2.Update(false, FVector::ZeroVector, Shot, Bot, 1.5, B, R2, T2);
	TestTrue(TEXT("deterministic noise"), T1.Equals(T2, 1e-6));
	return true;
}

#endif
```

  In `Tests/CombatObservationTests.cpp` → `FFPSCombatObsPackTest`, before `return true;`, add:

```cpp
	const TArray<float> Sound = PackSound(FVector(0.0, 2.0, 0.0), 1500.f, 0.5f, P, 1.f);
	TestEqual(TEXT("sound size"), Sound.Num(), SoundSize);
	TestEqual(TEXT("sound direction (unit, view frame)"), Sound[1], 1.f, 1e-5f);
	TestEqual(TEXT("sound distance / position scale"), Sound[3], 0.5f, 1e-5f);
	TestEqual(TEXT("sound age / memory"), Sound[4], 0.5f, 1e-5f);
```

- [ ] **Step 2: Build to verify it fails.** Expected: `cannot open include file: 'Bots/BotSenses.h'`.

- [ ] **Step 3: Implement** `Bots/BotSenses.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Common/FPSAgentTypes.h"

/** Fair bot senses and hearing (spec §4, D-024): the same rules as the agent's. Pure: the caller decides view-cone + line-of-sight visibility. */
namespace FPSBots
{
	enum class ESoundKind : uint8 { Footstep, Gunshot };

	/** An audible event in one arena. Each arena keeps its own buffer, so arenas never hear each other (hard rule 3). */
	struct FSoundEvent
	{
		FVector Position = FVector::ZeroVector;
		double Time = 0.0;
		ESoundKind Kind = ESoundKind::Footstep;
		/** INDEX_NONE = the agent, else the bot index. */
		int32 Source = INDEX_NONE;
	};

	/** Running makes footstep sounds; walking, crouching and standing still are silent (spec §4). */
	FPSAGENT_API bool IsAudibleMovement(float HorizontalSpeed, bool bCrouched, bool bWalking, float WalkSpeed, const FBotBehaviourSettings& B);
	FPSAGENT_API float HearingRadiusCm(ESoundKind Kind, const FBotBehaviourSettings& B);
	/** Within the hearing radius of its kind, and between 0 and MaxAgeSeconds old. */
	FPSAGENT_API bool CanHear(const FSoundEvent& E, const FVector& Listener, double Now, float MaxAgeSeconds, const FBotBehaviourSettings& B);

	/** What one bot knows about the agent: sightings wait out the reaction delay; heard sounds give a noisy position. */
	struct FPSAGENT_API FBotSenses
	{
		/** When the current continuous sighting started (< 0: not seeing). */
		double SightSince = -1.0;
		FVector LastKnown = FVector::ZeroVector;
		double LastKnownTime = -1.0e9;
		/** Time of the newest sound already reacted to. */
		double LastSoundTime = -1.0e9;

		void Reset() { *this = FBotSenses(); }
		/**
		 * Returns true (OutTarget = where to look or go) when a stimulus becomes actionable:
		 * - seeing the agent continuously for at least ReactionSeconds (every update while it lasts);
		 * - otherwise the newest audible agent sound (Source == INDEX_NONE) that is at least ReactionSeconds old and new to this bot.
		 * bSeesAgent must already include the view cone and the geometry line-of-sight check.
		 */
		bool Update(bool bSeesAgent, const FVector& AgentPos, const TArray<FSoundEvent>& Sounds, const FVector& BotPos, double Now,
			const FBotBehaviourSettings& B, FRandomStream& Rng, FVector& OutTarget);
		bool Remembers(double Now, const FBotBehaviourSettings& B) const { return Now - LastKnownTime <= B.SightMemorySeconds; }
	};
}
```

  `Bots/BotSenses.cpp`:

```cpp
#include "Bots/BotSenses.h"

namespace FPSBots
{
	bool IsAudibleMovement(float Speed, bool bCrouched, bool bWalking, float WalkSpeed, const FBotBehaviourSettings& B)
	{
		return !bCrouched && !bWalking && Speed > WalkSpeed * B.SilentSpeedFraction;
	}

	float HearingRadiusCm(ESoundKind Kind, const FBotBehaviourSettings& B)
	{
		return Kind == ESoundKind::Gunshot ? B.GunshotHearingCm : B.FootstepHearingCm;
	}

	bool CanHear(const FSoundEvent& E, const FVector& Listener, double Now, float MaxAgeSeconds, const FBotBehaviourSettings& B)
	{
		const double Age = Now - E.Time;
		return Age >= 0.0 && Age <= MaxAgeSeconds && FVector::Dist(E.Position, Listener) <= HearingRadiusCm(E.Kind, B);
	}

	bool FBotSenses::Update(bool bSeesAgent, const FVector& AgentPos, const TArray<FSoundEvent>& Sounds, const FVector& BotPos, double Now,
		const FBotBehaviourSettings& B, FRandomStream& Rng, FVector& OutTarget)
	{
		if (bSeesAgent)
		{
			if (SightSince < 0.0)
			{
				SightSince = Now;
			}
			if (Now - SightSince + 1e-9 >= B.ReactionSeconds)
			{
				LastKnown = AgentPos;
				LastKnownTime = Now;
				OutTarget = AgentPos;
				return true;
			}
		}
		else
		{
			SightSince = -1.0;
		}
		const FSoundEvent* Best = nullptr;
		for (const FSoundEvent& E : Sounds)
		{
			if (E.Source != INDEX_NONE || E.Time <= LastSoundTime || Now - E.Time + 1e-9 < B.ReactionSeconds)
			{
				continue;
			}
			if (!CanHear(E, BotPos, Now, B.ReactionSeconds + B.SoundMemorySeconds, B))
			{
				continue;
			}
			if (!Best || E.Time > Best->Time)
			{
				Best = &E;
			}
		}
		if (!Best)
		{
			return false;
		}
		LastSoundTime = Best->Time;
		const float Angle = Rng.FRandRange(0.f, 2.f * PI);
		const float Radius = B.HearingNoiseCm * FMath::Sqrt(Rng.FRand());
		OutTarget = Best->Position + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.0);
		LastKnown = OutTarget;
		LastKnownTime = Now;
		return true;
	}
}
```

  In `Learning/CombatObservation.h`, after `PackHit`, add:

```cpp
	/** [direction x,y,z (view frame, unit), min(distance / PositionScaleCm, 1), min(age / SoundMemorySeconds, 1)] (spec §2 hearing, D-024). */
	FPSAGENT_API TArray<float> PackSound(const FVector& DirectionView, float DistanceCm, float AgeSeconds, const FCombatPerceptionSettings& P, float SoundMemorySeconds);
```

  In `Learning/CombatObservation.cpp`, after `PackHit`, add:

```cpp
	TArray<float> PackSound(const FVector& DirectionView, float DistanceCm, float AgeSeconds, const FCombatPerceptionSettings& P, float SoundMemorySeconds)
	{
		const FVector D = DirectionView.GetSafeNormal();
		return { static_cast<float>(D.X), static_cast<float>(D.Y), static_cast<float>(D.Z),
			FMath::Clamp(Safe(DistanceCm, FMath::Max(1.f, P.PositionScaleCm)), 0.f, 1.f),
			FMath::Clamp(Safe(AgeSeconds, SoundMemorySeconds), 0.f, 1.f) };
	}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Bots.Senses"` (expect 2 passed) and `-Filter "FPSAgent.Combat.Obs"` (expect 3 passed). Then run the full suite.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Bots/BotSenses.* Game/FPSAgent/Source/FPSAgent/Learning/CombatObservation.* Game/FPSAgent/Source/FPSAgent/Tests/BotSensesTests.cpp Game/FPSAgent/Source/FPSAgent/Tests/CombatObservationTests.cpp
git commit -m "[local] Bot senses: reaction delay, 3 s memory, hearing (gunshots 40 m, running footsteps 15 m, walking silent) with seeded noise; PackSound"
git pull --rebase && git push
```

---

### Task 5: Bot brain (behaviours, path following, investigate)

**Files:**
- Create: `Bots/BotBrain.h`, `Bots/BotBrain.cpp`
- Test: `Tests/BotBrainTests.cpp`

**Interfaces:**
- Consumes: `FPSNav::FTileGraph`, `FPSNav::TileAt`, `FPSNav::TileCenterLocal` (Task 2); `MakeOpenFloorSpec` (existing); `FBotBehaviourSettings` (Task 1).
- Produces (namespace `FPSBots`):
  - `enum class EBotBehaviour : uint8 { Static, Strafe, Peek, Crouch, Patrol, Explore }`, `const TCHAR* BehaviourName(EBotBehaviour)`.
  - `struct FBotIntent { FVector2D MoveDir; bool bCrouch; float LookYawDeg; }`.
  - `struct FBotSetup { EBotBehaviour Behaviour; bool bCrouched; float FacingYawDeg; FIntPoint PeekHidden, PeekExposed; }`.
  - `class FBotBrain`:
    - `void Start(const FBotSetup&, const FMapSpec&, const FPSNav::FTileGraph&, int32 Seed, const FVector2D& Pos, double Now, const FBotBehaviourSettings&)`;
    - `void Investigate(const FVector2D& Target, const FVector2D& Pos, double Now, const FBotBehaviourSettings&)`;
    - `FBotIntent Update(const FVector2D& Pos, double Now, const FBotBehaviourSettings&)`;
    - `GetBehaviour()`, `IsInvestigating()`, `IsMover()`, `GetWaypoints()`.
  - Positions are map-local XY in cm (relative to the map origin). Maps are never rotated, so a direction is the same in local and world XY.

- [ ] **Step 1: Write the failing test** `Tests/BotBrainTests.cpp`:

```cpp
#include "Misc/AutomationTest.h"
#include "Arena/FloorGenerator.h"
#include "Bots/BotBrain.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace FPSBots;

namespace
{
	/** Moves a point along the brain's intent at a fixed speed (no collision: a path must never need it). */
	struct FSim
	{
		FVector2D Pos;
		double Now = 0.0;
		TArray<FIntPoint> Tiles;
		TArray<FVector2D> Positions;
		TArray<FBotIntent> Intents;
		void Run(FBotBrain& Brain, const FMapSpec& Map, float Seconds, float Speed, const FBotBehaviourSettings& B)
		{
			const float Dt = 1.f / 60.f;
			for (int32 i = 0; i < FMath::RoundToInt(Seconds / Dt); ++i)
			{
				Now += Dt;
				const FBotIntent I = Brain.Update(Pos, Now, B);
				Pos += I.MoveDir * Speed * Dt;
				Positions.Add(Pos);
				Tiles.Add(FPSNav::TileAt(Map, Pos));
				Intents.Add(I);
			}
		}
	};

	FBotSetup Setup(EBotBehaviour Behaviour, float Yaw = 0.f)
	{
		FBotSetup S;
		S.Behaviour = Behaviour;
		S.FacingYawDeg = Yaw;
		return S;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainStationaryTest, "FPSAgent.Bots.Brain.Stationary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainStationaryTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	const FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("brain"));
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(6, 6));

	FBotBrain Static;
	FBotSetup S = Setup(EBotBehaviour::Static);
	S.bCrouched = true;
	Static.Start(S, Map, G, 1, Start, 0.0, B);
	FSim A{ Start };
	A.Run(Static, Map, 1.f, 300.f, B);
	TestTrue(TEXT("static: never moves"), A.Pos.Equals(Start, 1e-3));
	TestTrue(TEXT("static: keeps its crouch"), A.Intents.Last().bCrouch);

	FBotBrain Crouch;
	Crouch.Start(Setup(EBotBehaviour::Crouch), Map, G, 2, Start, 0.0, B);
	FSim C{ Start };
	C.Run(Crouch, Map, 3.f, 300.f, B);
	int32 Toggles = 0;
	for (int32 i = 1; i < C.Intents.Num(); ++i) { Toggles += C.Intents[i].bCrouch != C.Intents[i - 1].bCrouch ? 1 : 0; }
	TestTrue(TEXT("crouch: toggles at least twice in 3 s"), Toggles >= 2);
	TestTrue(TEXT("crouch: never moves"), C.Pos.Equals(Start, 1e-3));

	FBotBrain Strafe;
	Strafe.Start(Setup(EBotBehaviour::Strafe, 0.f), Map, G, 3, Start, 0.0, B);
	FSim R{ Start };
	R.Run(Strafe, Map, 4.f, 300.f, B);
	bool bLeft = false, bRight = false, bAlongFacing = false;
	double MaxLateral = 0.0;
	for (const FBotIntent& I : R.Intents)
	{
		bRight |= I.MoveDir.Y > 0.5;
		bLeft |= I.MoveDir.Y < -0.5;
		bAlongFacing |= FMath::Abs(I.MoveDir.X) > 1e-3;
	}
	for (const FVector2D& P : R.Positions) { MaxLateral = FMath::Max(MaxLateral, FMath::Abs(P.Y - Start.Y)); }
	TestTrue(TEXT("strafe: both ways"), bLeft && bRight);
	TestFalse(TEXT("strafe: only sideways to its facing"), bAlongFacing);
	TestTrue(TEXT("strafe: stays near its spot (half-width + one tick)"), MaxLateral <= B.StrafeHalfWidthCm + 20.0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainPeekTest, "FPSAgent.Bots.Brain.Peek",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainPeekTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("peek"));
	Map.Tiles[5][5] = TEXT('h');
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	FBotSetup S = Setup(EBotBehaviour::Peek);
	S.PeekHidden = FIntPoint(4, 5);   // north of the cover
	S.PeekExposed = FIntPoint(4, 6);  // one tile east of it
	const FVector2D Hidden = FPSNav::TileCenterLocal(Map, S.PeekHidden);
	const FVector2D Exposed = FPSNav::TileCenterLocal(Map, S.PeekExposed);
	FBotBrain Peek;
	Peek.Start(S, Map, G, 4, Hidden, 0.0, B);
	FSim P{ Hidden };
	double OutAt = -1.0, BackAt = -1.0;
	for (int32 Step = 0; Step < 60 * 10 && BackAt < 0.0; ++Step)
	{
		P.Run(Peek, Map, 1.f / 60.f, 300.f, B);
		if (OutAt < 0.0 && FVector2D::Distance(P.Pos, Exposed) <= B.WaypointToleranceCm) { OutAt = P.Now; }
		if (OutAt >= 0.0 && FVector2D::Distance(P.Pos, Hidden) <= B.WaypointToleranceCm) { BackAt = P.Now; }
	}
	TestTrue(TEXT("peek: steps out"), OutAt >= 0.0);
	TestTrue(TEXT("peek: and back behind cover"), BackAt > OutAt);
	for (const FIntPoint& T : P.Tiles) { TestTrue(TEXT("peek: never inside cover"), G.IsWalkable(T)); }

	FBotBrain NoCover;
	FBotSetup N = Setup(EBotBehaviour::Peek);
	NoCover.Start(N, Map, G, 5, Hidden, 0.0, B);
	TestEqual(TEXT("peek without a cover pair strafes (D-031)"), static_cast<int32>(NoCover.GetBehaviour()), static_cast<int32>(EBotBehaviour::Strafe));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainMoversTest, "FPSAgent.Bots.Brain.Movers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainMoversTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	const FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("movers"));
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(6, 6));

	FBotBrain Patrol;
	Patrol.Start(Setup(EBotBehaviour::Patrol), Map, G, 6, Start, 0.0, B);
	const TArray<FIntPoint> Way = Patrol.GetWaypoints();
	TestEqual(TEXT("patrol: waypoints"), Way.Num(), B.PatrolWaypoints);
	FSim P{ Start };
	P.Run(Patrol, Map, 90.f, 400.f, B);
	int32 Next = 0;
	for (const FIntPoint& T : P.Tiles) { if (Next < Way.Num() && T == Way[Next]) { ++Next; } }
	TestEqual(TEXT("patrol: visits its waypoints in order"), Next, Way.Num());

	FBotBrain Explore;
	Explore.Start(Setup(EBotBehaviour::Explore), Map, G, 7, Start, 0.0, B);
	FSim E{ Start };
	E.Run(Explore, Map, 60.f, 400.f, B);
	TSet<FIntPoint> Distinct(E.Tiles);
	TestTrue(TEXT("explore: covers ground (>= 15 tiles in 60 s)"), Distinct.Num() >= 15);
	FBotBrain Explore2;
	Explore2.Start(Setup(EBotBehaviour::Explore), Map, G, 7, Start, 0.0, B);
	FSim E2{ Start };
	E2.Run(Explore2, Map, 60.f, 400.f, B);
	TestTrue(TEXT("explore: deterministic per seed"), E.Pos.Equals(E2.Pos, 1e-6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSBotBrainInvestigateTest, "FPSAgent.Bots.Brain.Investigate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSBotBrainInvestigateTest::RunTest(const FString& Parameters)
{
	const FBotBehaviourSettings B;
	// A high-cover wall across row 6 with one gap at column 10: the path must go around through the gap.
	FMapSpec Map = FPSArena::MakeOpenFloorSpec(10, TEXT("wall"));
	for (int32 C = 1; C <= 9; ++C) { Map.Tiles[6][C] = TEXT('h'); }
	const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(Map);
	const FVector2D Start = FPSNav::TileCenterLocal(Map, FIntPoint(8, 2));
	const FVector2D Target = FPSNav::TileCenterLocal(Map, FIntPoint(3, 2));

	FBotBrain Patrol;
	Patrol.Start(Setup(EBotBehaviour::Patrol), Map, G, 8, Start, 0.0, B);
	Patrol.Investigate(Target, Start, 0.0, B);
	TestTrue(TEXT("movers investigate"), Patrol.IsInvestigating());
	FSim P{ Start };
	double ArrivedAt = -1.0;
	for (int32 Step = 0; Step < 60 * 30 && ArrivedAt < 0.0; ++Step)
	{
		P.Run(Patrol, Map, 1.f / 60.f, 400.f, B);
		if (FVector2D::Distance(P.Pos, Target) <= 100.0) { ArrivedAt = P.Now; }
	}
	TestTrue(TEXT("reaches the target around the wall"), ArrivedAt >= 0.0);
	for (const FIntPoint& T : P.Tiles) { TestTrue(TEXT("never walks through cover"), G.IsWalkable(T)); }
	P.Run(Patrol, Map, B.InvestigateLookSeconds + 1.f, 400.f, B);
	TestFalse(TEXT("looks around, then resumes its patrol"), Patrol.IsInvestigating());

	FBotBrain Strafe;
	Strafe.Start(Setup(EBotBehaviour::Strafe, 0.f), Map, G, 9, Start, 0.0, B);
	Strafe.Investigate(Start + FVector2D(0.0, 1000.0), Start, 0.0, B);
	TestFalse(TEXT("non-movers hold their spot (D-031)"), Strafe.IsInvestigating());
	FSim S{ Start };
	S.Run(Strafe, Map, 1.f / 60.f, 300.f, B);
	TestEqual(TEXT("...but turn to face the stimulus (+Y = yaw 90)"), S.Intents.Last().LookYawDeg, 90.f, 0.01f);
	return true;
}

#endif
```

- [ ] **Step 2: Build to verify it fails.** Expected: `cannot open include file: 'Bots/BotBrain.h'`.

- [ ] **Step 3: Implement** `Bots/BotBrain.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Arena/TileGraph.h"
#include "Common/FPSAgentTypes.h"

/** Scripted-bot behaviours (spec §4). Pure: positions are map-local XY in cm; maps are never rotated, so directions match world XY. */
namespace FPSBots
{
	enum class EBotBehaviour : uint8 { Static, Strafe, Peek, Crouch, Patrol, Explore };
	FPSAGENT_API const TCHAR* BehaviourName(EBotBehaviour Behaviour);

	/** What a bot wants this tick. */
	struct FBotIntent
	{
		/** Unit or zero XY direction to move in. */
		FVector2D MoveDir = FVector2D::ZeroVector;
		bool bCrouch = false;
		/** Where its body should face (degrees). */
		float LookYawDeg = 0.f;
	};

	struct FBotSetup
	{
		EBotBehaviour Behaviour = EBotBehaviour::Static;
		bool bCrouched = false;
		float FacingYawDeg = 0.f;
		/** Peek: the tile behind cover and the adjacent tile in the open. (-1, -1) = none: the bot strafes (D-031). */
		FIntPoint PeekHidden = FIntPoint(-1, -1);
		FIntPoint PeekExposed = FIntPoint(-1, -1);
	};

	class FPSAGENT_API FBotBrain
	{
	public:
		/** InMap and InGraph must stay alive while the brain is used (the arena owns them for the episode). */
		void Start(const FBotSetup& InSetup, const FMapSpec& InMap, const FPSNav::FTileGraph& InGraph, int32 Seed, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		/** Patrol/Explore bots walk to Target and look around, then resume. The others turn to face it and hold their spot (D-031). */
		void Investigate(const FVector2D& Target, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		FBotIntent Update(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);

		EBotBehaviour GetBehaviour() const { return Setup.Behaviour; }
		bool IsMover() const { return Setup.Behaviour == EBotBehaviour::Patrol || Setup.Behaviour == EBotBehaviour::Explore; }
		bool IsInvestigating() const { return Mode != EMode::Base; }
		const TArray<FIntPoint>& GetWaypoints() const { return Waypoints; }

	private:
		enum class EMode : uint8 { Base, Investigate, LookAround };

		bool PlanTo(const FIntPoint& Goal, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B);
		FVector2D FollowPath(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B, bool& bArrived);
		FIntPoint PickExploreTarget(const FVector2D& Pos, const FBotBehaviourSettings& B);
		void PickPatrolWaypoints(const FVector2D& Pos, const FBotBehaviourSettings& B);

		FBotSetup Setup;
		const FMapSpec* Map = nullptr;
		const FPSNav::FTileGraph* Graph = nullptr;
		FRandomStream Rng;
		EMode Mode = EMode::Base;
		FVector2D Anchor = FVector2D::ZeroVector;
		float FacingYaw = 0.f;
		double LastUpdate = 0.0;

		TArray<FVector2D> PathPoints;
		int32 PathIndex = 0;
		double BestDist = TNumericLimits<double>::Max();
		double ProgressTime = 0.0;

		double NextToggle = 0.0;
		float StrafeSign = 1.f;
		bool bCrouchNow = false;
		bool bPeekOut = false;
		double PauseUntil = -1.0;

		TArray<FIntPoint> Waypoints;
		int32 WaypointIndex = 0;
		TArray<double> VisitTime;

		double LookUntil = 0.0;
		float LookYaw = 0.f;
	};
}
```

  `Bots/BotBrain.cpp`:

```cpp
#include "Bots/BotBrain.h"

namespace
{
	float YawOf(const FVector2D& D)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(D.Y, D.X));
	}

	FVector2D RightOf(float YawDeg)
	{
		const float R = FMath::DegreesToRadians(YawDeg + 90.f);
		return FVector2D(FMath::Cos(R), FMath::Sin(R));
	}
}

namespace FPSBots
{
	const TCHAR* BehaviourName(EBotBehaviour Behaviour)
	{
		switch (Behaviour)
		{
		case EBotBehaviour::Static:  return TEXT("Static");
		case EBotBehaviour::Strafe:  return TEXT("Strafe");
		case EBotBehaviour::Peek:    return TEXT("Peek");
		case EBotBehaviour::Crouch:  return TEXT("Crouch");
		case EBotBehaviour::Patrol:  return TEXT("Patrol");
		case EBotBehaviour::Explore: return TEXT("Explore");
		}
		return TEXT("Unknown");
	}

	void FBotBrain::Start(const FBotSetup& InSetup, const FMapSpec& InMap, const FPSNav::FTileGraph& InGraph, int32 Seed, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		*this = FBotBrain();
		Setup = InSetup;
		Map = &InMap;
		Graph = &InGraph;
		Rng.Initialize(Seed);
		Anchor = Pos;
		FacingYaw = Setup.FacingYawDeg;
		LookYaw = FacingYaw;
		bCrouchNow = Setup.bCrouched;
		LastUpdate = Now;
		VisitTime.Init(-1.0e9, Graph->Rows * Graph->Cols);
		if (Setup.Behaviour == EBotBehaviour::Peek && !(Graph->IsWalkable(Setup.PeekHidden) && Graph->IsWalkable(Setup.PeekExposed)))
		{
			Setup.Behaviour = EBotBehaviour::Strafe; // no cover pair (D-031)
		}
		StrafeSign = Rng.FRand() < 0.5f ? -1.f : 1.f;
		switch (Setup.Behaviour)
		{
		case EBotBehaviour::Strafe: NextToggle = Now + Rng.FRandRange(B.StrafePeriodMinSeconds, B.StrafePeriodMaxSeconds); break;
		case EBotBehaviour::Crouch: NextToggle = Now + Rng.FRandRange(B.CrouchPeriodMinSeconds, B.CrouchPeriodMaxSeconds); break;
		case EBotBehaviour::Peek:   NextToggle = Now + Rng.FRandRange(B.PeekPeriodMinSeconds, B.PeekPeriodMaxSeconds); break;
		case EBotBehaviour::Patrol: PickPatrolWaypoints(Pos, B); break;
		default: break;
		}
	}

	void FBotBrain::PickPatrolWaypoints(const FVector2D& Pos, const FBotBehaviourSettings& B)
	{
		const FIntPoint Here = FPSNav::TileAt(*Map, Pos);
		const TArray<FIntPoint> Tiles = Graph->WalkableTiles();
		for (int32 Try = 0; Try < B.PatrolWaypoints * 8 && Waypoints.Num() < B.PatrolWaypoints && Tiles.Num() > 1; ++Try)
		{
			const FIntPoint T = Tiles[Rng.RandRange(0, Tiles.Num() - 1)];
			if (T != Here && !Waypoints.Contains(T) && Graph->FindPath(Here, T).Num() > 0)
			{
				Waypoints.Add(T);
			}
		}
	}

	bool FBotBrain::PlanTo(const FIntPoint& Goal, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		PathPoints.Reset();
		PathIndex = 0;
		BestDist = TNumericLimits<double>::Max();
		ProgressTime = Now;
		const TArray<FIntPoint> Tiles = Graph->FindPath(FPSNav::TileAt(*Map, Pos), Goal);
		if (Tiles.Num() == 0)
		{
			return false;
		}
		for (int32 i = 1; i < Tiles.Num(); ++i)
		{
			FVector2D P = FPSNav::TileCenterLocal(*Map, Tiles[i]);
			if (i + 1 < Tiles.Num()) // seeded offsets vary the route; the final point is the exact tile centre
			{
				P += FVector2D(Rng.FRandRange(-1.f, 1.f), Rng.FRandRange(-1.f, 1.f)) * B.PathOffsetCm;
			}
			PathPoints.Add(P);
		}
		if (PathPoints.Num() == 0)
		{
			PathPoints.Add(FPSNav::TileCenterLocal(*Map, Goal));
		}
		return true;
	}

	FVector2D FBotBrain::FollowPath(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B, bool& bArrived)
	{
		bArrived = false;
		auto Advance = [&]()
		{
			++PathIndex;
			BestDist = TNumericLimits<double>::Max();
			ProgressTime = Now;
		};
		while (PathIndex < PathPoints.Num() && FVector2D::Distance(Pos, PathPoints[PathIndex]) <= B.WaypointToleranceCm)
		{
			Advance();
		}
		if (PathIndex < PathPoints.Num())
		{
			const double D = FVector2D::Distance(Pos, PathPoints[PathIndex]);
			if (D < BestDist - 1.0)
			{
				BestDist = D;
				ProgressTime = Now;
			}
			else if (Now - ProgressTime > B.StuckSeconds)
			{
				Advance(); // stuck on something: skip this point
			}
		}
		if (PathIndex >= PathPoints.Num())
		{
			bArrived = true;
			return FVector2D::ZeroVector;
		}
		return (PathPoints[PathIndex] - Pos).GetSafeNormal();
	}

	FIntPoint FBotBrain::PickExploreTarget(const FVector2D& Pos, const FBotBehaviourSettings& B)
	{
		const TArray<FIntPoint> Tiles = Graph->WalkableTiles();
		const FIntPoint Here = FPSNav::TileAt(*Map, Pos);
		FIntPoint Best = Here;
		double BestTime = TNumericLimits<double>::Max();
		for (int32 k = 0; k < FMath::Max(1, B.ExploreCandidates) && Tiles.Num() > 0; ++k)
		{
			const FIntPoint T = Tiles[Rng.RandRange(0, Tiles.Num() - 1)];
			const double Visited = VisitTime[Graph->Index(T)];
			if (T != Here && Visited < BestTime)
			{
				BestTime = Visited;
				Best = T;
			}
		}
		return Best;
	}

	void FBotBrain::Investigate(const FVector2D& Target, const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		if (!Map || !Graph)
		{
			return;
		}
		if (!IsMover())
		{
			FacingYaw = YawOf(Target - Pos); // hold the spot, face the stimulus
			return;
		}
		if (PlanTo(FPSNav::TileAt(*Map, Target), Pos, Now, B))
		{
			Mode = EMode::Investigate;
		}
	}

	FBotIntent FBotBrain::Update(const FVector2D& Pos, double Now, const FBotBehaviourSettings& B)
	{
		const float Dt = static_cast<float>(FMath::Max(0.0, Now - LastUpdate));
		LastUpdate = Now;
		FBotIntent I;
		if (!Map || !Graph)
		{
			return I;
		}
		VisitTime[Graph->Index(FPSNav::TileAt(*Map, Pos))] = Now;
		I.bCrouch = bCrouchNow;
		I.LookYawDeg = FacingYaw;

		if (Mode == EMode::LookAround)
		{
			LookYaw = FRotator::NormalizeAxis(LookYaw + B.InvestigateTurnDegPerSec * Dt);
			I.LookYawDeg = LookYaw;
			I.bCrouch = false;
			if (Now >= LookUntil)
			{
				Mode = EMode::Base;
				PathPoints.Reset();
			}
			return I;
		}
		if (Mode == EMode::Investigate)
		{
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			I.bCrouch = false;
			if (!I.MoveDir.IsNearlyZero())
			{
				FacingYaw = YawOf(I.MoveDir);
			}
			I.LookYawDeg = FacingYaw;
			if (bArrived)
			{
				Mode = EMode::LookAround;
				LookUntil = Now + B.InvestigateLookSeconds;
				LookYaw = FacingYaw;
			}
			return I;
		}

		switch (Setup.Behaviour)
		{
		case EBotBehaviour::Static:
			break;
		case EBotBehaviour::Crouch:
			if (Now >= NextToggle)
			{
				bCrouchNow = !bCrouchNow;
				NextToggle = Now + Rng.FRandRange(B.CrouchPeriodMinSeconds, B.CrouchPeriodMaxSeconds);
			}
			I.bCrouch = bCrouchNow;
			break;
		case EBotBehaviour::Strafe:
		{
			const FVector2D Right = RightOf(FacingYaw);
			const double Lateral = FVector2D::DotProduct(Pos - Anchor, Right);
			const bool bPast = (Lateral > B.StrafeHalfWidthCm && StrafeSign > 0.f) || (Lateral < -B.StrafeHalfWidthCm && StrafeSign < 0.f);
			if (Now >= NextToggle || bPast)
			{
				StrafeSign = -StrafeSign;
				NextToggle = Now + Rng.FRandRange(B.StrafePeriodMinSeconds, B.StrafePeriodMaxSeconds);
			}
			I.MoveDir = Right * StrafeSign;
			break;
		}
		case EBotBehaviour::Peek:
		{
			const FVector2D Goal = FPSNav::TileCenterLocal(*Map, bPeekOut ? Setup.PeekExposed : Setup.PeekHidden);
			if (FVector2D::Distance(Pos, Goal) > B.WaypointToleranceCm)
			{
				I.MoveDir = (Goal - Pos).GetSafeNormal();
			}
			else if (Now >= NextToggle)
			{
				bPeekOut = !bPeekOut;
				NextToggle = Now + Rng.FRandRange(B.PeekPeriodMinSeconds, B.PeekPeriodMaxSeconds);
			}
			break;
		}
		case EBotBehaviour::Patrol:
		{
			if (Now < PauseUntil || Waypoints.Num() == 0)
			{
				break;
			}
			if (PathPoints.Num() == 0 && !PlanTo(Waypoints[WaypointIndex], Pos, Now, B))
			{
				WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
				break;
			}
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			if (bArrived)
			{
				WaypointIndex = (WaypointIndex + 1) % Waypoints.Num();
				PathPoints.Reset();
				PauseUntil = Now + Rng.FRandRange(B.PatrolPauseMinSeconds, B.PatrolPauseMaxSeconds);
			}
			break;
		}
		case EBotBehaviour::Explore:
		{
			if (PathPoints.Num() == 0 && !PlanTo(PickExploreTarget(Pos, B), Pos, Now, B))
			{
				break;
			}
			bool bArrived = false;
			I.MoveDir = FollowPath(Pos, Now, B, bArrived);
			if (bArrived)
			{
				PathPoints.Reset();
			}
			break;
		}
		}
		if (IsMover() && !I.MoveDir.IsNearlyZero())
		{
			FacingYaw = YawOf(I.MoveDir);
			I.LookYawDeg = FacingYaw;
		}
		return I;
	}
}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Bots.Brain"`. Expected: `Succeeded: 4  Failed: 0`. Then run the full suite.
  - **If "never inside cover" fails:** a path point offset put the straight line into a cover tile. Consecutive path tiles share an edge, so their union is a rectangle and the segment stays inside it. Check that `PlanTo` adds no offset that exceeds half a tile.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Bots/BotBrain.* Game/FPSAgent/Source/FPSAgent/Tests/BotBrainTests.cpp
git commit -m "[local] Bot brain: Static/Strafe/Peek/Crouch/Patrol/Explore, A* path following with seeded offsets, Investigate (movers walk, others turn)"
git pull --rebase && git push
```

---

### Task 6: Stage-generic episodes and the S1 sampler

**Files:**
- Modify: `Learning/Curriculum.h`, `Learning/Curriculum.cpp`
- Test: `Tests/CurriculumTests.cpp`

**Interfaces:**
- Consumes:
  - `FPSArena::MakeCoverFloorSpec`, `MakeOpenFloorSpec`;
  - `FPSNav::FTileGraph`, `HasTileLos`, `TileAt`, `TileCenterLocal`;
  - `FPSBots::EBotBehaviour`;
  - `FCombatStageSpec`, `FBotBehaviourMix`, `FBotBehaviourSettings`;
  - the existing `Knob` and `FS0Episode`.
- Produces (namespace `FPSCurriculum`):
  - `struct FBotSpawn { FVector2D Offset; float YawDeg; bool bCrouched; FPSBots::EBotBehaviour Behaviour; float SpeedScale; FIntPoint PeekHidden, PeekExposed; }`. The offset is in cm from the arena centre (world XY).
  - `struct FCombatEpisode { float Level; bool bReview; FMapSpec Map; FVector2D AgentOffset; float AgentYawDeg; TArray<FBotSpawn> Bots; }`.
  - `FCombatEpisode FromS0(const FS0Episode&, const FMapSpec& OpenFloor)`.
  - `FCombatEpisode SampleS1(FRandomStream&, float ControllerLevel, bool bAllowReview, const FCurriculumSettings&, const FBotBehaviourSettings&)`.

- [ ] **Step 1: Write the failing test.** In `Tests/CurriculumTests.cpp`:
  - Add `#include "Arena/TileGraph.h"`.
  - Before `#endif`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCurriculumSampleS1Test, "FPSAgent.Combat.Curriculum.SampleS1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCurriculumSampleS1Test::RunTest(const FString& Parameters)
{
	const FCurriculumSettings C; // S1 defaults from MakeS1StageSpec
	const FBotBehaviourSettings B; // 103 deg view cone
	auto CoverCount = [](const FMapSpec& S)
	{
		int32 N = 0;
		for (const FString& Row : S.Tiles) { for (const TCHAR Ch : Row) { N += (Ch == TEXT('l') || Ch == TEXT('h')) ? 1 : 0; } }
		return N;
	};
	auto Check = [&](const FCombatEpisode& E, const TCHAR* What)
	{
		const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(E.Map);
		const FVector2D MapCenter(E.Map.Center().X, E.Map.Center().Y);
		const FVector2D AgentLocal = MapCenter + E.AgentOffset;
		const FIntPoint AgentTile = FPSNav::TileAt(E.Map, AgentLocal);
		TestTrue(FString::Printf(TEXT("%s: agent on a walkable tile"), What), G.IsWalkable(AgentTile));
		TSet<FIntPoint> Used = { AgentTile };
		for (const FBotSpawn& Bot : E.Bots)
		{
			const FVector2D Local = MapCenter + Bot.Offset;
			const FIntPoint Tile = FPSNav::TileAt(E.Map, Local);
			TestTrue(FString::Printf(TEXT("%s: bot on its own walkable tile"), What), G.IsWalkable(Tile) && !Used.Contains(Tile));
			Used.Add(Tile);
			const float ToAgent = FMath::RadiansToDegrees(FMath::Atan2(AgentLocal.Y - Local.Y, AgentLocal.X - Local.X));
			TestTrue(FString::Printf(TEXT("%s: no bot sees the agent at spawn"), What), FMath::Abs(FRotator::NormalizeAxis(ToAgent - Bot.YawDeg)) > 0.5f * B.SightFovDeg);
			if (Bot.Behaviour == FPSBots::EBotBehaviour::Peek)
			{
				TestTrue(FString::Printf(TEXT("%s: peek starts hidden"), What), Bot.PeekHidden == Tile && !FPSNav::HasTileLos(E.Map, AgentTile, Bot.PeekHidden));
				TestTrue(FString::Printf(TEXT("%s: peek steps into the open"), What), FPSNav::HasTileLos(E.Map, AgentTile, Bot.PeekExposed));
			}
		}
	};
	for (int32 Seed = 0; Seed < 40; ++Seed)
	{
		FRandomStream Rng(Seed);
		const FCombatEpisode E = SampleS1(Rng, 0.f, false, C, B);
		Check(E, TEXT("level 0"));
		TestEqual(TEXT("level 0: one bot"), E.Bots.Num(), 1);
		TestTrue(TEXT("level 0: almost no cover (<= 5 tiles, jitter)"), CoverCount(E.Map) <= 5);
		const FPSBots::EBotBehaviour Be = E.Bots[0].Behaviour;
		TestTrue(TEXT("level 0: strafe, peek or crouch"), Be == FPSBots::EBotBehaviour::Strafe || Be == FPSBots::EBotBehaviour::Peek || Be == FPSBots::EBotBehaviour::Crouch);
		TestTrue(TEXT("level 0: 60% speed"), E.Bots[0].SpeedScale >= 0.6f && E.Bots[0].SpeedScale <= 0.64f + 1e-4f);
	}
	bool bPatrol = false, bExplore = false;
	for (int32 Seed = 0; Seed < 30; ++Seed)
	{
		FRandomStream Rng(100 + Seed);
		const FCombatEpisode E = SampleS1(Rng, 1.f, false, C, B);
		Check(E, TEXT("level 1"));
		TestEqual(TEXT("level 1: five bots"), E.Bots.Num(), 5);
		TestTrue(TEXT("level 1: medium cover (43..48 tiles)"), CoverCount(E.Map) >= 43 && CoverCount(E.Map) <= 48);
		for (const FBotSpawn& Bot : E.Bots)
		{
			bPatrol |= Bot.Behaviour == FPSBots::EBotBehaviour::Patrol;
			bExplore |= Bot.Behaviour == FPSBots::EBotBehaviour::Explore;
			TestTrue(TEXT("level 1: full speed"), Bot.SpeedScale >= 0.96f - 1e-4f && Bot.SpeedScale <= 1.f);
		}
	}
	TestTrue(TEXT("level 1 mixes in patrol and explore"), bPatrol && bExplore);
	FRandomStream A(42), Bs(42);
	const FCombatEpisode EA = SampleS1(A, 0.6f, true, C, B);
	const FCombatEpisode EB = SampleS1(Bs, 0.6f, true, C, B);
	TestTrue(TEXT("deterministic map"), EA.Map.Tiles == EB.Map.Tiles);
	TestTrue(TEXT("deterministic agent"), EA.AgentOffset.Equals(EB.AgentOffset, 1e-6) && EA.AgentYawDeg == EB.AgentYawDeg);
	TestEqual(TEXT("deterministic bots"), EA.Bots.Num(), EB.Bots.Num());

	FS0Episode S0;
	S0.AgentYawDeg = 30.f;
	S0.BotOffsets = { FVector2D(1000.0, 0.0) };
	S0.BotCrouched = { true };
	const FCombatEpisode F = FromS0(S0, FPSArena::MakeOpenFloorSpec(20, TEXT("s0")));
	TestTrue(TEXT("S0: agent at the centre"), F.AgentOffset.IsNearlyZero());
	TestEqual(TEXT("S0: bot faces the agent"), F.Bots[0].YawDeg, 180.f, 1e-3f);
	TestTrue(TEXT("S0: static and crouched"), F.Bots[0].Behaviour == FPSBots::EBotBehaviour::Static && F.Bots[0].bCrouched);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'SampleS1': identifier not found`.

- [ ] **Step 3: Implement.** In `Learning/Curriculum.h`:
  - Add the includes `#include "Arena/MapSpec.h"` and `#include "Bots/BotBrain.h"`.
  - After `SampleS0`, inside `namespace FPSCurriculum`, add:

```cpp
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
```

  In `Learning/Curriculum.cpp`:
  - Add the includes `#include "Arena/FloorGenerator.h"` and `#include "Arena/TileGraph.h"`.
  - Inside the existing anonymous namespace (next to `MinBotGapCm` and `PlacementTries`), add:

```cpp
	FPSBots::EBotBehaviour SampleBehaviour(FRandomStream& Rng, const FBotBehaviourMix& Easy, const FBotBehaviourMix& Hard, float Level)
	{
		const float L = FMath::Clamp(Level, 0.f, 1.f);
		const float W[6] = {
			FMath::Lerp(Easy.Static, Hard.Static, L), FMath::Lerp(Easy.Strafe, Hard.Strafe, L), FMath::Lerp(Easy.Peek, Hard.Peek, L),
			FMath::Lerp(Easy.Crouch, Hard.Crouch, L), FMath::Lerp(Easy.Patrol, Hard.Patrol, L), FMath::Lerp(Easy.Explore, Hard.Explore, L) };
		float Sum = 0.f;
		for (const float Wi : W) { Sum += FMath::Max(0.f, Wi); }
		if (Sum <= 0.f)
		{
			return FPSBots::EBotBehaviour::Static;
		}
		float R = Rng.FRand() * Sum;
		for (int32 i = 0; i < 6; ++i)
		{
			R -= FMath::Max(0.f, W[i]);
			if (R < 0.f) { return static_cast<FPSBots::EBotBehaviour>(i); }
		}
		return FPSBots::EBotBehaviour::Explore;
	}

	/** A free tile near Near (radius 2, row-major) hidden from the agent's tile, with a free 4-neighbour the agent can see. */
	bool FindPeekPair(const FMapSpec& Map, const FPSNav::FTileGraph& G, const FIntPoint& Agent, const FIntPoint& Near, const TSet<FIntPoint>& Taken, FIntPoint& OutHidden, FIntPoint& OutExposed)
	{
		static const FIntPoint Dirs[4] = { FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
		for (int32 Dr = -2; Dr <= 2; ++Dr)
		{
			for (int32 Dc = -2; Dc <= 2; ++Dc)
			{
				const FIntPoint H = Near + FIntPoint(Dr, Dc);
				if (!G.IsWalkable(H) || H == Agent || Taken.Contains(H) || FPSNav::HasTileLos(Map, Agent, H))
				{
					continue;
				}
				for (const FIntPoint& D : Dirs)
				{
					const FIntPoint E = H + D;
					if (G.IsWalkable(E) && E != Agent && !Taken.Contains(E) && FPSNav::HasTileLos(Map, Agent, E))
					{
						OutHidden = H;
						OutExposed = E;
						return true;
					}
				}
			}
		}
		return false;
	}
```

  Inside `namespace FPSCurriculum`, after `SampleS0`, add:

```cpp
	FCombatEpisode FromS0(const FS0Episode& E, const FMapSpec& OpenFloor)
	{
		FCombatEpisode Out;
		Out.Level = E.Level;
		Out.bReview = E.bReview;
		Out.Map = OpenFloor;
		Out.AgentYawDeg = E.AgentYawDeg;
		for (int32 i = 0; i < E.BotOffsets.Num(); ++i)
		{
			FBotSpawn Bot;
			Bot.Offset = E.BotOffsets[i];
			Bot.YawDeg = FRotator::NormalizeAxis(FMath::RadiansToDegrees(FMath::Atan2(-Bot.Offset.Y, -Bot.Offset.X))); // face the agent at the centre ((-180, 180])
			Bot.bCrouched = E.BotCrouched.IsValidIndex(i) && E.BotCrouched[i];
			Out.Bots.Add(Bot);
		}
		return Out;
	}

	FCombatEpisode SampleS1(FRandomStream& Rng, float ControllerLevel, bool bAllowReview, const FCurriculumSettings& C, const FBotBehaviourSettings& B)
	{
		const FCombatStageSpec& S = C.S1;
		FCombatEpisode E;
		E.bReview = bAllowReview && Rng.FRand() < C.ReviewFraction;
		E.Level = E.bReview ? Rng.FRandRange(0.f, ControllerLevel) : ControllerLevel;
		const float Density = Knob(Rng, S.CoverDensityEasy, S.CoverDensityHard, E.Level, C.JitterFraction);
		E.Map = FPSArena::MakeCoverFloorSpec(S.FloorTiles, Density, S.LowCoverFraction, Rng, TEXT("s1floor"));
		const FPSNav::FTileGraph G = FPSNav::FTileGraph::Build(E.Map);
		const TArray<FIntPoint> Free = G.WalkableTiles();
		const FVector2D MapCenter(E.Map.Center().X, E.Map.Center().Y);
		const FIntPoint AgentTile = Free[Rng.RandRange(0, Free.Num() - 1)];
		const FVector2D AgentLocal = FPSNav::TileCenterLocal(E.Map, AgentTile);
		E.AgentOffset = AgentLocal - MapCenter;
		E.AgentYawDeg = Rng.FRandRange(-180.f, 180.f);

		const int32 MaxBots = FMath::Max(S.BotsEasy, S.BotsHard);
		const int32 Count = FMath::Clamp(FMath::RoundToInt(Knob(Rng, S.BotsEasy, S.BotsHard, E.Level, C.JitterFraction)), 1, MaxBots);
		const float MaxDist = Knob(Rng, S.BotDistanceMaxCmEasy, S.BotDistanceMaxCmHard, E.Level, C.JitterFraction);
		const float MinDist = FMath::Min(S.BotDistanceMinCm, MaxDist);
		const float Spread = Knob(Rng, S.BotSpreadDegEasy, S.BotSpreadDegHard, E.Level, C.JitterFraction);
		const float CrouchChance = Knob(Rng, S.CrouchedFractionEasy, S.CrouchedFractionHard, E.Level, C.JitterFraction);
		// Facing away from the agent by up to this much keeps the agent outside the bot's view cone, with a 10 degree margin.
		const float AwayHalfDeg = FMath::Max(0.f, 180.f - 0.5f * B.SightFovDeg - 10.f);
		TSet<FIntPoint> Taken = { AgentTile };
		for (int32 i = 0; i < Count; ++i)
		{
			FIntPoint Tile(-1, -1);
			for (int32 Try = 0; Try < PlacementTries && Tile.X < 0; ++Try)
			{
				const float Yaw = FMath::DegreesToRadians(E.AgentYawDeg + Rng.FRandRange(-Spread, Spread));
				const float Dist = Rng.FRandRange(MinDist, MaxDist);
				const FIntPoint T = FPSNav::TileAt(E.Map, AgentLocal + FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)) * Dist);
				if (G.IsWalkable(T) && !Taken.Contains(T))
				{
					Tile = T;
				}
			}
			for (int32 k = 0; k < Free.Num() && Tile.X < 0; ++k) // crowded: the first free tile
			{
				if (!Taken.Contains(Free[k])) { Tile = Free[k]; }
			}
			if (Tile.X < 0)
			{
				break;
			}
			FBotSpawn Bot;
			Bot.Behaviour = SampleBehaviour(Rng, S.BehaviourMixEasy, S.BehaviourMixHard, E.Level);
			Bot.bCrouched = Rng.FRand() < CrouchChance;
			Bot.SpeedScale = Knob(Rng, S.BotSpeedScaleEasy, S.BotSpeedScaleHard, E.Level, C.JitterFraction);
			if (Bot.Behaviour == FPSBots::EBotBehaviour::Peek)
			{
				FIntPoint Hidden, Exposed;
				if (FindPeekPair(E.Map, G, AgentTile, Tile, Taken, Hidden, Exposed))
				{
					Tile = Hidden;
					Bot.PeekHidden = Hidden;
					Bot.PeekExposed = Exposed;
					Taken.Add(Exposed);
				}
				else
				{
					Bot.Behaviour = FPSBots::EBotBehaviour::Strafe; // no cover nearby (D-031)
				}
			}
			Taken.Add(Tile);
			const FVector2D Local = FPSNav::TileCenterLocal(E.Map, Tile);
			Bot.Offset = Local - MapCenter;
			const float ToAgent = FMath::RadiansToDegrees(FMath::Atan2(AgentLocal.Y - Local.Y, AgentLocal.X - Local.X));
			Bot.YawDeg = FRotator::NormalizeAxis(ToAgent + 180.f + Rng.FRandRange(-AwayHalfDeg, AwayHalfDeg));
			E.Bots.Add(Bot);
		}
		return E;
	}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Combat.Curriculum"`. Expected: `Succeeded: 3  Failed: 0`. Then run the full suite.
  - **If the level 1 cover count is 0:** `MakeCoverFloorSpec` fell back to the open floor. Report the seed, because that should be very rare at 12% density.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/Curriculum.* Game/FPSAgent/Source/FPSAgent/Tests/CurriculumTests.cpp
git commit -m "[local] Stage-generic FCombatEpisode; S1 sampler: cover, free-tile spawns, bots facing away (agent unseen), behaviour mix, speed, peek pairs"
git pull --rebase && git push
```

---

### Task 7: Bean speed scale, crouch applied at once, and bot driving

**Files:**
- Modify: `Character/BeanCharacter.h/.cpp`, `Bots/ScriptedBot.h/.cpp`
- Test: `Tests/CombatBodyTests.cpp`

**Interfaces:**
- Consumes: `FPSBots::FBotIntent` (Task 5).
- Produces:
  - `void ABeanCharacter::SetSpeedScale(float)`, `void ABeanCharacter::ApplyCrouchNow(bool)`.
  - `void AScriptedBot::Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed, float SpeedScale = 1.f)`.
  - `void AScriptedBot::Drive(const FPSBots::FBotIntent& Intent, float DeltaSeconds, float TurnRateDegPerSec)`.
- Fixes the S0 final-review finding: a bean teleported to standing height with a crouched capsule floated, then fell about 30 cm. `Crouch()`/`UnCrouch()` only take effect on the next movement tick; `ApplyCrouchNow` changes the capsule at once and keeps the feet on the floor.

- [ ] **Step 1: Write the failing test.** In `Tests/CombatBodyTests.cpp`, add the includes `"Bots/BotBrain.h"`, `"Common/FPSAgentSettings.h"`, `"Components/CapsuleComponent.h"` and `"GameFramework/CharacterMovementComponent.h"` (skip any already present). Then, before `#endif`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSScriptedBotCrouchDriveTest, "FPSAgent.Bots.ScriptedBot.CrouchAndDrive",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSScriptedBotCrouchDriveTest::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AScriptedBot* Bot = W.World->SpawnActor<AScriptedBot>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator, P);
	const UFPSAgentSettings& S = UFPSAgentSettings::Get();
	const double StandZ = S.Body.StandingHalfHeight + 2.0; // the spawn convention: feet 2 cm above the floor top (z = 0)

	Bot->Activate(FVector(0.0, 0.0, StandZ), 0.f, true, 1, 0.6f);
	TestTrue(TEXT("crouched at once"), Bot->bIsCrouched);
	TestEqual(TEXT("crouched capsule"), static_cast<float>(Bot->GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight()), S.Body.CrouchedHalfHeight, 0.5f);
	TestEqual(TEXT("feet stay on the floor"), static_cast<float>(Bot->GetActorLocation().Z), static_cast<float>(S.Body.CrouchedHalfHeight + 2.0), 1.f);
	TestEqual(TEXT("speed scale"), Bot->GetCharacterMovement()->MaxWalkSpeed, S.Movement.RunSpeed * 0.6f, 0.01f);

	Bot->Activate(FVector(0.0, 0.0, StandZ), 0.f, false, 2, 1.f);
	TestFalse(TEXT("standing at once"), Bot->bIsCrouched);
	TestEqual(TEXT("standing height"), static_cast<float>(Bot->GetActorLocation().Z), static_cast<float>(StandZ), 1.f);
	TestEqual(TEXT("full speed again"), Bot->GetCharacterMovement()->MaxWalkSpeed, S.Movement.RunSpeed, 0.01f);

	FPSBots::FBotIntent I;
	I.MoveDir = FVector2D(0.0, 1.0);
	I.LookYawDeg = 90.f;
	Bot->Drive(I, 0.1f, 360.f);
	TestTrue(TEXT("move input toward +Y"), Bot->GetPendingMovementInputVector().GetSafeNormal().Equals(FVector(0.0, 1.0, 0.0), 1e-3));
	TestEqual(TEXT("turns at most 36 deg in 0.1 s at 360 deg/s"), static_cast<float>(Bot->GetActorRotation().Yaw), 36.f, 1e-3f);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'Drive': is not a member of 'AScriptedBot'` (or the `Activate` argument count).

- [ ] **Step 3: Implement.**
  - **`Character/BeanCharacter.h`.** In the public section, after `IsWalking()`, add:

```cpp
	/** Movement speed multiplier for run, walk and crouch (scripted bots: the stage's BotSpeedScale). */
	void SetSpeedScale(float Scale);
	/** Crouch or stand up now, keeping the feet on the floor (episode starts: Crouch()/UnCrouch() would wait for the next movement tick). */
	void ApplyCrouchNow(bool bCrouch);
```

    In the private section, add `float SpeedScale = 1.f;`.

  - **`Character/BeanCharacter.cpp`.**
    - In `ApplyMovementTuning()`, change the two speed lines to `Move->MaxWalkSpeed = (bWalking ? M.WalkSpeed : M.RunSpeed) * SpeedScale;` and `Move->MaxWalkSpeedCrouched = M.CrouchSpeed * SpeedScale;`.
    - In `SetWalking`, change its speed line to `GetCharacterMovement()->MaxWalkSpeed = (bWalking ? M.WalkSpeed : M.RunSpeed) * SpeedScale;`.
    - Add:

```cpp
void ABeanCharacter::SetSpeedScale(float Scale)
{
	SpeedScale = FMath::Max(0.f, Scale);
	ApplyMovementTuning();
}

void ABeanCharacter::ApplyCrouchNow(bool bCrouch)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->bWantsToCrouch = bCrouch;
	if (bCrouch)
	{
		Move->Crouch(false);   // resizes the capsule now; the base stays on the floor while walking
	}
	else
	{
		Move->UnCrouch(false);
	}
}
```

  - **`Bots/ScriptedBot.h`.**
    - Add `#include "Bots/BotBrain.h"`.
    - Change `Activate` to `void Activate(const FVector& Location, float YawDeg, bool bCrouched, int32 Seed, float SpeedScale = 1.f);`.
    - Add:

```cpp
	/** Applies one tick of the brain's intent: movement input, crouch and body yaw (turning at TurnRateDegPerSec). Dead or inactive bots ignore it. */
	void Drive(const FPSBots::FBotIntent& Intent, float DeltaSeconds, float TurnRateDegPerSec);
```

  - **`Bots/ScriptedBot.cpp`.**
    - In `Activate`, replace the final line `if (bCrouched) { Crouch(); } else { UnCrouch(); }` with:

```cpp
	SetSpeedScale(SpeedScale);
	ApplyCrouchNow(bCrouched); // after the teleport: the capsule is resized now and the feet stay on the floor
```

    - Then add:

```cpp
void AScriptedBot::Drive(const FPSBots::FBotIntent& Intent, float DeltaSeconds, float TurnRateDegPerSec)
{
	if (!IsAlive())
	{
		return;
	}
	if (!Intent.MoveDir.IsNearlyZero())
	{
		AddMovementInput(FVector(Intent.MoveDir.X, Intent.MoveDir.Y, 0.0), 1.f);
	}
	if (Intent.bCrouch != GetCharacterMovement()->bWantsToCrouch)
	{
		if (Intent.bCrouch) { Crouch(); } else { UnCrouch(); }
	}
	const float Current = GetActorRotation().Yaw;
	const float Delta = FRotator::NormalizeAxis(Intent.LookYawDeg - Current);
	const float MaxStep = TurnRateDegPerSec * DeltaSeconds;
	const FRotator Facing(0.0, FRotator::NormalizeAxis(Current + FMath::Clamp(Delta, -MaxStep, MaxStep)), 0.0);
	if (AController* C = GetController())
	{
		C->SetControlRotation(Facing); // the bean turns with its controller's yaw
	}
	SetActorRotation(Facing);
}
```

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Bots.ScriptedBot"`. Expected: `Succeeded: 2  Failed: 0`. Then run the full suite. The existing `Lifecycle` test still checks `bWantsToCrouch`, which `ApplyCrouchNow` sets.
  - **If "crouched at once" fails in the test world:** `UCharacterMovementComponent::Crouch` needs walking mode. `SetDeadState(false)` sets it, so check that it runs before `ApplyCrouchNow`.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Character/BeanCharacter.* Game/FPSAgent/Source/FPSAgent/Bots/ScriptedBot.* Game/FPSAgent/Source/FPSAgent/Tests/CombatBodyTests.cpp
git commit -m "[local] Beans: speed scale, crouch applied at once on teleport (no float/fall); AScriptedBot::Drive applies a brain intent"
git pull --rebase && git push
```

---

### Task 8: `UCombatArena` for S1: cover pool, bot brains and senses, sounds, hearing

**Files:**
- Modify: `Learning/CombatArena.h`, `Learning/CombatArena.cpp`
- Test: `Tests/CombatArenaTests.cpp`

**Interfaces:**
- Consumes:
  - Tasks 1–7: the stage specs, `FPSNav`, `FPSBots`, `FCombatEpisode`/`FromS0`, `PackSound`, `Activate(..., SpeedScale)`, `Drive`, `ApplyCrouchNow`;
  - existing `FPSArena::SpawnBlock` (movable cube) and `BuildMap`.
- Produces (`UCombatArena`):
  - `void Build(UWorld*, int32 ArenaIndex, const FVector& Center, int32 BaseSeed, int32 StageIndex = 0)`;
  - `void StartEpisode(const FPSCurriculum::FCombatEpisode&, int32 Seed, double Now)`. The `FS0Episode` overload stays as a wrapper, so build A's tests are unchanged.
  - `static float FloorHalfSizeCm(int32 StageIndex = 0)`;
  - accessors `GetBaseSpec()`, `GetStageIndex()`, `GetBotBrain(int32)`, `GetSounds()`, `GetVisibleCoverCount()`;
  - `void EmitSound(const FPSBots::FSoundEvent&)`.

- [ ] **Step 1: Write the failing test.** In `Tests/CombatArenaTests.cpp`, before `#endif`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaS1Test, "FPSAgent.Combat.Arena.S1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaS1Test::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7, /*StageIndex=*/1);
	TestEqual(TEXT("S1 arena"), A->GetStageIndex(), 1);

	FPSCurriculum::FCombatEpisode E;
	E.Map = A->GetBaseSpec();
	const int32 Mid = E.Map.Rows() / 2;            // 11 on the 22 x 22 S1 floor
	E.Map.Tiles[Mid - 3][Mid] = TEXT('h');          // world centre (1000, 200): a high block north of the agent
	E.Map.Tiles[Mid][Mid + 3] = TEXT('l');          // world centre (-200, 1400): a low block east of the agent
	E.AgentYawDeg = 0.f;
	FPSCurriculum::FBotSpawn Patrol;
	Patrol.Offset = FVector2D(800.0, 800.0);
	Patrol.YawDeg = 45.f;                            // facing away from the agent: it can't see it
	Patrol.Behaviour = FPSBots::EBotBehaviour::Patrol;
	FPSCurriculum::FBotSpawn Static;
	Static.Offset = FVector2D(-800.0, 0.0);
	Static.Behaviour = FPSBots::EBotBehaviour::Static;
	E.Bots = { Patrol, Static };
	A->StartEpisode(E, 11, 0.0);

	TestEqual(TEXT("two cover blocks placed"), A->GetVisibleCoverCount(), 2);
	TestFalse(TEXT("high cover blocks sight (moved pool block has collision)"), FPSPerception::HasLineOfSight(W.World, FVector(500.0, 200.0, 100.0), FVector(1500.0, 200.0, 100.0)));
	TestFalse(TEXT("low cover blocks at 1 m"), FPSPerception::HasLineOfSight(W.World, FVector(-200.0, 1000.0, 100.0), FVector(-200.0, 1800.0, 100.0)));
	TestTrue(TEXT("low cover is clear above 1.2 m"), FPSPerception::HasLineOfSight(W.World, FVector(-200.0, 1000.0, 150.0), FVector(-200.0, 1800.0, 150.0)));

	A->TickAction(1.f / 60.f);
	TestFalse(TEXT("the patrol bot walks"), A->GetBot(0)->GetPendingMovementInputVector().IsNearlyZero());
	TestTrue(TEXT("the static bot holds"), A->GetBot(1)->GetPendingMovementInputVector().IsNearlyZero());

	A->GetAgent()->GetRifle()->FireShot(0);
	bool bGunshot = false;
	for (const FPSBots::FSoundEvent& S : A->GetSounds()) { bGunshot |= S.Kind == FPSBots::ESoundKind::Gunshot && S.Source == INDEX_NONE; }
	TestTrue(TEXT("the agent's shot is a sound event"), bGunshot);
	A->PrepareDecision(0.1);
	TestFalse(TEXT("not before the reaction delay"), A->GetBotBrain(0).IsInvestigating());
	A->PrepareDecision(0.6);
	TestTrue(TEXT("the patrol bot investigates the shot it heard"), A->GetBotBrain(0).IsInvestigating());

	FPSBots::FSoundEvent Step;
	Step.Position = FVector(0.0, 1000.0, 100.0);
	Step.Time = 0.55;
	Step.Kind = FPSBots::ESoundKind::Footstep;
	Step.Source = 0;
	A->EmitSound(Step);
	FPSCombatObs::FObservationData O;
	A->BuildObservation(O, 0.6);
	TestEqual(TEXT("the agent hears one enemy sound"), O.Sounds.Num(), 1);
	if (O.Sounds.Num() == 1)
	{
		TestTrue(TEXT("...to its right (+Y at yaw 0)"), O.Sounds[0][1] > 0.9f);
	}
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: errors such as `'GetStageIndex': is not a member of 'UCombatArena'` and too many arguments to `Build`.

- [ ] **Step 3: Implement `Learning/CombatArena.h`.**
  - Add the includes `"Arena/MapSpec.h"`, `"Arena/TileGraph.h"`, `"Bots/BotBrain.h"` and `"Bots/BotSenses.h"`.
  - Add the forward declaration `class AStaticMeshActor;`.
  - In the public section:
    - Replace the `Build` declaration with `void Build(UWorld* World, int32 InArenaIndex, const FVector& InCenter, int32 InBaseSeed, int32 InStageIndex = 0);`.
    - Replace `static float FloorHalfSizeCm();` with `static float FloorHalfSizeCm(int32 InStageIndex = 0);`.
    - Next to the existing `StartEpisode`, add:

```cpp
	/** Places the agent, bots and (S1+) cover for an episode of any stage, and resets every per-episode counter. */
	void StartEpisode(const FPSCurriculum::FCombatEpisode& Config, int32 Seed, double Now);
	const FMapSpec& GetBaseSpec() const { return BaseSpec; }
	int32 GetStageIndex() const { return StageIndex; }
	const FPSBots::FBotBrain& GetBotBrain(int32 Index) const { return Brains[Index]; }
	const TArray<FPSBots::FSoundEvent>& GetSounds() const { return Sounds; }
	int32 GetVisibleCoverCount() const { return VisibleCover; }
	/** Adds a sound to this arena's buffer (bots and the agent hear only their own arena: hard rule 3). */
	void EmitSound(const FPSBots::FSoundEvent& Event) { Sounds.Add(Event); }
```

  - In the private section:
    - Add `const FCombatStageSpec& StageSpec() const;`, `FVector2D ToLocal(const FVector& World) const;`, `void ApplyCover(const FMapSpec& Map);`, `void TickBots(double Now, float DeltaSeconds);`, `void EmitFootsteps(double Now);` and `void SenseBots(double Now);`.
    - Add the members:

```cpp
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
	/** Next footstep time per source: [0] = the agent, [i + 1] = bot i. */
	TArray<double> NextFootstep;
	/** The -Scripted reference's explore brain (S1+). The policy never reads it. */
	FPSBots::FBotBrain AgentBrain;
	double LastScriptedSoundTime = -1.0e9;
```

- [ ] **Step 4: Implement `Learning/CombatArena.cpp`.**

  1. **Includes and the stage helper.**
     - Add the includes `"Engine/StaticMeshActor.h"` and `"Arena/ArenaBuilder.h"`.
     - Delete `Stage()` from the anonymous namespace.
     - Replace every remaining `Stage()` call in the file with `StageSpec()`.
     - Add:

```cpp
const FCombatStageSpec& UCombatArena::StageSpec() const
{
	return Settings().Curriculum.Stage(StageIndex);
}

FVector2D UCombatArena::ToLocal(const FVector& World) const
{
	const FVector L = World - MapOrigin;
	return FVector2D(L.X, L.Y);
}
```

  2. **Floor size helpers.** Replace `FloorHalfSizeCm` and `GetBotRadiusCm` with:

```cpp
float UCombatArena::FloorHalfSizeCm(int32 InStageIndex)
{
	return 0.5f * (Settings().Curriculum.Stage(InStageIndex).FloorTiles + 2) * FMapSpec().TileCm;
}

float UCombatArena::GetBotRadiusCm() const
{
	return 0.5f * StageSpec().FloorTiles * FMapSpec().TileCm - WallMarginCm;
}
```

  3. **`Build`.**
     - Change the signature to take `int32 InStageIndex` and set `StageIndex = InStageIndex;` first.
     - Replace the two floor lines (`MakeOpenFloorSpec` and `BuildMap`) with:

```cpp
	BaseSpec = FPSArena::MakeOpenFloorSpec(StageSpec().FloorTiles, FString::Printf(TEXT("floor_%d"), ArenaIndex));
	MapOrigin = Center - BaseSpec.Center();
	FPSArena::BuildMap(World, BaseSpec, MapOrigin);
	CurrentMap = BaseSpec;
	Graph = FPSNav::FTileGraph::Build(CurrentMap);
	// Cover changes per episode (S1+): a pool of movable cubes sized for the densest layout, parked hidden below the floor (D-031).
	const int32 Interior = StageSpec().FloorTiles * StageSpec().FloorTiles;
	const int32 PoolSize = FMath::CeilToInt(StageSpec().CoverDensityHard * (1.f + Settings().Curriculum.JitterFraction) * Interior);
	for (int32 i = 0; i < PoolSize; ++i)
	{
		AStaticMeshActor* Block = FPSArena::SpawnBlock(World, MapOrigin + FVector(0.0, 0.0, -1000.0 - 10.0 * i), FVector(BaseSpec.TileCm, BaseSpec.TileCm, FPSMapFormat::LowCoverCm));
		Block->SetActorHiddenInGame(true);
		Block->SetActorEnableCollision(false);
		CoverPool.Add(Block);
	}
```

     - At the end of `Build`, after the bot loop, add:

```cpp
	Brains.SetNum(Bots.Num());
	Senses.SetNum(Bots.Num());
	BotRngs.SetNum(Bots.Num());
	NextFootstep.Init(0.0, Bots.Num() + 1);
```

  4. **Cover placement.** Add:

```cpp
void UCombatArena::ApplyCover(const FMapSpec& Map)
{
	int32 Used = 0;
	for (int32 R = 0; R < Map.Rows(); ++R)
	{
		for (int32 C = 0; C < Map.Cols(); ++C)
		{
			const TCHAR Ch = Map.TileAt(R, C);
			if (Ch != TEXT('l') && Ch != TEXT('h'))
			{
				continue;
			}
			if (!CoverPool.IsValidIndex(Used))
			{
				UE_LOG(LogFPSAgent, Warning, TEXT("Combat: arena %d's cover pool (%d) is too small for this layout"), ArenaIndex, CoverPool.Num());
				break;
			}
			const float H = Ch == TEXT('h') ? FPSMapFormat::HighCoverCm : FPSMapFormat::LowCoverCm;
			AStaticMeshActor* Block = CoverPool[Used++];
			Block->SetActorLocation(MapOrigin + Map.TileCenter(R, C) + FVector(0.0, 0.0, 0.5 * H)); // from the floor top up
			Block->SetActorScale3D(FVector(Map.TileCm, Map.TileCm, H) / 100.0);
			Block->SetActorHiddenInGame(false);
			Block->SetActorEnableCollision(true);
		}
	}
	for (int32 i = Used; i < CoverPool.Num(); ++i)
	{
		CoverPool[i]->SetActorHiddenInGame(true);
		CoverPool[i]->SetActorEnableCollision(false);
		CoverPool[i]->SetActorLocation(MapOrigin + FVector(0.0, 0.0, -1000.0 - 10.0 * i));
	}
	VisibleCover = Used;
}
```

  5. **Episode start.**
     - Rename the existing `StartEpisode(const FPSCurriculum::FS0Episode&, ...)` body away; replace it with the wrapper below.
     - Then implement the stage-generic version. It keeps everything the S0 version did; only the noted lines change:

```cpp
void UCombatArena::StartEpisode(const FPSCurriculum::FS0Episode& Config, int32 Seed, double Now)
{
	StartEpisode(FPSCurriculum::FromS0(Config, BaseSpec), Seed, Now);
}

void UCombatArena::StartEpisode(const FPSCurriculum::FCombatEpisode& Config, int32 Seed, double Now)
{
	++EpisodeCount;
	bReview = Config.bReview;
	Level = Config.Level;
	EpisodeStart = Now;
	LastHitTime = -1.0e9;
	LastScriptedSoundTime = -1.0e9;
	Held = FPSCombatObs::FAgentAction();
	Kills = 0;
	Shots = 0;
	Hits = 0;
	Headshots = 0;
	Tracks.Reset();
	Sounds.Reset();
	for (double& T : NextFootstep) { T = Now; }
	CurrentMap = Config.Map;
	Graph = FPSNav::FTileGraph::Build(CurrentMap);
	if (StageIndex > 0)
	{
		ApplyCover(CurrentMap);
	}
	if (!Agent)
	{
		return;
	}
	const UFPSAgentSettings& S = Settings();
	const FVector Spawn = Center + FVector(Config.AgentOffset.X, Config.AgentOffset.Y, 0.0) + SpawnOffset();
	const FRotator Facing(0.0, Config.AgentYawDeg, 0.0);
	Agent->SetActorLocationAndRotation(Spawn, Facing, false, nullptr, ETeleportType::TeleportPhysics);
	if (AController* C = Agent->GetController())
	{
		C->SetControlRotation(Facing);
	}
	Agent->GetHealth()->ResetToFull();
	Agent->GetRifle()->ResetWeapon();
	Agent->GetRifle()->SeedRandom(static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x9E3779B9u) & 0x7fffffff)); // own stream, not the sampler's
	Agent->SetDeadState(false);
	Agent->SetWalking(false);
	Agent->StopJumping();
	Agent->GetCharacterMovement()->StopMovementImmediately();
	Agent->ApplyCrouchNow(false); // stand up now: a crouched capsule teleported to standing height would float, then fall

	ActiveBots = 0;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot)
		{
			continue;
		}
		if (!Config.Bots.IsValidIndex(i))
		{
			Bot->Deactivate();
			continue;
		}
		const FPSCurriculum::FBotSpawn& B = Config.Bots[i];
		const FVector Pos = Center + FVector(B.Offset.X, B.Offset.Y, 0.0) + SpawnOffset();
		const int32 BotSeed = static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), static_cast<uint32>(i + 1)) & 0x7fffffff);
		Bot->Activate(Pos, B.YawDeg, B.bCrouched, BotSeed, B.SpeedScale);
		FPSBots::FBotSetup Setup;
		Setup.Behaviour = B.Behaviour;
		Setup.bCrouched = B.bCrouched;
		Setup.FacingYawDeg = B.YawDeg;
		Setup.PeekHidden = B.PeekHidden;
		Setup.PeekExposed = B.PeekExposed;
		Brains[i].Start(Setup, CurrentMap, Graph, static_cast<int32>(HashCombineFast(static_cast<uint32>(BotSeed), 0xB4A1Fu) & 0x7fffffff), ToLocal(Pos), Now, S.Bots);
		Senses[i].Reset();
		BotRngs[i].Initialize(static_cast<int32>(HashCombineFast(static_cast<uint32>(BotSeed), 0x5E45Eu) & 0x7fffffff));
		++ActiveBots;
	}

	FPSBots::FBotSetup AgentSetup;
	AgentSetup.Behaviour = FPSBots::EBotBehaviour::Explore;
	AgentSetup.FacingYawDeg = Config.AgentYawDeg;
	AgentBrain.Start(AgentSetup, CurrentMap, Graph, static_cast<int32>(HashCombineFast(static_cast<uint32>(Seed), 0x51EDu) & 0x7fffffff), ToLocal(Spawn), Now, S.Bots);

	// Initial tracks (no reward) so the first observation already sees what is in view; the aim helper starts from here.
	TArray<FPSPerception::FSighting> Sightings;
	Sense(Sightings);
	FPSPerception::UpdateTracks(Tracks, Sightings, Now, S.Combat.Perception.MemorySeconds);
	Ledger.BeginEpisode(FPSCombatReward::AimPotential(NearestVisibleAimErrorDeg(Sightings), StageSpec().AimHelperWeight, S.Combat.Reward));
}
```

  6. **Ticks: bots act and footsteps sound.** In `TickAction`, right after the `Ledger.Add(ETerm::Time, ...)` line and before the dead-agent early return, add:

```cpp
	const double TickNow = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	TickBots(TickNow, DeltaSeconds);
	EmitFootsteps(TickNow);
```

     Then add:

```cpp
void UCombatArena::TickBots(double Now, float DeltaSeconds)
{
	const FBotBehaviourSettings& B = Settings().Bots;
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (Bot && Bot->IsAlive())
		{
			Bot->Drive(Brains[i].Update(ToLocal(Bot->GetActorLocation()), Now, B), DeltaSeconds, B.TurnRateDegPerSec);
		}
	}
}

void UCombatArena::EmitFootsteps(double Now)
{
	const UFPSAgentSettings& S = Settings();
	auto Emit = [&](const ABeanCharacter* Bean, int32 Source, int32 Slot)
	{
		if (!Bean || !NextFootstep.IsValidIndex(Slot) || Now < NextFootstep[Slot])
		{
			return;
		}
		if (!FPSBots::IsAudibleMovement(Bean->GetHorizontalSpeed(), Bean->bIsCrouched, Bean->IsWalking(), S.Movement.WalkSpeed, S.Bots))
		{
			return;
		}
		FPSBots::FSoundEvent E;
		E.Position = Bean->GetActorLocation();
		E.Time = Now;
		E.Kind = FPSBots::ESoundKind::Footstep;
		E.Source = Source;
		Sounds.Add(E);
		NextFootstep[Slot] = Now + S.Bots.FootstepIntervalSeconds;
	};
	if (!IsAgentDead())
	{
		Emit(Agent, INDEX_NONE, 0);
	}
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		if (Bots[i] && Bots[i]->IsAlive())
		{
			Emit(Bots[i], i, i + 1);
		}
	}
	const double Keep = S.Bots.ReactionSeconds + S.Bots.SoundMemorySeconds + 1.0;
	Sounds.RemoveAll([Now, Keep](const FPSBots::FSoundEvent& E) { return Now - E.Time > Keep; });
}
```

  7. **Gunshots are sounds.** At the top of `OnAgentShot`, add:

```cpp
	if (Agent)
	{
		FPSBots::FSoundEvent E;
		E.Position = Agent->GetActorLocation();
		E.Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		E.Kind = FPSBots::ESoundKind::Gunshot;
		E.Source = INDEX_NONE;
		Sounds.Add(E);
	}
```

  8. **Bot senses, once per decision.** At the top of `PrepareDecision`, add `SenseBots(Now);`. Then add:

```cpp
void UCombatArena::SenseBots(double Now)
{
	if (!Agent)
	{
		return;
	}
	const FBotBehaviourSettings& B = Settings().Bots;
	const UWorld* World = GetWorld();
	const bool bAgentAlive = !IsAgentDead();
	const FVector Head = Agent->GetHitbox(EHitZone::Head)->GetComponentLocation();
	const FVector Chest = Agent->GetHitbox(EHitZone::Body)->GetComponentLocation();
	for (int32 i = 0; i < Bots.Num(); ++i)
	{
		AScriptedBot* Bot = Bots[i];
		if (!Bot || !Bot->IsAlive())
		{
			continue;
		}
		FVector Eye;
		FRotator View;
		Bot->GetActorEyesViewPoint(Eye, View);
		// Fair: the same view cone and geometry-only line of sight as the agent's (spec §4); only this arena's agent.
		const bool bSees = bAgentAlive && FPSPerception::InFieldOfView(Eye, View, Head, B.SightFovDeg)
			&& (FPSPerception::HasLineOfSight(World, Eye, Head) || FPSPerception::HasLineOfSight(World, Eye, Chest));
		FVector Target;
		if (Senses[i].Update(bSees, Agent->GetActorLocation(), Sounds, Bot->GetActorLocation(), Now, B, BotRngs[i], Target))
		{
			Brains[i].Investigate(ToLocal(Target), ToLocal(Bot->GetActorLocation()), Now, B);
		}
	}
}
```

  9. **The agent's hearing (D-024, D-031).** In `BuildObservation`, replace `Out.Sounds.Reset(); // hearing arrives ...` with:

```cpp
	Out.Sounds.Reset();
	const FBotBehaviourSettings& Bs = S.Bots;
	TArray<const FPSBots::FSoundEvent*> Heard;
	for (const FPSBots::FSoundEvent& E : Sounds)
	{
		if (E.Source != INDEX_NONE && FPSBots::CanHear(E, Agent->GetActorLocation(), Now, Bs.SoundMemorySeconds, Bs))
		{
			Heard.Add(&E);
		}
	}
	Heard.StableSort([](const FPSBots::FSoundEvent& A, const FPSBots::FSoundEvent& B) { return A.Time > B.Time; }); // newest first
	for (int32 i = 0; i < Heard.Num() && i < P.MaxSounds; ++i)
	{
		const FVector Delta = Heard[i]->Position - Eye;
		Out.Sounds.Add(FPSCombatObs::PackSound(FPSPerception::ToViewFrame(Delta, Yaw), static_cast<float>(Delta.Size()),
			static_cast<float>(Now - Heard[i]->Time), P, Bs.SoundMemorySeconds));
	}
```

- [ ] **Step 5: Build and run** `-Filter "FPSAgent.Combat.Arena"`. Expected: `Succeeded: 6  Failed: 0`. Then run the full suite.
  - **If "the patrol bot walks" fails:** check that `TickBots` runs before the dead-agent early return.
  - **If "high cover blocks sight" fails:** the pool block's collision isn't re-enabled after the move. Check `SetActorEnableCollision(true)` and the block's location; the floor top is z = 0 at the arena origin.

- [ ] **Step 6: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatArena.* Game/FPSAgent/Source/FPSAgent/Tests/CombatArenaTests.cpp
git commit -m "[local] UCombatArena S1: per-episode cover from a block pool, tile graph, bot brains + fair senses at 15 Hz, footstep/gunshot sounds, agent hearing"
git pull --rebase && git push
```

---

### Task 9: S0/S1 dispatch in the task actor and the game mode

**Files:**
- Modify: `Learning/CombatTaskActor.h`, `Learning/CombatTaskActor.cpp`, `Arena/ArenaGameMode.cpp`

**Interfaces:**
- Consumes: `UCombatArena::Build(..., StageIndex)`, `StartEpisode(FCombatEpisode)`, `FloorHalfSizeCm(int32)` (Task 8); `FPSCurriculum::SampleS1` (Task 6); `FCurriculumSettings::Stage(int32)` (Task 1).
- Produces:
  - `-Stage=S1` runs (training, `-Infer`, `-Eval`, `-Scripted`); `-Stage=S2`/`S3` still exit 2.
  - `static FBox ACombatTaskActor::ComputeBounds(int32 ArenaCount, const FVector& Origin, int32 StageIndex = 0)`.
  - Stage names in `Combat: ready. stage=S1 ...` and in the `CombatEval` JSON.

- [ ] **Step 1: Implement `Learning/CombatTaskActor.h`.**
  - Change `ComputeBounds` to `static FBox ComputeBounds(int32 ArenaCount, const FVector& Origin, int32 StageIndex = 0);`.
  - Add the private member `int32 StageIndex = 0;`.

- [ ] **Step 2: Implement `Learning/CombatTaskActor.cpp`.**
  1. **`ComputeBounds`.** Take `int32 StageIndex` and use `const float Half = UCombatArena::FloorHalfSizeCm(StageIndex);`.
  2. **`BeginPlay`, stage check.** Replace the stage check with:

```cpp
	StageIndex = AArenaGameMode::RequestedCombatStage();
	if (StageIndex < 0 || StageIndex > 1)
	{
		Exit(2, FString::Printf(TEXT("stage S%d is not in this build (build B has S0 and S1)"), StageIndex));
		return;
	}
	const FCombatStageSpec& Stage = S.Curriculum.Stage(StageIndex);
```

     Replace the local `const int32 StageIndex = ...` with the member, and use `Stage` below.
  3. **`BeginPlay`, arena spacing guard.** Use `UCombatArena::FloorHalfSizeCm(StageIndex)`.
  4. **`BeginPlay`, arena build.** Use `Arena->Build(GetWorld(), i, Centers[i], Args.Seed, StageIndex);`.
  5. **`BeginPlay`, step cap.** Use `Config.Trainer.MaxEpisodeStepNum = FMath::CeilToInt(Stage.TimeLimitSeconds / DecisionSeconds) + 2;`.
  6. **`BeginPlay`, the `Combat: ready.` line.**
     - Change `stage=S0` to `stage=S%d` with `StageIndex`.
     - Use `Stage.TimeLimitSeconds`, `Stage.BotsEasy` and `Stage.BotsHard`.
     - Add ` agentMoves=%d` with `Stage.bAgentCanMove ? 1 : 0` at the end.
  7. **`StartNextEpisode`.** Add the S1 branch first:

```cpp
	if (StageIndex == 1)
	{
		const UFPSAgentSettings& S = UFPSAgentSettings::Get();
		Arena->StartEpisode(FPSCurriculum::SampleS1(Rng, Difficulty[ArenaIndex].Level, /*bAllowReview=*/!bEval, S.Curriculum, S.Bots), Seed, Now());
		return;
	}
```

     The existing S0 lines follow unchanged. `Rng` and `Seed` are the existing locals; keep their order.
  8. **`FinishEval`.**
     - Use `const float Gate = UFPSAgentSettings::Get().Curriculum.Stage(StageIndex).PromotionClearRate;`.
     - Pass the stage name `FString::Printf(TEXT("S%d"), StageIndex)` to `EvalJson`.

- [ ] **Step 3: Implement `Arena/ArenaGameMode.cpp`.** In `BuildCombat`:
  - add `const int32 StageIndex = FMath::Clamp(RequestedCombatStage(), 0, 1);`;
  - pass it to `ACombatTaskActor::ComputeBounds(FFPSRunArgs::Get().Arenas, FVector::ZeroVector, StageIndex)`;
  - pass it to `UCombatArena::FloorHalfSizeCm(StageIndex)` for the camera.

- [ ] **Step 4: Build and run all tests.** Expected: `Failed: 0`. The headless S1 runs come in Task 12, once the scripted reference moves.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatTaskActor.* Game/FPSAgent/Source/FPSAgent/Arena/ArenaGameMode.cpp
git commit -m "[local] -Stage=S1 dispatch: S1 arenas and sampler, stage-specific time limit/gate/names; S2/S3 exit 2"
git pull --rebase && git push
```

---

### Task 10: `-ResumeFrom` checks the snapshot's network shape

**Files:**
- Modify: `Learning/TrainingSession.cpp`

**Interfaces:**
- Consumes: `ULearningAgentsNeuralNetwork::NeuralNetworkData` → `ULearningNeuralNetworkData::GetInputSize()`, `GetOutputSize()`, `GetCompatibilityHash()` (Learning module, `LearningNeuralNetwork.h`).
- Produces: a snapshot whose network doesn't match this build exits with code 2 and a clear log line. This fixes a final-review finding of build A. Before, Learning Agents' `LoadFromSnapshot` overwrote the sizes and hash without comparing them, so a mismatched snapshot "loaded" and then scored 0.

- [ ] **Step 1: Record the baseline.** Run:

```powershell
$ue = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "$PWD\Game\FPSAgent\FPSAgent.uproject"
$snap = "$PWD\Game\FPSAgent\Saved\Snapshots\run-S0-20261112-152039"   # promoted S0 snapshot (gh release download run-S0-20261112-152039 -D <folder>, check SHA256SUMS)
$log = "$env:TEMP\resume_mismatch.log"
$p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended -Stage=S0 -Arenas=1 -Infer -ResumeFrom=`"$snap`" -Set=Combat.Perception.RayYawCount=12 -abslog=`"$log`"" -PassThru -WindowStyle Hidden
if (-not $p.WaitForExit(60000)) { Stop-Process -Id $p.Id -Force; "(stopped)" }
Select-String $log -Pattern "Combat: |LogLearning: Error|snapshot" | Select-Object -Last 6
```

  Expected **before** the fix: no exit with code 2, and probably `LogLearning: Error` lines about encoder sizes. Note what you see.

- [ ] **Step 2: Implement.** In `Learning/TrainingSession.cpp`:
  - Add `#include "LearningNeuralNetwork.h"`.
  - In `LoadSnapshots`, keep the existing file-exists loop. Replace the load loop with:

```cpp
	// Learning Agents' LoadFromSnapshot overwrites the network's sizes and compatibility hash without comparing them, so a snapshot
	// from another build or network setting would "load" and then score 0. Compare before and after; a mismatch is a bad argument (exit 2).
	struct FShape { int32 In = 0; int32 Out = 0; int32 Hash = 0; bool bKnown = false; };
	auto ShapeOf = [](const ULearningAgentsNeuralNetwork* Network)
	{
		FShape S;
		if (Network && Network->NeuralNetworkData)
		{
			S.In = Network->NeuralNetworkData->GetInputSize();
			S.Out = Network->NeuralNetworkData->GetOutputSize();
			S.Hash = Network->NeuralNetworkData->GetCompatibilityHash();
			S.bKnown = S.In > 0 || S.Out > 0;
		}
		return S;
	};
	TArray<FShape> Before;
	for (const TPair<FString, ULearningAgentsNeuralNetwork*>& Item : Items)
	{
		Before.Add(ShapeOf(Item.Value));
	}
	for (int32 i = 0; i < Items.Num(); ++i)
	{
		FFilePath File;
		File.FilePath = FPaths::Combine(Dir, Items[i].Key);
		Items[i].Value->LoadNetworkFromSnapshot(File);
		const FShape After = ShapeOf(Items[i].Value);
		if (Before[i].bKnown && (After.In != Before[i].In || After.Out != Before[i].Out || After.Hash != Before[i].Hash))
		{
			UE_LOG(LogFPSAgent, Error, TEXT("%s: snapshot %s doesn't match this build's network (inputs %d vs %d, outputs %d vs %d, hash %d vs %d). Resume only snapshots from the same build and network settings."),
				*Config.LogPrefix, *File.FilePath, After.In, Before[i].In, After.Out, Before[i].Out, After.Hash, Before[i].Hash);
			return false;
		}
	}
```

  - In `Setup`, change the `-ResumeFrom` exit reason to `TEXT("-ResumeFrom snapshots are missing or don't match this build's network")`.

- [ ] **Step 3: Build and run all tests.** Expected: `Failed: 0`.

- [ ] **Step 4: Verify headless.** Re-run Step 1. Expected:
  - a `Combat: snapshot ...policy.bin doesn't match` line (or the encoder's), then `Combat: exiting (code 2): -ResumeFrom snapshots are missing or don't match this build's network`, and the process ends by itself;
  - on Windows the process exit code may read 0, so the log line is the check.

  Then run the same command **without** the `-Set` (60 s). Expected: `Combat: loaded 3 snapshots`, `CombatStats` lines and no `LogLearning: Error`. The matching snapshot still loads.
  - **If the matching snapshot is reported as a mismatch:** Learning Agents changes the hash on load even when the shape matches. Then compare only the input and output sizes, and report it.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/TrainingSession.cpp
git commit -m "[local] -ResumeFrom checks each snapshot's network sizes and compatibility hash; a mismatch exits 2 instead of loading silently"
git pull --rebase && git push
```

---

### Task 11: Gate-level clear rate (`clearTop`) in the stats

**Files:**
- Modify: `Learning/CombatStats.h`, `Learning/CombatStats.cpp`, `Learning/CombatTaskActor.cpp`
- Test: `Tests/CombatStatsTests.cpp`

**Interfaces:**
- Consumes: `FCurriculumSettings::GateLevel` (Task 1).
- Produces:
  - `FWindow` gains `TopEpisodes`, `TopClears`, `TopLevel` and `double ClearRateTop() const`. `Reset()` keeps `TopLevel`.
  - The log line gains ` clearTop=..%(n=..)` before ` terms:`.
  - The CSV gains the columns `clearRateTop,episodesTop` at the **end**, so the existing columns keep their positions for CLUSTER's parser.

- [ ] **Step 1: Write the failing test.** In `Tests/CombatStatsTests.cpp`, before `#endif`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatStatsTopTest, "FPSAgent.Combat.Stats.ClearTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatStatsTopTest::RunTest(const FString& Parameters)
{
	auto Rec = [](float Level, bool bCleared, bool bReview) { FEpisodeRecord R; R.Level = Level; R.bCleared = bCleared; R.bReview = bReview; return R; };
	FWindow W;
	W.TopLevel = 0.95f;
	W.Add(Rec(1.f, true, false));    // counts, cleared
	W.Add(Rec(0.96f, false, false)); // counts, not cleared
	W.Add(Rec(0.5f, true, false));   // below the gate level
	W.Add(Rec(1.f, true, true));     // review episode: never counts
	TestEqual(TEXT("gate-level episodes"), W.TopEpisodes, 2);
	TestEqual(TEXT("gate-level clear rate"), static_cast<float>(W.ClearRateTop()), 0.5f, 1e-6f);
	TestTrue(TEXT("log line has clearTop"), LogLine(W, 1.0, FLevelSummary(), 1.0).Contains(TEXT("clearTop=50.0%(n=2)")));
	TArray<FString> Header, Row;
	CsvHeader().ParseIntoArray(Header, TEXT(","), false);
	CsvRow(W, 1.0, FLevelSummary(), 1.0).ParseIntoArray(Row, TEXT(","), false);
	TestEqual(TEXT("csv columns still match"), Row.Num(), Header.Num());
	TestEqual(TEXT("new columns are last"), Header.Last(), FString(TEXT("episodesTop")));
	W.TopLevel = 0.9f;
	W.Reset();
	TestEqual(TEXT("reset clears the counts"), W.TopEpisodes, 0);
	TestEqual(TEXT("reset keeps the gate level"), W.TopLevel, 0.9f);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** Expected: `'TopLevel': is not a member of 'FPSCombatStats::FWindow'`.

- [ ] **Step 3: Implement.**
  - **`Learning/CombatStats.h`, `FWindow`.**
    - Add:

```cpp
		/** Non-review episodes at a level of at least TopLevel (the promotion trigger's gate-level clear rate, D-031). */
		int32 TopEpisodes = 0;
		int32 TopClears = 0;
		float TopLevel = 0.95f;
		double ClearRateTop() const;
```

    - Change `Reset()` to `void Reset() { const float Keep = TopLevel; *this = FWindow(); TopLevel = Keep; }`.
  - **`Learning/CombatStats.cpp`.**
    - In `FWindow::Add`, add:

```cpp
		if (!R.bReview && R.Level >= TopLevel - 1e-4f)
		{
			++TopEpisodes;
			TopClears += R.bCleared ? 1 : 0;
		}
```

    - Add `double FWindow::ClearRateTop() const { return Ratio(TopClears, TopEpisodes); }`.
    - In `LogLine`:
      - change `decisions/s(wall)=%.0f terms:` to `decisions/s(wall)=%.0f clearTop=%.1f%%(n=%d) terms:`;
      - add the arguments `100.0 * W.ClearRateTop(), W.TopEpisodes` after `DecisionsPerSecond`.
    - In `CsvHeader`, after the term loop, add `H += TEXT(",clearRateTop,episodesTop");`.
    - In `CsvRow`, after the term loop, add `R += FString::Printf(TEXT(",%.4f,%d"), W.ClearRateTop(), W.TopEpisodes);`.
  - **`Learning/CombatTaskActor.cpp`, `BeginPlay`.** Before the arenas are built, set `Window.TopLevel = S.Curriculum.GateLevel; EvalTotals.TopLevel = S.Curriculum.GateLevel;`.

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Combat.Stats"`. Expected: `Succeeded: 2  Failed: 0`. Then run the full suite.

- [ ] **Step 5: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatStats.* Game/FPSAgent/Source/FPSAgent/Learning/CombatTaskActor.cpp Game/FPSAgent/Source/FPSAgent/Tests/CombatStatsTests.cpp
git commit -m "[local] CombatStats clearTop: gate-level clear rate of non-review episodes (promotion trigger, L-007 note a); CSV columns appended last"
git pull --rebase && git push
```

---

### Task 12: The `-Scripted` S1 reference, and headless verification of build B

**Files:**
- Modify: `Learning/CombatArena.h`, `Learning/CombatArena.cpp`
- Test: `Tests/CombatArenaTests.cpp`

**Interfaces:**
- Consumes: `AgentBrain`, `Sounds` and `LastScriptedSoundTime` (Task 8); `FPSBots::CanHear` (Task 4).
- Produces: `FPSCombatObs::FAgentAction UCombatArena::ScriptedAction(float DecisionSeconds)`, **no longer const**. With an enemy in sight it uses the S0 logic (stand still, turn to the head, fire on target). Otherwise, when the stage lets the agent move, it explores and investigates enemy sounds (D-031, ruling 7).

- [ ] **Step 1: Write the failing test.** In `Tests/CombatArenaTests.cpp`, before `#endif`, add:

```cpp
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSCombatArenaScriptedS1Test, "FPSAgent.Combat.Arena.ScriptedS1",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)
bool FFPSCombatArenaScriptedS1Test::RunTest(const FString& Parameters)
{
	FPSTest::FTestWorld W;
	TStrongObjectPtr<UCombatArena> A(NewObject<UCombatArena>(GetTransientPackage()));
	A->Build(W.World, 0, FVector::ZeroVector, 7, /*StageIndex=*/1);
	FPSCurriculum::FCombatEpisode E;
	E.Map = A->GetBaseSpec();
	E.AgentYawDeg = 0.f;
	FPSCurriculum::FBotSpawn Behind;
	Behind.Offset = FVector2D(-1500.0, 0.0); // behind the agent: out of view
	Behind.YawDeg = 180.f;
	E.Bots = { Behind };
	A->StartEpisode(E, 3, 0.0);
	A->PrepareDecision(0.1);
	const FPSCombatObs::FAgentAction Explore = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("nothing in view: the reference explores (moves or turns)"), !Explore.Move.IsNearlyZero() || FMath::Abs(Explore.Aim.X) > 0.01);
	TestFalse(TEXT("nothing in view: no fire"), Explore.bFire);

	FPSCurriculum::FCombatEpisode F = E;
	F.Bots[0].Offset = FVector2D(1000.0, 0.0); // straight ahead
	A->StartEpisode(F, 4, 0.0);
	A->PrepareDecision(0.1);
	const FPSCombatObs::FAgentAction Shoot = A->ScriptedAction(4.f / 60.f);
	TestTrue(TEXT("enemy in view: stands still to shoot"), Shoot.Move.IsNearlyZero());
	TestTrue(TEXT("enemy in view: aims at it"), FMath::Abs(Shoot.Aim.X) < 0.05);
	return true;
}
```

- [ ] **Step 2: Build to verify it fails.** The first assertion fails, because the current reference scans in place without moving, or behaves only like S0.

- [ ] **Step 3: Implement.**
  - In `Learning/CombatArena.h`, change the declaration to `FPSCombatObs::FAgentAction ScriptedAction(float DecisionSeconds);` (drop `const`).
  - In `Learning/CombatArena.cpp`, drop `const` from the definition too.
  - Replace its final `return FPSCombatObs::ScriptedAction(...);` with:

```cpp
	FPSCombatObs::FAgentAction A = FPSCombatObs::ScriptedAction(Target, Fire.Ammo, S.Rifle.MagazineSize, Fire.IsReloading(), S.Body.HeadRadius, S.Combat.Perception, DecisionSeconds);
	if (!StageSpec().bAgentCanMove || Target.bHasTarget)
	{
		return A; // S0, or an enemy in sight: stand still, turn to the head, fire on target
	}
	// The agent may move and sees nobody: explore like a bot and investigate the newest enemy sound it hears (the same hearing as the policy).
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const FBotBehaviourSettings& B = S.Bots;
	const FVector2D Here = ToLocal(Agent->GetActorLocation());
	const FPSBots::FSoundEvent* Newest = nullptr;
	for (const FPSBots::FSoundEvent& E : Sounds)
	{
		if (E.Source != INDEX_NONE && E.Time > LastScriptedSoundTime && FPSBots::CanHear(E, Agent->GetActorLocation(), Now, B.SoundMemorySeconds, B)
			&& (!Newest || E.Time > Newest->Time))
		{
			Newest = &E;
		}
	}
	if (Newest)
	{
		LastScriptedSoundTime = Newest->Time;
		AgentBrain.Investigate(ToLocal(Newest->Position), Here, Now, B);
	}
	const FPSBots::FBotIntent Intent = AgentBrain.Update(Here, Now, B);
	const float MaxPerDecision = FMath::Max(1e-3f, S.Combat.Perception.MaxTurnRateDegPerSec * DecisionSeconds);
	const float PitchFix = FMath::Clamp(static_cast<float>(-FRotator::NormalizeAxis(View.Pitch)) / MaxPerDecision, -1.f, 1.f); // level the view
	if (Intent.MoveDir.IsNearlyZero())
	{
		A.Aim = FVector2D(0.5, PitchFix); // standing (paused or looking around): sweep the view
		return A;
	}
	const float MoveYaw = FMath::RadiansToDegrees(FMath::Atan2(Intent.MoveDir.Y, Intent.MoveDir.X));
	const float YawError = FRotator::NormalizeAxis(MoveYaw - View.Yaw);
	A.Aim = FVector2D(FMath::Clamp(YawError / MaxPerDecision, -1.f, 1.f), PitchFix);
	if (FMath::Abs(YawError) < 45.f) // walk once roughly facing the path, so the eyes lead
	{
		const FVector Local = FPSPerception::ToViewFrame(FVector(Intent.MoveDir.X, Intent.MoveDir.Y, 0.0), View.Yaw);
		A.Move = FVector2D(Local.X, Local.Y).GetClampedToMaxSize(1.0);
	}
	return A;
```

    `Target`, `Fire`, `S`, `Eye` and `View` are the function's existing locals. Keep their definitions.

- [ ] **Step 4: Build and run** `-Filter "FPSAgent.Combat.Arena"`. Expected: `Succeeded: 7  Failed: 0`. Then run the full suite, `Failed: 0`, and record the count.

- [ ] **Step 5: Headless verification of build B** (no training). Keep the real output; Task 13 publishes it.

```powershell
$ue = "C:\Program Files\Epic Games\UE_5.7\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$proj = "$PWD\Game\FPSAgent\FPSAgent.uproject"
$tmp = $env:TEMP
$snap = "$PWD\Game\FPSAgent\Saved\Snapshots\run-S0-20261112-152039"
function Run($name, $extra, $seconds) {
  $log = "$tmp\$name.log"
  $p = Start-Process $ue "`"$proj`" -game -nullrhi -nosound -unattended $extra -abslog=`"$log`"" -PassThru -WindowStyle Hidden
  if (-not $p.WaitForExit($seconds * 1000)) { Stop-Process -Id $p.Id -Force; "($name stopped after $seconds s)" } else { "($name exit=$($p.ExitCode))" }
  Select-String $log -Pattern "Combat: |CombatStats|CombatEval|LogLearning: Error|Error: |Warning: .*cover pool" | Select-Object -Last 6 | ForEach-Object { $_.Line.Substring([Math]::Max(0, $_.Line.IndexOf('Log'))) }
}
# (a) S1 scripted reference: 4 arenas, 150 s. Expect CombatStats lines with clear > 0, a moving level, clearTop logged, and no cover-pool warning.
Run "s1_scripted" "-Stage=S1 -Arenas=4 -Seed=1 -Scripted -StatsCsv=`"$tmp\s1_scripted.csv`"" 150
Get-Content "$tmp\s1_scripted.csv" -TotalCount 2
# (b) S1 scripted promotion test at level 1: exit 0, CombatEval with "stage":"S1"; the JSON file exists.
Run "s1_eval" "-Stage=S1 -Arenas=4 -Seed=1000 -Scripted -Eval -EvalEpisodes=40 -StartDifficulty=1 -EvalJson=`"$tmp\s1_eval.json`"" 600
Get-Content "$tmp\s1_eval.json"
# (c) The promoted S0 snapshot plays S1 (frozen layout): "loaded 3 snapshots", CombatStats lines, no LogLearning errors, no mismatch exit.
Run "s1_infer_s0snap" "-Stage=S1 -Arenas=2 -Seed=2 -Infer -ResumeFrom=`"$snap`"" 60
# (d) S0 regression: the scripted eval still passes.
Run "s0_eval" "-Stage=S0 -Arenas=4 -Seed=1000 -Scripted -Eval -EvalEpisodes=40 -StartDifficulty=1" 300
# (e) S2 is not in this build: "Combat: exiting (code 2): stage S2 is not in this build".
Run "s2_missing" "-Stage=S2 -Scripted" 60
Get-Process UnrealEditor* -ErrorAction SilentlyContinue | Stop-Process -Force
```

  - Expected: (a) clears happen and the level moves; (b) exit 0 with `"stage":"S1"`; (c) clean; (d) `"passed":true`; (e) the code-2 log line.
  - Note (a)'s and (b)'s clear rate, clear time, accuracy and decisions/s. These are the S1 reference numbers.
  - **If (a) never clears:**
    - Look at `acc=` and `kills/ep` first.
    - The reference must see the bots: check that `Sense` still runs against moving bots.
    - Check that bots walk at their speed scale, which needs the real game's movement ticks.
    - Fix the cause in the arena or the brain, not in the rewards.

- [ ] **Step 6: Commit**

```bash
git add Game/FPSAgent/Source/FPSAgent/Learning/CombatArena.* Game/FPSAgent/Source/FPSAgent/Tests/CombatArenaTests.cpp
git commit -m "[local] -Scripted S1 reference: stand and shoot when an enemy is in sight, otherwise explore and investigate heard enemy sounds"
git pull --rebase && git push
```

---

### Task 13: Document, package and ship build B (controller)
- [ ] **README:**
  - *Game command-line arguments*: add an `-Stage=S1` row (Movers: the agent moves, cover, moving bots, hearing, 30 s).
  - *Scripted bots*: behaviours, senses and hearing, as built.
  - *RL design*: the hearing row (D-031).
  - The `CombatStats` line with `clearTop`, and the promotion trigger (mean level ≥ 0.9 **and** `clearTop` ≥ gate, 10 windows).
- [ ] **Spec:** add "Build B (S1) implemented" to the status block and point to D-031.
- [ ] **Package and publish:** check AC power and that the editor is closed, then run `powershell -File tools/package_linux.ps1 -Publish`.
- [ ] **`docs/builds.md`:** add a row with the real numbers from Task 10 Step 4 and Task 12 Step 5.
- [ ] **`docs/STATUS.md`:** add L-009.
  - Starting snapshot: S1 training starts from the promoted L-008 snapshot (S0 with movement) if it exists, otherwise from `run-S0-20261112-152039`.
  - Re-measure Spike B on S1 (moving bots cost more).
  - Promotion trigger: mean level ≥ 0.9 and `clearTop` ≥ 70%.
  - Gate: `-Stage=S1 -Eval`, plus the `-Scripted` S1 reference.
  - Partition: not `gpuq` (user, 2026-11-13).

---

## Self-review (done while writing)
- **Spec coverage, build B row (spec §6):**

  | Item | Task |
  |---|---|
  | TileGraph + A* | 2 |
  | Floor generator with cover | 3 |
  | Strafe / Peek / Crouch / Patrol / Explore | 5 |
  | Investigate | 5 (brain), 8 (senses trigger it) |
  | Footsteps and hearing | 4 (rules), 8 (events and the agent's sounds) |
  | S1 knobs (cover 0→medium, 1→5 bots, behaviour mix, speed 60→100%, 30 s, aim helper 0.2) | 1, 6 |
  | Spawn out of sight | 6 (D-031) |
  | `-Scripted` S1 | 12 |
  | Per-arena senses and sounds (hard rule 3) | 8 |

- **Spec §6 tests:**

  | Test | Task |
  |---|---|
  | `TileGraph`: map01 strongly connected, ramps both ways, drops one-way, valid deterministic A* | 2 |
  | Floor generator: connected, density, deterministic | 3 |
  | `BotSenses`: view-cone edge (`InFieldOfView`, covered in build A), memory expiry, hearing radius, walking silent, reaction delay | 4 |
  | `EpisodeSampler`: deterministic, L = 0 / L = 1 ranges, out of sight | 6 |
  | World: a bot walks a path (pending movement input in the test world, real walking in the headless runs), cover blocks sight, arena reset | 8, 12 |

- **Carry-overs:**

  | Fix | Task |
  |---|---|
  | Gate-level trigger | 11 |
  | Snapshot shape check | 10 |
  | Crouch float | 7 (bots and the agent) |

- **Deferred, on purpose:**
  - Hunt (S3).
  - Bot shooting, reaction as a stage knob, damage taken (S2, build C).
  - Weighting toward named zones in Explore and Patrol: S1 floors have no zones, so it comes with the S3 pool maps.
- **Type consistency checked:**
  - Tiles are `FIntPoint(X = row, Y = column)` everywhere.
  - Positions are map-local `FVector2D` in the brain and sampler; the arena converts world to local with `ToLocal` (World − MapOrigin).
  - `FBotSpawn::Offset` and `FCombatEpisode::AgentOffset` are relative to the arena centre (= `Center` in world, `Map.Center()` in local).
  - `Investigate(Target, Pos, Now, B)` is called that way in the brain test, `SenseBots` and the scripted reference.
  - `Activate(..., SpeedScale)` is called that way in `StartEpisode`.
  - `ScriptedAction` is non-const in the header, the source and the tests.
