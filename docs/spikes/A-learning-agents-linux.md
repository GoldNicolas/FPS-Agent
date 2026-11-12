# Spike A: packaged Learning Agents game trains on Cradle against an external trainer
- Date / side: 2026-11-11, LOCAL (build). The CLUSTER side runs it and fills in **Cluster result** below.
- Build: **`build-cccd809`** (so `<sha>` below = `cccd809`) · Slurm job(s): _CLUSTER fills in_
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
This is a sketch. The CLUSTER side owns the real `cluster/slurm/` script.

```bash
# Paths per D-021 / docs/cradle.md -> "Our paths": everything under ~/beanRant
# 0. fetch + verify the build (tag in docs/builds.md)
gh release download build-<sha> -D ~/beanRant/builds/<sha> && cd ~/beanRant/builds/<sha>
sha256sum -c SHA256SUMS && tar xzf FPSAgent-Linux-<sha>.tar.gz && cat Linux/BUILD_INFO.txt

# 1. on a compute node (srun/sbatch, ~15 min, 1 GPU or CPU-only, ~8 cores)
BUILD=~/beanRant/builds/<sha>/Linux; REPO=~/beanRant/FPS-Agent
RUN=~/beanRant/runs/spikeA-$(date +%Y%m%d)-$SLURM_JOB_ID; mkdir -p "$RUN"   # run ID per docs/runs/README.md
PORT=$((40000 + SLURM_JOB_ID % 20000))
source ~/miniconda3/bin/activate ~/beanRant/envs/la-train
python3 -u $REPO/cluster/learning_agents_python/train.py FPSAgent -l \
  --nne-cpu-path $REPO/cluster/learning_agents_python/nne_runtime_basic_cpu \
  Socket 127.0.0.1:$PORT "$RUN/trainer" > "$RUN/trainer.log" 2>&1 &
until grep -q "Server listening on" "$RUN/trainer.log"; do sleep 1; done   # do NOT probe the port
for i in 0 1; do
  "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended -Stage=SpikeA -Arenas=16 -Seed=$((SLURM_JOB_ID*1000+i)) \
    -Iterations=150 -LearningAgentsUseExternalTrainingProcess -LearningAgentsIpAddress=127.0.0.1 \
    -LearningAgentsPort=$PORT -LearningAgentsTimeout=120 -abslog="$RUN/game_$i.log" &
done
wait   # games exit 0 when the trainer reaches 150 iterations
grep -E "^INFO: *Iter|Iter:" "$RUN/trainer.log" | head -3; grep -E "Iter:" "$RUN/trainer.log" | tail -3
grep "SpikeA stats" "$RUN"/game_0.log | tail -3
ls -l "$RUN"/trainer/FPSAgent*/Snapshots/

# 2. resume test: same as 1 with -ResumeFrom=<run1>/trainer/FPSAgent0/Snapshots and -Iterations=30
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
_CLUSTER fills in: PASS / FAIL / PARTIAL with trimmed real output, numbers, and problems._

## Follow-ups
- STATUS.md: L-004 (run this spike).
- Spike C (fixed step): Learning Agents' `BeginTraining` already sets a fixed 60 Hz timestep, uncapped FPS and max physics step = fixed step. Headless `-Infer` sets the same.
