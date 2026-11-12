# Spike A: packaged Learning Agents game trains on Cradle against an external trainer
- Date / side: 2026-11-11, LOCAL (build). The CLUSTER side runs it and fills in **Cluster result** below.
- Build: **`build-cccd809`** (so `<sha>` below = `cccd809`) · Slurm job(s): 151703 (smoke), 151704 (run 1), 151705 (run 1b), 151707 (resume), 151708–151710 (playback), all on `gpu001` (`gpuq`)
- Question: does a packaged UE 5.7 Linux build run headless on Cradle (`-nullrhi`), connect to an external `train.py`, learn a toy task, save snapshots and resume from them?
- Pass criterion (README → Roadmap): reward goes up on a toy task; snapshots save; training resumes from them.

## The toy task (`-Stage=SpikeA`)
- **Setup:**
  - `-Arenas=K` walled 20 m × 20 m floors, 40 m apart, so agents never meet (hard rule 3).
  - One `ABeanCharacter` per arena, driven by an AI controller and real `CharacterMovement`.
- **Episode:**
  - Seeded start and target, 3–12 m apart.
  - It ends on reaching the target (2D distance ≤ 1 m), or is truncated after 360 steps (6 s at 60 Hz).
- **Observation:** target position relative to the agent (in units of 10 m), plus the agent's velocity (in units of 6 m/s). Nothing else; no other agents.
- **Action:** 2D move input (world X/Y), clamped to unit length.
- **Reward:**
  - +1 per meter closed in a step.
  - −0.5 per second.
  - +5 on reaching the target.
- **Tuning:** the values are toy values.
  - They are the **C++ defaults** of `FReachTaskSettings` in `Game/FPSAgent/Source/FPSAgent/Common/FPSAgentTypes.h`, reached through `UFPSAgentSettings::SpikeA`.
  - `DefaultGame.ini` may override them under `[/Script/FPSAgent.FPSAgentSettings]` with `SpikeA=(...)`, but it holds no overrides today (C-002).
- **PPO:** Learning Agents defaults (policy 1×128 + GRU 64; LR 1e-4/1e-3; batch 1024/4096; 10 000 steps per iteration), seed = `-Seed`.
- **The game asks the trainer for:**
  - snapshots every 10 iterations,
  - TensorBoard logs,
  - the GPU (the trainer falls back to CPU if CUDA is missing).

### Reference numbers (LOCAL, Windows editor binary `-game -nullrhi`, 4 arenas, no trainer)
| Mode | reached | avgReturn per episode | agent-steps/s (wall) |
|---|---|---|---|
| `-Infer` with the untrained policy | 0.0% | −3.2 (6 s timeout, barely moves) | ~6 900 |
| `-Scripted` (straight to target: the upper bound) | 100.0% | +9.7 to +10.1 | ~8 700 |

So, judged by the trainer's `Avg Rew Sum` line or the game's `avgReturn`, a learning run should climb from about −3 toward about +10.

### LOCAL checks done (no training: hard rule 6)
- 31/31 automation tests pass, including the new `FPSAgent.Learning.Reach.*` and `FPSAgent.Settings.RunArgs`.
- **Re-run on `a995846`, 2026-11-11:** `tools/run_tests.ps1` gives `Succeeded: 31  Failed: 0  NotRun: 0`, and the mapgen pytest gives `18 passed`.
  - `git diff cccd809 a995846 -- Game/ tools/ cluster/` is empty, so this is the same game code as `build-cccd809`.
  - Cradle can't run these tests (no Unreal Engine there), so this is the confirmation.
- Playback (`-Infer`), scripted and no-trainer runs headless, with the real output above.
- **No trainer reachable** (`-LearningAgentsPort=1 -LearningAgentsTimeout=5`): the game exits with **code 1 after 26 s** and logs `SpikeA: exiting (code 1): no trainer connection after 20 s (wall clock)`.

## Engine findings that matter for the cluster
1. **Start order.** Start the trainer first. Launch the games only once `trainer.log` shows `Server listening on 127.0.0.1:<port>` (needs `-l`).
2. **Never probe the trainer port** (`nc -z`, `/dev/tcp`, curl). `socket_communicator._listener` accepts a connection, then loops in `socket_receive_all` until the config arrives. On a closed socket that loop spins forever while holding the lock, so the trainer never initializes. Wait for the log line instead.
3. **The engine's connect timeout isn't wall-clock.** `SocketTraining::WaitForConnection` adds only its 1 ms sleeps to the timeout counter, not the time spent inside each blocking `connect()`:
   - on Windows (about 2 s per refused connect) it never ends;
   - on Linux a refused connect returns at once, so it roughly holds;
   - behind a firewall (dropped SYN) each attempt can take minutes.

   Our game adds a wall-clock watchdog: `-LearningAgentsTimeout` + 15 s, then exit code 1.
