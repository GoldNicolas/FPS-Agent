# Combat training (S0–S3): design

- **Status: draft, being agreed section by section with the user.**
  - Agreed: the decisions below and §1 (2026-11-12).
  - Not agreed yet: §2–§6. They are listed so the scope is visible, and they'll be filled in as they're agreed.
  - Implementation starts only after the whole spec is reviewed.
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

## 2. Observations and actions (not agreed yet)
To cover: self state, the ray-cast fan, the enemy set with line-of-sight, field-of-view and memory rules, the action layout (move, aim with a capped turn rate, fire, reload, crouch, walk), and human-likeness settings.

## 3. Reward (not agreed yet)
To cover: every term with its starting weight, the clear bonus as above, termination versus truncation, and logging.

## 4. Scripted bots (not agreed yet)
To cover: the behaviours (static, strafe, peek, crouch, patrol, explore, investigate, hunt, shoot), paths on the tile graph, fair senses, aim and reaction model, and difficulty parameters.

## 5. Curriculum and episodes (not agreed yet)
To cover:
- per-stage arena, bot and difficulty ranges;
- the automatic ramp controller;
- mixing in easier episodes to avoid forgetting;
- episode end conditions and time limits;
- promotion gates on `-Eval`.

## 6. Training operations, testing, documentation, phasing (not agreed yet)
To cover:
- run lengths in batches, chained jobs and resume;
- sweeps;
- the cluster requests;
- tests per module;
- which docs change;
- the build order S0 → S3.
