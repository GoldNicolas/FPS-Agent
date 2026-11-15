# Proposal: reward terms for tracking, finding, path-based search and smooth movement

- Status: **proposal**, requested by the user on 2026-11-15 and written by CLUSTER. Tracked as **C-008** in [`STATUS.md`](../../STATUS.md).
- Owner of the implementation: LOCAL (`Game/`), plus a new Linux build. CLUSTER can't build the game.
- Network: **unchanged.** Only reward terms change, so training can resume from `run-S1-walk-20261114-152929` (S1 walk, 73.0% gate).
- All values below are starting guesses. Each needs a `verify` note (hard rule 7) and can be changed with `-Set=Combat.Reward.*`.

## Why
The user wants the game's agent to show:
1. good tracking;
2. good finding;
3. purposeful seeking, with more accurate movement while it seeks;
4. a search pattern derived from the agent's predicted straight-line path. It should be dynamic and segmented, not an obvious circle or a fixed look-around;
5. efficient, smooth movement rewarded **more than speed**.

Measured problem: in the latest S1 walk gate ([`runs/S1-walk-20261114-154575.md`](../../runs/S1-walk-20261114-154575.md)), **29.5% of episodes time out**, mostly because the agent hasn't found the last bots in 30 s. The scripted reference clears in 9.7 s; the policy takes 14.0 s.

Today's reward has no term for any of these behaviours:
- the aim helper is potential-based, so its sum over an episode is about 0;
- FirstSighting is a flat +0.2 per bot;
- speed is pushed only by `ClearSpeed` and `TimePerSecond`.

## Agent state: Engaged or Searching
- **Engaged:** at least one enemy is visible, or was seen within `SightMemorySeconds` (3 s).
- **Searching:** otherwise.

Each new term is active in one state only, as noted below.

## New terms
Notation:
- `Δt` = 1/15 s (one decision);
- `e` = angular aim error to the nearest visible enemy, in degrees;
- `m_t` and `a_t` = the move and aim action vectors of decision t.

### 1. Tracking (Engaged)
`r_track = TrackWeight · min(1, ω_e / TrackOmegaRef) · exp(−(e / TrackSigma)²) · Δt`
- `ω_e` is the enemy's angular speed across the agent's view, so tracking a moving target pays and a static one doesn't.
- **Budget:** at most `TrackBudgetPerBot` per bot per episode. Once the budget is used up, killing the bot is the only remaining payoff, so the agent can't farm the term by tracking without shooting.
- Starting values: `TrackWeight` 0.3/s, `TrackSigma` 3°, `TrackOmegaRef` 20°/s, `TrackBudgetPerBot` 0.15.

### 2. Pre-aim on first sighting (finding)
`r_preaim = PreAimWeight · exp(−(e₀ / PreAimSigma)²)`, paid once per bot, on the decision it is first sighted.
- `e₀` is the aim error at that moment.
- This rewards already looking where the enemy appears (crosshair placement). It adds to FirstSighting, which stays at 0.2.
- Starting values: `PreAimWeight` 0.1, `PreAimSigma` 10°.

### 3. Search along the predicted path (Searching)
This is the user's "linear path → search area" idea.

**Predicted path:** `p̂(τ) = p₀ + v·τ`, for τ in [0, `SearchHorizon`].
- `p₀` is the agent's position and `v` its horizontal velocity.
- If `|v|` is below about 50 cm/s, use the move input's direction at walk speed instead.
- Clip the path at the first blocked tile.

**Search area A_t:** the arena tiles that have line of sight to some point of `p̂`. These are the tiles an enemy could shoot from at the places the agent is about to reach.

**Per tile `c`:**
- **Urgency** `u_c = exp(−τ_c / SearchUrgency)`, where `τ_c` is the earliest τ at which `c` sees `p̂(τ)`.
- **Staleness** `s_c = 1 − exp(−(t − t_c) / SearchStale)`, where `t_c` is the last time the tile was inside the agent's FOV with line of sight from its eye. Every tile starts fully stale.
- **Prior weight** `π_c`:
  - 1 by default;
  - × `SearchEdgeWeight` for tiles next to a blocked tile (corners and cover edges, where bots peek and hide);
  - × `SearchSoundWeight` inside the cone of a heard sound that is still in memory;
  - × `SearchLastSeenWeight` within 2 tiles of an enemy's last-seen position that is still in memory.

**Reward:**
```
r_search = SearchWeight · Σ_{c ∈ A_t, newly viewed this decision} π_c·u_c·s_c  /  Σ_{c ∈ A_t} π_c·u_c
```
- It is capped at `SearchBudget` per episode.
- "Newly viewed" means the tile entered the FOV with line of sight during this decision. After the reward, set `t_c = t`.

**Why it gives a dynamic, segmented pattern rather than a circle:**
- A 360° spin re-views tiles that are already fresh (`s ≈ 0`) or far from the path (`u ≈ 0`), so it earns almost nothing and loses time.
- The best order is "clear the angle that exposes you soonest, then the next". That order depends on the arena's geometry and the route, so the look pattern changes with every path.
- Tiles go stale again after about `SearchStale`, so the agent re-checks them without repeating a fixed loop.