4. **The trainer takes its settings from the game.** PPO settings, device and snapshot interval are sent by the first game that connects. `train.py`'s `--batch-size` (default 1) is the number of experience buffers to collect before each update. In Socket mode `-n` is parsed but unused.
5. **Where the trainer writes (`train.py ... Socket <addr> $RUN/trainer`):**
   - `$RUN/trainer/FPSAgent0/Snapshots/{policy,critic,encoder,decoder}.bin`, **overwritten** every 10 iterations, so copy them aside to keep history;
   - `$RUN/trainer/FPSAgent0/Configs/*.json`;
   - `$RUN/trainer/TensorBoard/runs/...`.
6. **Python packages** for the socket PPO path: `numpy`, `torch`, **`smart_open`** (imported at module load, so the trainer won't start without it; the engine pins 7.3.0), `tensorboard` (optional). Not needed: `boto3`, `mlflow`.
7. **Expect slow startup.** glibc 2.28 makes engine startup slower (`dlopen`), a one-off cost per process. Use `-LearningAgentsTimeout=60` or more.

## Suggested CLUSTER procedure
This is a sketch. The CLUSTER side owns the real script, [`cluster/slurm/spikeA.sbatch`](../../cluster/slurm/spikeA.sbatch), and that's the one to run.

`-Batches=` needs the C-004 fix build or newer (`docs/builds.md`). With `build-cccd809`, use `-Iterations=` with batches × 32 (C-005).

```bash
# Paths per D-021 / docs/cradle.md -> "Our paths": everything under ~/beanRant
# 0. fetch + verify the build (tag in docs/builds.md)
gh release download build-<sha> -D ~/beanRant/builds/<sha> && cd ~/beanRant/builds/<sha>
sha256sum -c SHA256SUMS && tar xzf FPSAgent-Linux-<sha>.tar.gz && cat Linux/BUILD_INFO.txt

# 1. on a compute node (srun/sbatch, ~15 min, 1 GPU or CPU-only, ~8 cores)
BUILD=~/beanRant/builds/<sha>/Linux; REPO=~/beanRant/FPS-Agent
RUN=~/beanRant/runs/spikeA-$(date +%Y%m%d)-$SLURM_JOB_ID; mkdir -p "$RUN"   # run ID per docs/runs/README.md
PORT=$((40000 + SLURM_JOB_ID % 20000))
# Call la-train's python by full path: on this account `activate` still runs another env's python (C-003, docs/cradle.md)
unset PYTHONPATH PYTHONHOME VIRTUAL_ENV; export PYTHONNOUSERSITE=1
PY=~/beanRant/envs/la-train/bin/python
"$PY" -s -u $REPO/cluster/learning_agents_python/train.py FPSAgent -l \
  --nne-cpu-path $REPO/cluster/learning_agents_python/nne_runtime_basic_cpu \
  Socket 127.0.0.1:$PORT "$RUN/trainer" > "$RUN/trainer.log" 2>&1 &
until grep -q "Server listening on" "$RUN/trainer.log"; do sleep 1; done   # do NOT probe the port
for i in 0 1; do
  "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended -Stage=SpikeA -Arenas=16 -Seed=$((SLURM_JOB_ID*1000+i)) \
    -Batches=30 -LearningAgentsUseExternalTrainingProcess -LearningAgentsIpAddress=127.0.0.1 \
    -LearningAgentsPort=$PORT -LearningAgentsTimeout=120 -abslog="$RUN/game_$i.log" &
done
wait   # games exit 0 when the trainer finishes 30 batches (960 gradient iterations)
grep -E "^INFO: *Iter|Iter:" "$RUN/trainer.log" | head -3; grep -E "Iter:" "$RUN/trainer.log" | tail -3
grep "SpikeA stats" "$RUN"/game_0.log | tail -3
ls -l "$RUN"/trainer/FPSAgent*/Snapshots/

# 2. resume test: same as 1 with -ResumeFrom=<run1>/trainer/FPSAgent0/Snapshots and -Batches=2
#    -> the first Iter lines should already be near run 1's final Avg Rew Sum (a fresh run starts near -3).
# 3. playback eval (no trainer; runs until stopped): timeout 120 "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended \
#      -Stage=SpikeA -Arenas=16 -Infer -ResumeFrom=<run1 snapshots> -abslog=$RUN/eval.log
#    -> "SpikeA stats ... reached=X%": untrained 0%, scripted (-Scripted) 100%.
```

**Pass if:**
- (a) `Avg Rew Sum` / `avgReturn` rises clearly from about −3 (≥ +5 by the end is a good result),
- (b) the four `.bin` snapshots exist,
- (c) the resumed run starts at the trained level,
- (d) playback with the snapshots reaches targets well above 0%.

Also record: seconds per iteration, agent-steps/s, CPU vs GPU, and the game startup time.

## Expected trainer config (check against the saved JSON)
The trainer saves the config it received to `$RUN/trainer/FPSAgent0/Configs/FPSAgent_PPO_Socket_<timestamp>.json`. It does so before training starts (`train.py` → `save_config`). **Only the first game that connects** supplies the config. LOCAL derived the values below from `ReachTaskActor.cpp` and the UE 5.7.4 headers (`LearningAgentsPPOTrainer.h/.cpp`). They can only be observed on Cradle: LOCAL may not train (hard rule 6).

**Run-specific values** (Spike A sets these):

| Key (`PPOSettings.*` unless marked top level) | Expected | Set by |
|---|---|---|
| `IterationNum` | `150` in run 1, `30` in the resume run (`1000000` without `-Iterations`) | `-Iterations` / `FReachTaskSettings::MaxIterations` |
| `SaveSnapshots` / `IterationsPerSnapshot` | `true` / `10` | Spike A / `FReachTaskSettings` |
| `Seed` | the first-connecting game's `-Seed` (`JOBID*1000+0` or `+1`) | Spike A (`RandomSeed = -Seed`) |
| `Device` (top level) | `"GPU"` (without CUDA, the trainer logs a fallback to CPU and continues) | `FReachTaskSettings::bTrainOnGpu` |
| `UseTensorBoard` / `UseMLflow` (top level) | `true` / `false` | Spike A |
| `TrainerMethod`, `CommunicationType`, `TaskName`, `BatchSize` (top level) | `"PPO"`, `"Socket"`, `"FPSAgent"`, `1` | engine / `train.py` (`--batch-size` default) |

**Learning Agents defaults** (Spike A doesn't change these):

| Key | Expected |
|---|---|
| `LearningRatePolicy` | `1e-4` |
| `LearningRateCritic` | `1e-3` |
| `LearningRateDecay` | `1.0` |
| `WeightDecay` | `1e-4` |
| `PolicyBatchSize` | `1024` |
| `CriticBatchSize` | `4096` |
| `PolicyWindow` | `16` |
| `IterationsPerGather` | `32` |
| `CriticWarmupIterations` | `8` |
| `EpsilonClip` | `0.2` |
| `ActionSurrogateWeight` | `1.0` |
| `ActionRegularizationWeight` | `0.001` |
| `ActionEntropyWeight` | `0.0` |
| `ReturnRegularizationWeight` | `1e-4` |
| `GaeLambda` | `0.95` |
| `DiscountFactor` | `0.99` |
| `AdvantageNormalization` | `true` |
| `AdvantageMin` / `AdvantageMax` | `0.0` / `10.0` |
| `UseGradNormMaxClipping` / `GradNormMax` | `false` / `0.5` |
| `TrimEpisodeStartStepNum` / `TrimEpisodeEndStepNum` | `0` / `0` |
| `ReplayBuffers[0].MaxEpisodeNum` / `MaxStepNum` | `1000` / `10000` (per-iteration recording limits) |

- **Floats are float32 widened to double.** For example, `1e-4` appears as `9.99999974737875e-05`, so compare with a relative tolerance of about 1e-6.
- **Not in the JSON:**
  - The episode cap of 360 steps lives on the game side. Check `SpikeA: ready. ... maxEpisodeSteps=360` in `game_0.log`.
  - The network sizes (policy 1×128 ELU + GRU memory 64; critic 1×128 ELU) are Learning Agents defaults. They arrive as initial weights, not as settings.
- **The game's side of the handshake:** `game_<i>.log` has `SpikeA: trainer settings: iterations=150 snapshotEvery=10 device=GPU tensorboard=1 seed=<seed>`.
  - From the C-004 fix build on, the line also shows batches: `iterations=960 (= 30.0 batches of 32) snapshotEvery=10 ...`.
  - `IterationNum` is still in gradient iterations (`-Batches=30` gives 960).

Print the config (`python -I` keeps the run folder off the import path):

```bash
python -I - "$RUN"/trainer/FPSAgent0/Configs/*.json <<'EOF'
import json, sys
c = json.load(open(sys.argv[1]))
print({k: c.get(k) for k in ("TrainerMethod", "Device", "UseTensorBoard", "UseMLflow", "BatchSize")})
print(json.dumps(c["PPOSettings"], indent=1))
EOF
```

Record mismatches in *Cluster result*. A different value in either table means the game build or the trainer copy isn't what we think it is.

## Cluster result
**PASS on all four criteria** (2026-11-11, CLUSTER). One bookkeeping bug found: training-time returns are about 5 too high (finding 2, C-004).

- Script: [`cluster/slurm/spikeA.sbatch`](../../cluster/slurm/spikeA.sbatch). The trainer ran on the GPU with the `la-train` env ([Spike E](E-python-gpu.md)).
- Run folders: `~/beanRant/runs/spikeA-20261111-<jobid>/` on Cradle (D-021).
- **Run 1b's final snapshots** (the ones behind the +10.25 playback) are published as Release [`run-spikeA-20261111-151705`](https://github.com/GoldNicolas/FPS-Agent/releases/tag/run-spikeA-20261111-151705) (2026-11-12). Run report and how to play them back: [`runs/spikeA-20261111-151705.md`](../runs/spikeA-20261111-151705.md).

| Step | Job | Setup | Result |
|---|---|---|---|
| Smoke | 151703 | 2 games × 16 arenas, `-Iterations=10` (1 batch) | Connected, stepped, logged, saved the 4 snapshots. All exits 0. 13 s. |
| Run 1 (as specified) | 151704 | `-Iterations=150` = **5 batches** (finding 1) | `Avg Rew Sum` −3.21 → **+6.94**. Exits 0. 29 s. |
| Run 1b (extended) | 151705 | `-Iterations=4800` = 150 batches | `Avg Rew Sum` −2.97 → **+14.87**; plateau about +15 after about 15 batches. Episode length 357 → 65 steps. Exits 0. 617 s. |
| Resume | 151707 | `-ResumeFrom=<run 1b>/snapshots_final`, `-Iterations=30` | First `Iter` line already **+15.30** (a fresh run starts near −3). 100% reached in the first window. Exits 0. |
| Playback: trained | 151708 | `-Infer`, run 1b snapshots, 120 s, no GPU | 29,899 episodes, **100.0% reached**, avgReturn **+10.25** |
| Playback: scripted | 151709 | `-Scripted` | 45,558 episodes, 100.0% reached, avgReturn +10.25 |
| Playback: untrained | 151710 | `-Infer`, no snapshots | 5,744 episodes, 0.0% reached, avgReturn −3.12 |

**Pass criteria:**
- (a) **PASS.** The reward rises from about −3 to +6.9 (run 1) and +14.9 (run 1b). The training-time numbers are inflated by about 5 (finding 2). Playback shows the true level, +10.25, which equals the scripted upper bound.
- (b) **PASS.** All four `.bin` files exist: policy 468,528 B, critic 103,192 B, encoder 420 B, decoder 168 B.
- (c) **PASS.** The resumed run starts at the trained level (+15.30 in training terms).
- (d) **PASS.** Playback with the snapshots reaches 100% of targets; untrained reaches 0%.

Trimmed real output (run 1b, `trainer.log` and `game_0.log`):
```
Iter:       0 | Avg Rew: -0.00835 | Avg Rew Sum: -2.97073 | Avg Return: -0.64445 | Avg Episode Len: 357.14286 | Batch Size: 1
Iter:     480 | Avg Rew:  0.20494 | Avg Rew Sum: 14.62287 | Avg Return:  8.11088 | Avg Episode Len:  77.51938 | Batch Size: 1
Iter:    4768 | Avg Rew:  0.25414 | Avg Rew Sum: 14.86879 | Avg Return:  9.62225 | Avg Episode Len:  65.35948 | Batch Size: 1
SpikeA: trainer settings: iterations=4800 snapshotEvery=10 device=GPU tensorboard=1 seed=151705000
SpikeA stats: gameTime=1680s mode=train episodes=148 total=22776 reached=89.9% avgReturn=13.877 agentSteps/s(wall)=2437
SpikeA: exiting (code 0): trainer finished
```

**Expected trainer config: no mismatches.** The saved `Configs/*.json` of the smoke run, run 1 and run 1b match all 36 values in the two tables above, with a relative tolerance of 1e-6. The resume run also matches, including `IterationNum` 30. `Seed` comes from whichever game connects first: game 1 in the smoke run (`151703001`), game 0 in the others. Every game logged `maxEpisodeSteps=360` and the expected `trainer settings` line.

**Numbers:**

| What | Value |
|---|---|
| Seconds per batch (32 iterations) | about 4.1 s: training a median of 2.3 s (3.0 s for the first batch, with critic warm-up) plus waiting for experience 2.0 s |
| Agent-steps/s while training | about 2,400 overall (games and trainer take turns, so the games sit idle while the trainer updates) |
| Agent-steps/s in playback (1 process, 16 arenas, 4 cores) | about 17,800 policy, about 26,900 scripted |
| CPU vs GPU | `Device=GPU` and used, but `nvidia-smi` shows a median of 18.5% and a peak of 32% utilization, 735 MiB. The networks are tiny. |
| Startup | Trainer listening 2–4 s after start; games connected 3–4 s after launch. The glibc 2.28 slowdown is negligible. |
| Disk | Build 1.1 GB unpacked; each training run 1.4–8 MB |

**Findings:**
1. **`IterationNum` counts gradient iterations, not experience batches.** `train_ppo.py` adds 1 to its counter per gradient step, and there are 32 steps per batch (`IterationsPerGather`). So `-Iterations=150` means 5 batches, and the `Iter` lines go 0, 32, 64… The snapshot check also runs per gradient step, so the 4 files are rewritten 3–4 times per batch. This is harmless. (C-005)
2. **Training-time returns are about 5 too high.** That's one `ReachBonus`. The same weights score +10.25 per episode in playback, exactly the scripted upper bound. During training, the game's own `avgReturn` (about 14–15.5, from `A.Return`) and the trainer's `Avg Rew Sum` (about 15) agree with each other but sit about 5 above it. Geometry also caps a 3–12 m target with a 1 m radius at about +11 on average. So in training mode the reach bonus seems to be paid or counted twice per episode. Learning still works, but the training curves overstate the return. (C-004)
3. **2 games with `--batch-size 1`:** every update uses one game's buffer (`Batch Size: 1`). The other game's buffer is often thrown away as stale (`discard_stale_experiences`). This is for Spike B: try `--batch-size` = the number of games.
4. **What the engine writes:**
   - In its `HOME` (redirected to the run folder): only `.config/Epic/NotAllowedUnattendedBugReports`.
   - Inside the build folder: `Saved/` and `Intermediate/`.
   - Nothing lands in the real `~/.config`.
   - A harmless warning: `sh: xdg-user-dir: command not found`.
5. **A CLUSTER script issue, already noted:** in playback mode the script's summary lines after `timeout` didn't print. The job ends together with the game. The numbers above come from each run's `eval.log`. This will be fixed in the script before Spike B.

## LOCAL fixes after the cluster result (2026-11-12)
- **C-004, root cause.** The bug was in our game code, not the engine.
  - Each training step, Learning Agents runs `GatherCompletions()` **before** `GatherRewards()` (`ULearningAgentsPPOTrainer::RunTraining`, UE 5.7.4).
  - Our completion check read a `bReached` flag that only the reward function updated, so it was always one step behind:
    - **The reaching step:** the completion check saw the old flag and said "running"; then the reward paid +5.
    - **The next step:** the completion check saw the flag and said "terminated"; the agent was still inside the radius, so the reward paid +5 again.
  - Playback was unaffected because our own loop scores first and checks afterwards. That's why only the training curves were offset.
- **C-004, the fix.**
  - Completion is now a pure check of the current distance (`FPSReach::FReachEpisode::IsTerminal`).
  - The bonus is paid at most once per episode (`FReachEpisode::Score`).
  - Both training and playback use the same bookkeeping.
  - Regression tests `FPSAgent.Learning.Reach.EpisodeOrder` / `BonusOnce` replay both call orders.
  - **Expected now:** training `avgReturn` / `Avg Rew Sum` match playback (about +10 at the end), and episodes end one step earlier.
  - **Design rule for every later task (D-022):** episode end must be computed from the current state, never from a flag set while computing the reward.
- **C-005.** `-Batches=N` sets the run length in experience batches (N × 32 gradient iterations), and the game logs both numbers.
  - The snapshot interval stays at 10 gradient iterations. The trainer saves only when `iteration % IterationsPerSnapshot == 0`.
  - So an interval of a whole number of batches would leave the last snapshot up to that many batches old.
  - With 10, it is at most 9 iterations old.
- **C-003.** The procedure above calls la-train's python by full path and points to `spikeA.sbatch`.

## Follow-ups
- STATUS.md: L-004 (run this spike), done. L-005: confirm the C-004 fix on the new build (`docs/builds.md`).
- Spike C (fixed step): Learning Agents' `BeginTraining` already sets a fixed 60 Hz timestep, uncapped FPS and max physics step = fixed step. Headless `-Infer` sets the same.
