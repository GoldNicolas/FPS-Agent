# Combat training (S0–S3): design

- **Status: all sections agreed with the user, one by one (2026-11-12). Waiting for the user's review of the whole document.**
  - Implementation starts after that review, with the build A (S0) plan in `docs/superpowers/plans/`.
- **Side:** LOCAL (game). CLUSTER runs the training.
- **Research behind it:** [`docs/research/rl-reward-and-optimization.md`](../../research/rl-reward-and-optimization.md).
- **Builds on:** README → *RL design* (observations, actions, reward table, curriculum) and → *Scripted bots*. Spike A ([`docs/spikes/A-learning-agents-linux.md`](../../spikes/A-learning-agents-linux.md)) proved the pipeline. D-022 sets the episode-end rule.

## Goal
Agents that fight: find, aim at and kill scripted bots, under fair observations.
- Training gets longer and harder over time:
  - more bots;
  - bots that patrol, explore and hunt;
  - bots that shoot back;
  - bigger and procedurally varied maps.
- A clear bonus rewards killing all enemies quickly.
- Hard rules 1–8 still apply. In particular:
  - training is against scripted bots only;
  - observations come from line of sight only;
  - everything works under `-nullrhi`;
  - every value lives in settings.

Today's trained agents can't improve further: they only walk to targets and already hit the scripted ceiling (100% reached, +10.25). All further skill gains come from this combat environment.

## Decisions taken with the user (2026-11-12)
| Question | Decision |
|---|---|
| Scope | **The full ladder S0–S3, in one spec, built and shipped one stage at a time**, starting with S0 (aim gallery) so the cluster trains early. |
| Clear bonus | **Base plus speed:** on killing every bot in the episode, `+0.5 + 1.5 × (fraction of the time limit left)`. Max +2.0, about one bot's worth (+1 kill, about +1 damage); at least +0.5 for any clear. Paid once per episode (D-022). A setting. |
| Bot senses | **Fair senses**, the same rules as the agent: line of sight inside a field of view, a reaction delay, and hearing gunshots and footsteps within a radius. Bots search the last place they saw or heard the agent. |
| Curriculum | **Stages plus an automatic ramp.** S0–S3 are separate runs, each starting from the previous stage's best snapshot. Inside a run, difficulty rises while the agent clears more than about 70% of episodes and eases off below about 40%. |
| Fix pack first | C-004 / C-005 / C-003 shipped separately as `build-07eac52` (L-005). |

## 1. Architecture (agreed)
**What runs**
- New stages `-Stage=S0` to `S3`. Spike A stays as it is.
- A `CombatTaskActor` (`Learning/`) runs K arenas in one game process, like Spike A.
- Each arena holds **1 agent plus N bots**, built at its own position, so agents never meet (hard rule 3):

  | Stage | Arena |
  |---|---|
  | S0, S1 | open range floors, with optional cover |
  | S2 | small arenas with cover |
  | S3 | maps from the generated pool (`FPSArena::BuildMap` already takes an origin) |

- The trainer plumbing from Spike A (connect and watchdog, snapshot loading, exit codes, `-Batches`) moves out of `AReachTaskActor` into one small shared helper. Both tasks use it, so there's no copied trainer code.

**Pure modules, unit-tested without a world**

| Module | Job |
|---|---|
| `TileGraph` | Walkable tiles, ramps and drops from an `FMapSpec`, plus A* pathfinding. A C++ port of the Python model in `tools/mapgen`, checked on `map01`. |
| `BotSenses` | What a bot can see or hear: line of sight, field of view, reaction delay, hearing radius. |
| `RewardLedger` | Adds up each reward term separately per decision and per episode. |
| `DifficultyController` | The automatic ramp: per-arena level from the recent clear rate. |
| `EpisodeSampler` | A seeded episode setup (bot count, behaviours, spawns, map) from the stage and difficulty level. |

**Bots:** one `AScriptedBot` (`Bots/`) with switchable behaviours replaces `ARangeBot` in training. The range modes stay available for the play-test arena.

**Settings:** new `Combat`, `Bots` and `Curriculum` blocks in `UFPSAgentSettings` (D-012). Every value gets a source or `verify` note (hard rule 7).

**Thin RL layer:** the interactor and training environment call the task actor. Gameplay code doesn't know about Learning Agents (CLAUDE.md), so the trainer stack stays swappable.