**Optional variety:**
- randomize `SearchUrgency`, `SearchStale` and `SearchEdgeWeight` per episode within ±30% (`SearchStyleJitter`);
- CLUSTER can also sweep `Combat.Ppo.EntropyWeight` at about 0.002.

**Fairness (hard rule 4):**
- The reward isn't an observation, but this term still uses only map geometry plus what the agent saw or heard, never the bots' true positions.
- The search it learns is therefore one a fair player could do.

**Starting values:**

| Setting | Value |
|---|---:|
| `SearchWeight` | 0.05 per decision |
| `SearchBudget` | 1.0 |
| `SearchHorizon` | 1.5 s |
| `SearchUrgency` | 0.7 s |
| `SearchStale` | 4 s |
| `SearchEdgeWeight` | 2 |
| `SearchSoundWeight` | 3 |
| `SearchLastSeenWeight` | 3 |
| `SearchStyleJitter` | 0 (off) |

### 4. Smooth movement and aim (both states; ×`EngagedSmoothScale` when Engaged)
```
r_smooth = −SmoothMoveWeight·‖m_t − m_{t−1}‖²
           −SmoothAimWeight·‖a_t − a_{t−1}‖²
           −SpinWeight·max(0, (|yaw rate| − SpinFree) / SpinFree)²·Δt
```
- The terms penalize move jitter, aim flicks and fast spins.
- `EngagedSmoothScale` (0.25) keeps counter-strafing and target switching cheap in a fight.
- Starting values: `SmoothMoveWeight` 0.0005, `SmoothAimWeight` 0.0005, `SpinWeight` 0.1/s, `SpinFree` 180°/s, `EngagedSmoothScale` 0.25.

### 5. Path efficiency (Searching)
`r_eff = EfficiencyWeight · (η − EfficiencyTarget) · Δt`
- `η` = net displacement ÷ path length over the last `EfficiencyWindow` seconds; 0 when the agent stands still.
- This rewards straight, purposeful routes and penalizes wiggling and backtracking, independent of speed.
- **Wall push:** a separate `−WallPushWeight` per decision when the move input points into a wall and the agent's speed is under 20% of walk speed.
- Starting values: `EfficiencyWeight` 0.05/s, `EfficiencyTarget` 0.7, `EfficiencyWindow` 2 s, `WallPushWeight` 0.002.

### 6. Less weight on speed (existing settings)
| Setting | Now | Proposed |
|---|---:|---:|
| `ClearSpeed` | 1.5 | 0.5 |
| `ClearBase` | 0.5 | 1.0 |
| `TimePerSecond` | −0.01 | −0.005 |

LOCAL can change the build defaults, or CLUSTER can pass these with `-Set`.

## Scale
- All new positive terms together reach at most about 2.25 per episode: tracking 5 × 0.15, pre-aim 5 × 0.1, search 1.0.
- A typical episode should earn about 1 from them.
- Kills and the clear bonus give about 10.4. The new terms shape behaviour but never pay more than killing does.
- A fully jittery agent loses about 0.5 per 30 s to smoothness.

## Implementation notes (LOCAL)
**Code changes:**
- New `ETerm` entries in `Learning/CombatReward.h`: `Track`, `PreAim`, `Search`, `Smooth`, `Efficiency`, `WallPush`.
- New fields in `FCombatRewardSettings`, each with a `verify` note.
- The per-tile state (`t_c`, prior) lives per arena and agent, and is reset in `BeginEpisode`.

**Line of sight on the tile grid:** use the arena's tile occupancy grid with a grid ray walk (Bresenham or DDA), not physics traces.
- Work estimate: about 400 tiles × 128 arenas × 15 Hz, so physics traces would be too slow.
- Cover is fixed within an episode, so tile-to-path visibility can be cached once per episode.

**Throughput:** S1 runs at about 1,300–1,350 decisions/s per game at 4 × 32 on `gpul40q`.
- Please report the local cost.
- CLUSTER will measure it in the smoke test.

**Stats:** add the new terms to the `CombatStats` term breakdown, plus these metrics in the stats line and the eval JSON:
- seconds to first sighting;
- mean `e₀` at sighting;
- mean tracking error on moving targets;
- search coverage (the fraction of `A` viewed);
- mean `η`;
- move and aim jitter.

**Tests**, in the style of `CombatRewardTests`:
- tracking budget cap;
- pre-aim paid once per bot;
- search pays 0 for re-viewed fresh tiles and pays again after `SearchStale`;
- a spin earns less than a path-ordered sweep in a fixed test arena;
- smoothness scaled when Engaged;
- `η` in [0, 1].

## Run plan (CLUSTER, once the build ships)
1. **Smoke test:**
   - the new terms appear in `CombatStats`;
   - exit codes still pass the smoke checks;
   - an S1 walk snapshot loads (same network);
   - decisions/s is measured.
2. **Train:** `S1-walk-<date>-<jobid>` from `run-S1-walk-20261114-152929` (`-StartDifficulty=1`, `MOBILITY=walk`), 1,000 batches, 100-window trigger, `gpul40q`.
3. **Gate:** 200 episodes, seed 1000, 32 arenas, walk. Compare with 73.0%, and report:
   - the timeout share;
   - clear time;
   - the new behaviour metrics (above).
4. **Optional control:** a weights-only run with only section 6. It separates the effect of "less speed" from the new terms.