**Fixed choices**
1. **One observation and action layout for all stages**, so each stage resumes from the previous stage's snapshot (resume = weights only, the same network shapes). In S0 the agent "can't move" because its movement output is ignored; the output itself is not removed.
2. **15 decisions per second:** a decision every 4th physics tick, with the action held in between. Physics stays at a fixed 60 Hz. Rewards are summed over the 4 ticks.
3. **Discount 0.995** (a setting; sweep 0.99–0.997). Look-ahead is about 13 s, so the clear bonus is visible (research §2). Recorded as **D-023**; Spike A keeps one decision per tick (D-019).
4. **Bigger network:** policy 2 × 256 plus memory 128, critic 2 × 256 (settings). Spike B checks the speed.
5. **Per-term logging by the game:**
   - each reward term and each metric is averaged per stats window in the log, and written to a CSV per process (`-StatsCsv=<path>`);
   - the trainer only logs the total reward;
   - sweeps compare terms.
6. **`-Eval` mode:**
   - plays a fixed seed set per stage with no training, using mean actions;
   - writes a JSON summary (clear rate, kills, deaths, damage, time to clear, accuracy, headshot %);
   - the promotion check between stages uses it.

## 2. Observations and actions (agreed 2026-11-12)
The same layout is used in every stage. Everything is in the agent's view frame (yaw of the aim); positions and velocities are scaled by settings.

**Observations** (one Learning Agents struct observation):

| Part | Contents | Learning Agents type |
|---|---|---|
| Self (about 16 numbers) | Velocity (view frame) · on ground, crouched, walking · health and armor (fractions) · ammo in magazine (fraction), reloading · current spread, shots in the current spray, time since last shot · aim pitch · **time left in the episode** (fraction) | struct of float / bool / velocity |
| Surroundings | **72 rays** from eye height: 24 directions × 3 pitches (down, level, up), range 30 m (settings). **They trace level geometry only and ignore all pawns**, so they never reveal an enemy. | lidar / static array |
| Enemies (set, up to 5) | Only enemies that pass a **line-of-sight trace to the head or chest AND are inside the 103° view cone**, or that were seen in the last **2 s** (shown as remembered, at the last-known position). Per enemy: position and distance · **aim error to the head** (yaw, pitch) · velocity · facing the agent · crouched · visible now or remembered, plus the age. | set observation (attention; order and count don't matter) |
| Hit indicator (D-024) | When damaged: the direction the damage came from (view frame), plus its age. Empty otherwise. | optional |
| Hearing (set, up to 4; D-024) | Recent gunshots and footsteps by enemies within a hearing radius, using the same rules and radii as the bots: direction (view frame), rough distance, age. | set observation |

**Actions** (15 decisions per second, each held for 4 physics ticks):

| Action | Type | Notes |
|---|---|---|
| Move | continuous, 2 values (forward, right) | Clamped to length 1. Ignored in S0. |
| Aim | continuous, 2 values (yaw, pitch change) | ±1 = the maximum turn per decision. **Turn-rate cap 720°/s** (a setting, `verify`). Spread evenly over the 4 ticks, never snapped. |
| Fire | bool | The trigger is held for the 4 ticks; the rifle's own cadence decides the shots. |
| Reload, crouch, walk, jump | bool each | Crouch, walk and jump are ignored in S0. |

**Human-likeness settings** (README → *Human-likeness*): reaction delay (observations delayed by N ms) and aim noise exist from the start, but they're **off in training**. They're raised later for in-game difficulty levels.

## 3. Reward (agreed 2026-11-12)
- All weights live in one `FCombatRewardSettings` (settings, hard rule 7). The aim-helper weight is per stage (§5).
- Each term is summed over the 4 ticks of a decision by the `RewardLedger` and logged on its own (§1, point 5).

| Term | Amount | Notes |
|---|---|---|
| Damage dealt | `+ damage / 150` | Health plus armor actually removed (capped at what the bot had). A headshot kill from full = +1.0; a body shot ≈ +0.27. No separate headshot bonus: the damage table already pays 4×. |
| Kill | `+1.0` | |
| First sighting | `+0.2` | Once per bot per episode: the first decision on which that bot passes the line-of-sight-in-FOV check. |
| Clear bonus | `+0.5 + 1.5 × (time left / time limit)` | Once, on the decision when the last bot dies (D-022). |
| Aim helper (potential-based) | `γ·Φ(s') − Φ(s)`, with `Φ = W × (1 − min(e / 30°, 1))` | `e` = aim error to the head of the nearest **visible** enemy (not remembered). `Φ = 0` when none is visible and at episode end. `W` per stage: 0.2 in S0 and S1, 0.1 in S2, 0 in S3. Potential-based, so it can't change the optimal policy (research §3). Φ is never negative, so spotting an enemy can't cost reward. |
| Damage taken | `− damage / 150` | From S2 on (shooting bots). |
| Death | `−1.0` | **Termination.** |
| Shot that hits no enemy | `−0.01` per bullet | A body hit pays about 27× more, so "never shoot" never wins. |
| Time | `−0.01` per second | Anti-camping. Mild, because the clear bonus carries the speed incentive. |

- **Episode endings:**
  - death → Termination;
  - all bots dead → Termination;
  - time limit → **Truncation**: the trainer bootstraps the value, so a timeout isn't treated as a failure (research §1).
- **Completion rule (D-022):** completion is computed from state (agent dead / bots alive / time used), never from flags set while computing rewards. All one-off terms are paid once.
- **Scale check:** a perfect 3-bot episode is about +8; an early death about −2, inside the trainer's comfortable 1–10 range.
  - The 5.7 trainer clips negative advantages to 0 (research §1), so every good behaviour has a positive term and the penalties stay small.
- **Metrics, not rewards** (logged per stats window and in `-Eval`): accuracy, headshot %, time to clear, kills and deaths, damage per life, shots per kill, first-sighting time, and each reward term's share of the return.

## 4. Scripted bots (agreed 2026-11-12)
- One `AScriptedBot` (an `ABeanCharacter`). Its behaviour, senses and skill come from a per-bot `FBotProfile`, sampled per episode from the stage and difficulty ranges (§5).
- Each bot has its own RNG, seeded from the episode seed plus the bot index (determinism rule).

**Navigation**
- **Every arena is an `FMapSpec`.** S0/S1 range floors are generated specs: an open floor of N×M tiles, plus cover tiles in S1. So there is one code path for building and for pathing.
- `TileGraph` builds the walkable graph once per map, using the same rules as `tools/mapgen`: ramps connect levels, and drops are one-way.
- A* gives tile paths. Bots walk tile centres, plus a small seeded offset, so routes vary.
- No Unreal navmesh: this works headless, stays deterministic and is cheap.

**Behaviours** (per-bot mix from the stage and difficulty ranges)

| Behaviour | Description |
|---|---|
| Static | Stands still (standing or crouched). |
| Strafe | ADAD on a random period (the existing `StrafeLogic`). |
| Peek | Steps out from cover and back on a random schedule. |
| Crouch | Crouch-spams. |
| Patrol | A loop through waypoints (zone tiles: sites, mid, plus random walkable tiles), with short pauses. |
| Explore | Goes to tiles not visited recently, weighted toward named zones: "walk around hoping to find something". |
| Investigate | Goes to the last seen or heard agent position, looks around for a few seconds, then returns to its base behaviour. |
| Hunt | Explore + Investigate + briefly holds an angle at corners. S3. |

**Senses (fair; the same rules as the agent, D-024)**
- **Sight:** a 103° view cone, line of sight to the agent's head or chest, checked about 15 times per second (staggered across bots).
- **Reaction delay:** the bot acts on a new sighting only after it. While it waits, nothing happens.
- **Memory:** the last-seen position is kept for about 3 s.
- **Hearing:**
  - gunshots within about 40 m;
  - **running** footsteps within about 15 m;
  - walking and crouching are silent.
  - A heard event gives a position with seeded noise and starts Investigate.
- **Per arena only:** senses consider only the bot's own arena's agent, enforced by arena id, not by distance (hard rule 3).

**Shooting** (S2 onward)
- After the reaction delay, the bot turns toward the agent at its **tracking speed**.
- It fires **bursts** (a length range) with an **aim error** (degrees, seeded).
- If its profile says so, it stops moving before shooting (counter-strafe).
- It uses the same rifle rules and damage table as the agent.

**Difficulty knobs (`FBotProfile`)**
- behaviour mix;
- movement speed;
- reaction time;
- aim error;
- tracking speed;
- burst length;
- whether it stops to shoot;
- health;
- hearing radius;
- strafe and peek frequency.

**Life cycle:**
- In training, a dead bot stays dead until the episode ends.
- The play-test range keeps `ARangeBot` with respawning.
- Bots never damage each other.

## 5. Curriculum and episodes (agreed 2026-11-12)
**Stage ladder.**
- Each stage is a separate run, starting from the previous stage's promoted snapshot.
- Every knob has an *easy* value (difficulty 0) and a *hard* value (difficulty 1) in a per-stage `FStageSpec` (settings).
- At level L, the sampler draws around `lerp(easy, hard, L)` with a ±10% jitter of the range.

| Stage | Arena | Bots (easy → hard) | Bots shoot | Time limit | Aim helper W |
|---|---|---|---|---|---|
| **S0 Aim gallery** (agent can't move) | Generated open floor | 1 → 5 **static** (standing → some crouched); 5–15 m → 5–35 m; within ±45° of the agent's facing → all around | no | 20 s | 0.2 |
| **S1 Movers** | Generated open floor, cover density 0 → medium | 1 → 5; strafe / peek / crouch → + patrol / explore; speed 60% → 100% | no | 30 s | 0.2 |
| **S2 Duels** | Small generated cover arenas | 1 → 4; reaction 600 → 200 ms; aim error 6° → 1.5°; tracking 90 → 360 °/s | yes | 45 s | 0.1 |
| **S3 Hunt** | The 50-map pool (`pool.json`) | 2 → 5; patrol / explore / investigate / **hunt**; S2's hard skill values | yes | 90 s | 0 |

**Automatic ramp** (`DifficultyController`, per arena)
- **Level:** L ∈ [0, 1], starting at `-StartDifficulty=` (default 0), so chained jobs continue where they stopped.
- **Rolling window:** the last 20 counted episodes.
  - Clear rate > 70% → L += 0.05.
  - Clear rate < 40% → L −= 0.05.
  - The window resets after each change.
  - **Clear** = all bots dead with the agent alive. A timeout or a death is not a clear.
- **Review episodes (anti-forgetting):** 20% of episodes are sampled at a uniform random level in [0, L]. They are excluded from the window.
- **Logging:** the mean, min and max L across arenas, in every stats line and the CSV.

**Promotion**
- **Trigger:** the mean L stays ≥ 0.9 over the last 10 stats windows. The run report says so.
- **Gate:** CLUSTER runs `-Eval -Stage=Sn -StartDifficulty=1`: 200 fixed-seed episodes, no training, mean actions.
- **Pass:** clear rate ≥ 70% (a per-stage setting). The snapshot is then published as `run-<id>` and the next stage starts from it at L = 0.

**Episodes**
- **Spawning:**
  - The agent spawns facing a random direction.
  - S1 and later: on a tile that no bot can see at spawn time (tile-grid line-of-sight check).
  - S0: the agent stands at the arena centre with the bots placed around it.
- **Endings:** death or all bots dead → Termination; time limit → Truncation (§3).
  - `MaxEpisodeStepNum` = time limit × 15.
- **Independence:** each arena resets on its own.
- **S3 maps:** each arena gets one pool map for the life of the game process (picked by seed and arena index). Arenas and processes differ, and chained jobs re-seed. That avoids rebuilding geometry every episode.
- **Isolation:** arena spacing ≥ the largest map plus 50 m. Senses and hearing are filtered by arena id anyway (hard rule 3).

## 6. Training operations, testing, documentation, build order (agreed 2026-11-12)
**New command-line arguments** (parsed in `FFPSRunArgs`, logged at startup, documented in README → *Game command-line arguments*):

| Argument | Meaning |
|---|---|
| `-Stage=S0..S3` | The combat stage. `SpikeA` stays as it is. |
| `-Batches=N` | Run length in experience batches (exists since `build-07eac52`). |
| `-StartDifficulty=X` | Initial ramp level for every arena (0–1). Chained jobs pass the last logged mean level. |
| `-Eval` | Promotion test: `-EvalEpisodes=N` (default 200) fixed-seed episodes spread over the arenas, at `-StartDifficulty`, with mean actions, no training and no ramp. Writes `-EvalJson=<path>` and exits 0. Needs `-ResumeFrom`, unless `-Scripted` (the reference score). |
| `-StatsCsv=<path>` | Writes one CSV row per stats window: every reward term, every metric, and the ramp level (mean, min, max). |
| `-Set=<path>=<value>;...` | Overrides any `UFPSAgentSettings` field via reflection, e.g. `-Set=Combat.Reward.Kill=1.5;Combat.Ppo.Discount=0.997`. Overrides are logged at startup; an unknown path exits with code 2. **PPO knobs live in settings too** (`Combat.Ppo`: discount, GAE λ, entropy weight, learning rates, network sizes), so sweeps need no new build. |
| `-Scripted` | A reference agent instead of the policy, giving an environment check and a reference score per stage. S0: it turns toward the nearest visible enemy's head at the turn-rate cap and fires when the aim error is below the first-shot spread. S1–S3: it moves with the bots' Hunt logic. It uses the same senses as the policy (no extra information). |

**Long runs and sweeps** (CLUSTER-owned scripts; LOCAL files requests in STATUS.md):
- **Chained jobs:** a training job runs a fixed number of batches. The next job (`--dependency=afterany`) passes `-ResumeFrom=<previous snapshots>` and `-StartDifficulty=<last mean level>`.
- **Sweeps:** job arrays over `-Set=` values. They're compared on `-Eval` results, never on training curves alone.
- **Reporting:** each stage run gets a run report; a promoted snapshot is published as `run-<id>`.
- **Before S0:** re-measure throughput (Spike B) on the combat build (bots plus traces cost more than Spike A).

**Testing (test-first)**
- **Pure modules, no world:**
  - `TileGraph`: `map01` strongly connected; ramps both ways; drops one-way; A* paths valid and deterministic.
  - Floor generator: connected, cover density honoured, deterministic per seed.
  - `BotSenses`, with injected visibility: FOV edge; blocked line of sight; memory expiry; hearing radius; walking is silent; reaction delay.
  - `RewardLedger`: each term; once-only clear bonus and first sighting; the aim helper's potential telescopes and is never negative; completion-before-reward call order (D-022).
  - `DifficultyController`: up/down steps; window reset; review episodes excluded.
  - `EpisodeSampler`: deterministic; the ranges hold at L = 0 and L = 1; spawn out of sight.
  - `FFPSRunArgs`: the new arguments; `-Set` parsing and errors.
- **World tests** (headless automation):
  - rays ignore pawns;
  - the enemy set excludes a bot behind a wall or outside the FOV;
  - a bot walks a path on `map01`;
  - a shooting bot damages the agent;
  - an arena resets cleanly.
- **Before every build:** a headless `-Scripted` run per available stage, with real numbers in `docs/builds.md`.

**Documentation (before and after every step):**
- This spec is updated whenever a decision changes.
- README → *RL design*, *Scripted bots* and *Game command-line arguments*, plus the stats log line formats.
- `decisions.md` entries.
- Per build: a `docs/builds.md` row with what to test, and a STATUS.md request.
- Each build's implementation plan goes in `docs/superpowers/plans/` before coding starts.

**Arena generation for S1 and S2:**
- One C++ floor generator produces an `FMapSpec` from rows, cols, cover density and the low/high cover ratio, using the arena seed.
- It is validated by `TileGraph` connectivity and regenerated if invalid.
- S1 uses large floors with sparse cover; S2 uses small floors with dense cover.

**Edge cases:**
- **Agent dies on the same decision the last bot dies:** it counts as a death. Kill and damage rewards are still paid; there is no clear bonus.
- **No visible enemy:** the aim helper potential is 0. The enemy set and the hearing set may be empty (S0 always has empty hearing and no hit indicator).

**Build order** (each build is trainable on the cluster when it ships, and each has its own implementation plan):

| Build | Contents | CLUSTER then |
|---|---|---|
| **A → S0** | Shared training-session helper (from Spike A); `Combat` / `Bots` / `Curriculum` settings; generated open floors; `CombatTaskActor`; the full §2 layout (hearing and hit indicator included, empty in S0); `RewardLedger` (all §3 terms); `DifficultyController` + `EpisodeSampler` (S0 knobs); `AScriptedBot` Static; `-Scripted`, `-Eval`, `-Set`, `-StatsCsv`, `-StartDifficulty`; tests | Smoke test, Spike B on the combat build, S0 training to promotion |
| **B → S1** | `TileGraph` + A*; floor generator with cover; Strafe / Peek / Crouch / Patrol / Explore / Investigate; footsteps and hearing | S1 from the S0 snapshot |
| **C → S2** | Bot shooting (reaction, tracking, aim error, bursts, counter-strafe); small dense arenas; damage taken | S2 |
| **D → S3** | Hunt; one pool map per arena; S3 ranges | S3. The deathmatch evaluation gets its own spec later. |
