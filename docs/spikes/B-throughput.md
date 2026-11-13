# Spike B: throughput against N game processes × K arenas, and the trainer's `--batch-size`
- Date / side: 2026-11-12, CLUSTER
- Build: `build-07eac52` (Spike A reach task) · Slurm job(s), all 32 cores, `gpuq`:
  - **151810:** simulation only;
  - **151811:** training sweep;
  - **151812:** seed repeats;
  - **151931:** trainer with shortened polls;
  - **151932:** trainer on CPU, no GPU allocated, on `gpu002`. All others ran on `gpu001`, with 1 A100 for training.
- Question (README → Roadmap): how fast is each process, and which N (processes) × K (arenas) works best? The user also asked to include `--batch-size` = number of games.
- L-006 adds the trainer side: batch size, experience per update, GPU vs CPU, seconds per batch.
- Pass criterion: a table of steps/s against N and K on one node, and a chosen default.
- **Rerun on combat build A (`build-10520b5`, S0), L-007 (2), 2026-11-12:** jobs **152032–152034** (training; K = 8, 16, 32 arenas, each with N = 4, 8, 16 games and `--batch-size N`; 40 cores and 1 A100 each, on `gpu002`, `gpu003`, `gpu005`) and **152035** (simulation, CPU only, `gpu006`). Pinned trainer with the D-029 polls. See [*Combat build A*](#combat-build-a-build-10520b5-s0-l-007-2) below; the default is **D-030**.

## Result
**PASS:** tables below; the default is D-027.

**Short version:**
- **The games are not the bottleneck; the trainer is.** One playback process does 16–36 k agent-steps/s on about 2 cores. Training used only about 2.5 k/s with `--batch-size 1`, because each update costs a fixed ~3.9 s.
- **`--batch-size` = N uses ~8× more experience per second**, with almost nothing thrown away. **It doesn't learn faster per batch** with the stock PPO settings: each update is still 32 gradient iterations on fixed-size minibatches.
  - Up to 80 k steps per update (8 games) it learned about as fast.
  - At 160 k (16 games), 2 of 3 runs didn't reach +9 in 25 batches.
- **The trainer's three 1-second polls cost 0.5–1.5 s per batch.** A patched copy cut seconds per batch by **15–31%** (job 151931).
- **GPU vs CPU:**
  - The stock trainer on CPU is single-threaded by design (`torch.set_num_threads(1)`). Its gradient work takes **7.1–7.5 s per batch against 2.1–2.3 s on the A100**, so batches take 2.0–2.3× longer.
  - **Train on the GPU.**
- **The real levers are trainer-side:** request **C-007** (the polls, PPO minibatch sizes that grow with the data, buffer size against episode length).

## What I did
```bash
S=~/beanRant/FPS-Agent/cluster/slurm/spikeB.sbatch
sbatch -t 30 --export=ALL,MODE=sim,RESUME_FROM=<L-005 snapshots> $S          # 151810: 11 N×K configs, 45 s each
sbatch -t 60 --gres=gpu:1 --export=ALL,MODE=train,ARENAS=32,BATCHES=25 $S    # 151811: 8 N×batch-size configs
sbatch -t 60 --gres=gpu:1 --export=ALL,MODE=train,ARENAS=32,BATCHES=25,CONFIGS="8x1 8x8 8x1 8x8 8x1 8x8" $S   # 151812
sbatch -t 60 --gres=gpu:1 --export=ALL,MODE=train,ARENAS=32,BATCHES=25,CONFIGS="1x1:poll 4x4:poll 8x8:poll 16x16:poll 8x8" $S       # 151931
sbatch -t 120 --export=ALL,MODE=train,ARENAS=32,BATCHES=25,CONFIGS="1x1:cpu 4x4:cpu 8x8:cpu 16x16:cpu 1x1:cpu:poll 8x8:cpu:poll" $S  # 151932, no GPU
~/beanRant/envs/la-train/bin/python -I cluster/scripts/spikeB_summarize.py ~/beanRant/runs/spikeB-<date>-<job>
```
- **`:cpu`** hides the GPU from the trainer (`CUDA_VISIBLE_DEVICES=`). It logs `Defaulting to CPU training` and continues, which the summary checks.
- **`:poll`** runs a copy of the trainer, made inside the run folder, with the three per-batch polls shortened. The diff is [`B-throughput/trainer-poll-patch.diff`](B-throughput/trainer-poll-patch.diff). The pinned trainer in `cluster/learning_agents_python/` (LOCAL-owned) is unchanged.
- **sim mode:** `-Infer` playback of the L-005 policy, with no trainer and no GPU.
- **train mode:** the real trainer (`train.py ... Socket ... --batch-size B`) + N games, 25 experience batches per config, from scratch. Every config of job 151812 has its own seeds.
- How each number is computed is in the [`spikeB_summarize.py`](../../cluster/scripts/spikeB_summarize.py) docstring.
  - Steps/s come from game time: Spike C showed exactly one step per agent per 1/60 s.
  - CPU comes from `ps` samples every 5 s.
  - "Used" experience = batches × batch size × `MaxStepNum` (10,000).
- CSVs: [`B-throughput/`](B-throughput/).

## Simulation only (job 151810)
32 allocated cores; each process runs the trained policy on K arenas.

| N proc | K arenas | agents | steps/s total | steps/s per process | frames/s per process | CPU cores per process | RSS MB per process |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 1 | 1 | 2,779 | 2,779 | 2,779 | 1.9 | 352 |
| 1 | 4 | 4 | 8,399 | 8,399 | 2,100 | 2.1 | 384 |
| 1 | 16 | 16 | 16,368 | 16,368 | 1,023 | 1.6 | 385 |
| 1 | 64 | 64 | 26,760 | 26,760 | 418 | 2.1 | 396 |
| 1 | 256 | 256 | 35,616 | 35,616 | 139 | 2.2 | 439 |
| 2 | 64 | 128 | 53,767 | 26,883 | 420 | 2.1 | 394 |
| 4 | 64 | 256 | 107,640 | 26,910 | 420 | 2.1 | 395 |
| 8 | 64 | 512 | 220,914 | 27,614 | 431 | 2.0 | 406 |
| 16 | 64 | 1,024 | **415,065** | 25,942 | 405 | 1.9 | 403 |
| 32 | 16 | 512 | 412,249 | 12,883 | 805 | 1.5 | 380 |
| 32 | 64 | 2,048 | **577,596** | 18,050 | 282 | 1.5 | 398 |

- **Per process:** more arenas → more steps/s, flattening out (16 → 64 → 256 arenas: 16 k → 27 k → 36 k). A frame costs a fixed overhead plus a per-agent part.
- **Per node:** linear up to 16 processes on 32 cores (about 2 cores each). Oversubscribing (32 processes) still adds 40%.
- **Memory:** 350–440 MB per process; it doesn't limit us.

## Training (jobs 151811 and 151812)
32 arenas per game, 25 batches, 1 A100. "Steps per batch" assumes a full buffer of `MaxStepNum` = 10,000 per game.

| Job · first seed | N games | `--batch-size` | s per batch | used steps/s | used / simulated | Avg Rew Sum (last 3) | batch (wall s) to ≥ 9 | GPU util median / max |
|---|---:|---:|---:|---:|---:|---:|---|---|
| 151811 | 1 | 1 | 3.91 | 2,558 | 0.81 | 9.98 | 6 (33 s) | 22% / 33% |
| 151811 | 2 | 1 | 3.88 | 2,580 | 0.40 | 9.74 | 8 (40 s) | 17% / 34% |
| 151811 | 2 | 2 | 3.99 | 5,006 | 0.81 | 9.93 | 7 (37 s) | 15% / 34% |
| 151811 | 4 | 1 | 4.07 | 2,455 | 0.20 | 9.15 | 23 (103 s) | 16% / 32% |
| 151811 | 4 | 4 | 4.50 | 8,888 | 0.79 | 7.77 | not in 25 | 10% / 34% |
| 151811 | 8 | 1 | 3.87 | 2,586 | 0.10 | 9.90 | 12 (57 s) | 12% / 32% |
| 151811 | 8 | 8 | 4.47 | 17,907 | 0.81 | −0.77 | not in 25 | 1% / 33% |
| 151811 | 16 | 16 | 6.24 | **25,634** | 0.76 | 9.11 | 20 (138 s) | 0% / 34% |
| 151812 · …000 | 8 | 1 | 3.81 | 2,628 | 0.10 | 10.07 | 5 (30 s) | 23% / 34% |
| 151812 · …050 | 8 | 8 | 4.02 | 19,902 | 0.77 | 9.71 | 11 (55 s) | 1% / 33% |
| 151812 · …100 | 8 | 1 | 3.84 | 2,606 | 0.10 | 10.12 | 7 (35 s) | 27% / 32% |
| 151812 · …150 | 8 | 8 | 4.60 | 17,393 | 0.77 | 9.72 | 11 (61 s) | 3% / 32% |
| 151812 · …200 | 8 | 1 | 3.99 | 2,505 | 0.10 | 9.87 | 10 (51 s) | 24% / 31% |
| 151812 · …250 | 8 | 8 | 4.91 | 16,296 | 0.77 | 9.73 | 8 (47 s) | 0% / 34% |

**Where a batch's time goes** (the trainer's own `Profile|` lines, mean per batch):

| | 1 game, batch size 1 | 16 games, batch size 16 |
|---|---:|---:|
| `PPO train` (32 gradient iterations) | 2.23 s | 2.10 s |
| `Pull Experience` (waiting for buffers) | 1.50 s | 1.53 s |
| `PPO create batches` | 0.07 s | **2.35 s** |
| `PPO gae`, load, logging | 0.07 s | 0.18 s |
| **Batch period** | **3.9 s** | **6.2 s** |

## Trainer side: shorter polls, and CPU vs GPU (jobs 151931, 151932; L-006)
32 arenas per game, 25 batches, `--batch-size` = N. Seconds per batch:

| N × batch size | GPU, stock | GPU, polls shortened | CPU, stock | CPU, polls shortened |
|---|---:|---:|---:|---:|
| 1 × 1 | 3.91 | **2.86** (−27%) | 8.84 | 7.90 |
| 4 × 4 | 4.50 | **3.10** (−31%) | 9.82 | — |
| 8 × 8 | 4.62 (same job; 4.02–4.91 in 151811/151812) | **3.64** (−21%) | 10.72 | 9.47 |
| 16 × 16 | 6.24 | **5.31** (−15%) | 12.54 | — |

Where the time goes (trainer `Profile|` lines, median per batch):

| | GPU, stock | GPU, polls shortened | CPU, stock |
|---|---:|---:|---:|
| `PPO train` | 2.1–2.3 s | 2.1–2.3 s | **7.1–7.5 s** |
| `Pull Experience` | 2.00 s (about 1.0 s in several 8-game runs) | **0.47–0.57 s** | 2.00 s (1.03 s at 16 × 16) |
| `PPO create batches` | 0.07 / 0.33 / 0.80 / 2.3 s for 1, 4, 8, 16 games | same | same |

| | 1 × 1 | 4 × 4 | 8 × 8 | 16 × 16 |
|---|---:|---:|---:|---:|
| Used steps/s, GPU with shortened polls | 3,502 | 12,904 | 22,001 | 30,155 |

- **Shortened polls:** waiting falls to the games' real simulation time for one buffer (about 0.5 s for 360 frames × 32 arenas). Everything else is unchanged.
  - All 75 games in jobs 151931/151932 exited 0, so the faster pings broke nothing.
  - The A100's median use rises from 0–27% to about 30% with 1–8 games.
- **CPU:**
  - `train_ppo.py` line 61 calls `torch.set_num_threads(1)`, so the stock CPU trainer uses one core (0.7–0.9 cores measured). That's what `-Set=Combat.Ppo.bTrainOnGpu=false` (Build A) would give.
  - The A100 does the same 32 iterations 3.3× faster, even for Spike A's tiny networks (1 × 128, memory 64). Build A's are 2 × 256 with memory 128, which favours the GPU more.
  - A multi-threaded CPU trainer would need another trainer patch. It isn't worth it: every Cradle node has GPUs.
- **Learning per batch** is the same algorithm on CPU and GPU, so these runs also feed finding 3.

## Findings
1. **The trainer sets the pace, not the games.**
   - Each update costs a fixed ~2.2 s of gradient work: 32 iterations with fixed minibatch sizes (policy 1,024, critic 4,096). The GPU sits mostly idle (median 0–27%, ≤ 1.5 GB).
   - On top of that comes 1–2 s waiting for buffers (median exactly 2.00 s), even though a game fills one in ~0.5 s. The trainer's socket code has **three 1-second polls** in the per-batch loop, all in `socket_communicator.py`:
     - `receive_experience`: the main thread checks for buffers once a second;
     - `_sender`: sends the new policy to the games once a second;
     - `_worker`: reads the game's next buffer at most a second after the policy went out.
   - Shortening them (job 151931) cut seconds per batch by 15–31% (*Trainer side* above).
   - The games sit idle most of the time: 16 games use 2.8 cores in total while training, against about 2 cores *each* when simulating freely.
2. **`--batch-size 1` with N games throws away (N−1)/N of the simulation.**
   - The trainer takes the first buffer to arrive. The others are stale after the update and are dropped (logged at debug level only).
   - So used / simulated is 0.81, 0.40, 0.20, 0.10 for N = 1, 2, 4, 8.
   - `--batch-size N` keeps it at about 0.8, so used experience scales: 2.5 k → 5.0 k → 8.9 k → 17.9 k → 25.6 k steps/s for N = 1, 2, 4, 8, 16.
3. **…but more experience per batch didn't learn faster; at 160 k steps per update it learned worse.** All 25 training runs of jobs 151811, 151812, 151931 and 151932, grouped by batch size:

   | `--batch-size` (steps per update) | runs | batches to reach +9 (25 max) | Avg Rew Sum at the end |
   |---|---:|---|---|
   | 1 (10 k) | 10 | 5, 6, 7, 7, 7, 7, 8, 10, 12, 23 (median 7) | 9.2–10.1 |
   | 2 (20 k) | 1 | 7 | 9.9 |
   | 4 (40 k) | 3 | 6, 12, not reached (7.8) | 7.8–10.1 |
   | 8 (80 k) | 8 | 8, 8, 9, 11, 11, 11, 15, not reached (−0.8) (median 11) | 9.5–9.7 (one −0.8) |
   | 16 (160 k) | 3 | 20, not reached (2.4), not reached (−0.1) | 9.1 / 2.4 / −0.1 |

   - **Why it's plausible:** the gradient work per update is fixed (32 iterations × 1,024-window policy minibatches, 4,096 for the critic). A sample is reused about 50 times at 10 k steps per update, but about 3 times at 160 k.
   - Every slow run sits near −2 for many batches before climbing.
   - **Plateau:** about +10.0 with batch size 1, about +9.6 with 8. A possible contributor: batch size 1 favours the game whose episodes finish fastest. This is a guess, not measured.
   - **Variation:** single runs vary a lot. 4 games with batch size 1 took 23 batches; 1 game took 6.
4. **Where the remaining ~20% goes, even with 1 game.** The game log shows the pattern:
   - The game pushes its buffer about 350–366 frames after the last reset, which is about 11,300 steps at 32 arenas, for a 10,000-step buffer.
   - **Every push is followed by a reset of all agents** (`LearningAgentsManager: Resetting Agents [0 1 2 … 31]`). Spike A asks for this: it passes `bResetAgentsOnUpdate = true` to `RunTraining`.
   - So the buffer holds completed episodes, and the episodes in progress at push time are dropped.
   - **For combat, LOCAL's Build A plan already turns this off** (planned D-026, `bResetAgentsOnUpdate = false`), so long episodes aren't cut at every update.
   - What's left to check is the buffer size. With 60 s episodes (900 decisions at 15 Hz, D-023) and 32 arenas, one round of episodes is about 28,800 steps, while `MaxStepNum` is 10,000.
   - Whether that only means more frequent, smaller updates, or lost steps, depends on how the 5.7 buffer handles episodes that don't fit (C-007).
5. **Batch-building grows with data:** `PPO create batches` takes 2.35 s for 160 k steps. Above about 8 games × 10 k steps it becomes the next bottleneck.
6. **One A100 is far more than this trainer needs:** median 0–31%, peaks 34%, ≤ 1.5 GB. Sweeps could put several trainers on one GPU (to test when sweeps start). The CPU fallback isn't an alternative (*Trainer side*).

## Chosen default (D-027)
First S0 jobs: **N = 4 games × K = 32 arenas, `--batch-size 4`, `-c 8` (2 cores per game), 1 GPU.** Re-measure with Spike B's sim mode on the first S0 build:
- **Batch size N:** uses all experience and is unbiased across games. Up to 80 k steps per update it cost no learning speed within the noise (finding 3). S0 steps will be far more expensive than the toy task's, so wasting 75–90% of them would hurt.
- **N = 4 (40 k steps per update):** with the stock PPO settings, more data per update doesn't learn faster, and 160 k learned worse. Go wider only once C-007 lets the minibatch sizes grow with the data.
- **K = 32:** past ~32 arenas, steps per process grow slowly. Also K × episode length must fit `MaxStepNum` (finding 4).
- **GPU trainer:** the CPU fallback doubles the time per batch.

## Combat build A: `build-10520b5`, S0 (L-007 (2))
Re-measured on the combat stage, as D-027 asked. S0 decides 15 times per game second (D-023), so a "step" here is one agent decision (4 fixed 60 Hz ticks).

**Commands** (`spikeB.sbatch` now takes `STAGE=S0`; training adds `-StatsCsv` per game; sim mode runs `-Eval` with a huge episode count at a fixed level `@L`, so the ramp can't change the number of bots):
```bash
S=~/beanRant/FPS-Agent/cluster/slurm/spikeB.sbatch
for K in 8 16 32; do
  sbatch -t 75 -c 40 --gres=gpu:1 --export=ALL,STAGE=S0,BUILD_SHA=10520b5,MODE=train,ARENAS=$K,BATCHES=15,CONFIG_LIMIT=900,CONFIGS="4x4 8x8 16x16" $S
done
sbatch -t 30 -c 40 --gres=none --export=ALL,STAGE=S0,BUILD_SHA=10520b5,MODE=sim,SECONDS_EACH=45,\
RESUME_FROM=<smoke 152030 a_train3/snapshots_final>,CONFIGS="1x8@0 1x16@0 1x32@0 1x64@0 1x32@1 1x64@1 4x32@0 8x32@0 16x32@0 16x32@1" $S
```
Level `@0` is 1 static bot per arena; `@1` is 5. The sim policy is the 3-batch smoke snapshot (it mostly times out, like a fresh run). CSVs: [`s0-sim-152035.csv`](B-throughput/s0-sim-152035.csv), [`s0-train-152032-152034.csv`](B-throughput/s0-train-152032-152034.csv).

### Simulation only (job 152035, no trainer)
| N proc | K arenas | bots | decisions/s total | per process | per arena | game s per wall s | CPU cores per process | RSS MB per process |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 8 | 1 | 2,055 | 2,055 | 257 | 17.1 | 1.42 | 376 |
| 1 | 16 | 1 | 2,376 | 2,376 | 148 | 9.9 | 1.37 | 396 |
| 1 | 32 | 1 | 3,050 | 3,050 | 95 | 6.4 | 1.48 | 404 |
| 1 | 32 | 5 | 2,458 | 2,458 | 77 | 5.1 | 1.39 | 413 |
| 1 | 64 | 1 | 3,363 | 3,363 | 53 | 3.5 | 1.48 | 430 |
| 1 | 64 | 5 | 2,663 | 2,663 | 42 | 2.8 | 1.29 | 437 |
| 4 | 32 | 1 | 12,678 | 3,169 | 99 | 6.6 | 1.50 | 405 |
| 8 | 32 | 1 | 24,786 | 3,098 | 97 | 6.5 | 1.50 | 404 |
| 16 | 32 | 1 | 43,642 | 2,728 | 85 | 5.7 | 1.48 | 403 |
| 16 | 32 | 5 | 34,840 | 2,177 | 68 | 4.5 | 1.39 | 406 |

### Training (jobs 152032–152034, 15 batches per config)
| K arenas | N games = `--batch-size` | s per batch | `PPO train` s | `Pull Experience` s | `PPO create batches` s | used decisions/s | used / simulated | GPU median / max | GPU memory | trainer cores | game cores (all) |
|---:|---:|---:|---:|---:|---:|---:|---:|---|---:|---:|---:|
| 8 | 4 | 18.62 | 11.28 | 5.93 | 0.32 | 2,148 | 0.78 | 12% / 27% | 1.5 GB | 0.67 | 1.9 |
| 8 | 8 | 20.08 | 11.36 | 6.01 | 0.79 | 3,985 | 0.78 | 11% / 27% | 2.1 GB | 0.72 | 3.2 |
| 8 | 16 | 23.99 | 11.55 | 6.66 | 2.24 | 6,669 | 0.79 | 4% / 27% | 3.4 GB | 0.76 | 3.8 |
| 16 | 4 | 18.94 | 11.64 | 6.02 | 0.32 | 2,112 | 0.67 | 12% / 27% | 1.5 GB | 0.65 | 1.8 |
| 16 | 8 | 20.13 | 11.54 | 6.09 | 0.79 | 3,974 | 0.66 | 11% / 27% | 2.1 GB | 0.71 | 3.5 |
| 16 | 16 | 24.27 | 11.96 | 6.58 | 2.25 | 6,593 | 0.68 | 4% / 26% | 3.4 GB | 0.77 | 3.9 |
| 32 | 4 | 19.00 | 11.31 | 6.33 | 0.32 | 2,105 | 0.51 | 12% / 27% | 1.5 GB | 0.64 | 1.9 |
| 32 | 8 | 20.83 | 11.79 | 6.48 | 0.79 | 3,840 | 0.53 | 11% / 27% | 2.1 GB | 0.70 | 3.1 |
| 32 | 16 | 24.62 | 11.55 | 7.41 | 2.20 | 6,498 | 0.54 | 3% / 27% | 3.4 GB | 0.74 | 5.1 |

All 84 games exited 0. Every game log stayed small: 6–9 KB per game per batch, 59–129 `Resetting Agents` lines per game over 15 batches. The trainer's `Avg Rew Sum` rose from about −0.5 to −0.2 in every config, at about the same rate per batch for N = 4, 8 and 16; nothing cleared yet (`level` 0.00), as expected after 15 batches.

### Combat findings
1. **The trainer sets the pace, and a combat update costs 3× a reach-task update.**
   - `PPO train` takes **11.3–12.0 s per batch whatever N and K are** (Spike A: 2.2 s).
   - The combat policy has a 128-wide memory state trained over 16-step windows (`PolicyWindow` 16, 1,024 windows per policy minibatch) and attention encoders for the enemy and sound sets (saved trainer config).
   - The update is overhead-bound, not GPU-bound: the GPU's median use is 3–12% and the trainer process uses 0.64–0.77 cores.
2. **Seconds per batch grow only with N, through batch-building:** 19 s (N = 4), 20–21 s (N = 8), 24–25 s (N = 16). `PPO create batches` goes 0.32 → 0.79 → 2.2 s, as in Spike A. K hardly matters.
3. **Experience per second grows almost linearly with N** (about 2,100 / 3,900 / 6,600 used decisions/s), because the fixed `PPO train` dominates. Per batch, `Avg Rew Sum` improved at the same rate for N = 4, 8 and 16 in these 15 batches, matching Spike A: more experience per update didn't learn faster per update.
4. **While untrained agents all time out together, the replay buffer drops part of every round.**
   - Each game's buffer holds 10,000 steps (`ReplayMaxSteps`). All K episodes end on the same tick at 20 s (300 decisions).
   - At K = 32 a round is 9,600 steps, so the buffer fills on the 2nd round. The game pushes every 40 s of game time (checked in the log), and the rest of that round is dropped: **used / simulated = 0.51–0.54** (predicted 10,000 / 19,200 = 0.52).
   - The same arithmetic gives 0.69 at K = 16 (3 rounds; measured 0.66–0.68) and 0.83 at K = 8 (5 rounds; measured 0.78–0.79).
   - So `Pull Experience` (about 6 s for every K) is the game time of 2–5 lockstep rounds.
   - Once agents clear at different times, episodes de-synchronize and the loss shrinks to the episodes that end on the sending tick. The fill time should then fall to 10,000 / (decisions/s per game): about 3.3 s at K = 32 and 4.9 s at K = 8.
   - No change is needed; S0 training watches it.
5. **Simulation per process (CPU only):**
   - 2,055 decisions/s at K = 8 up to 3,363 at K = 64, on about 1.4–1.5 cores and 0.4 GB.
   - At K = 32, one arena runs at 6.4× real time.
   - On one node, 16 processes × 32 arenas reach 43,642 decisions/s (−14% per process against 4 processes).
   - In training, the games are idle most of the time: all games together use 1.8–5.1 cores, because they wait for the trainer.
6. **The static bots are cheap next to the agent's senses.**
   - 5 bots instead of 1 cost **19–21%** of decisions/s: 3,050 → 2,458 at K = 32, 3,363 → 2,663 at K = 64, 43,642 → 34,840 for 16 × 32.
   - That's about 20 µs per extra bot per decision (4 ticks), against about 330 µs per decision for the agent with its 82 traces, the first bot and the engine's per-arena cost (K = 32, level 0).
7. **No problems with the patched polls (D-029):** no heartbeat or connection errors in 84 game logs. The only side effect is cosmetic: `_worker` tracebacks after `Socket Communicator Exiting...` at the trainer's shutdown ([smoke report](../runs/s0smoke-20261112-152030.md), observation 4).

### Chosen default for combat (D-030)
**D-027 stands for S0: N = 4 games × K = 32 arenas, `--batch-size 4`, 1 GPU, `-c 8`**, now measured on the combat build:
- **N = 4:** the fastest updates (19.0 s per batch). N = 8 costs +10% per batch and N = 16 +30%, for more experience per update that didn't learn faster per update here or in Spike A.
- **K = 32:** the fastest game per process (3,050 decisions/s against 2,055 at K = 8), so the shortest buffer fill once episodes de-synchronize. One lockstep round (32 × 300 = 9,600) also fits `ReplayMaxSteps` (10,000).
- **`-c 8`:** 4 games use about 2 cores in training and 6 at full speed, plus the trainer's ~0.7.
- **1 GPU:** it's 88–97% idle. Several trainers per GPU would be the better fit for sweeps (one job, several trainers); it isn't needed for one chain.
- **Job length:** about 300 batches per 2 h job (19 s × 300 = 95 min, plus the 5 min `USR1` margin).
- **Revisit** when S0 is learning: N = 4 against N = 8 on learning per wall-clock hour, and the trainer knobs `-Set=Combat.Ppo.IterationsPerGather` / `PolicyBatchSize` (they change PPO, so compare on `-Eval`).

## Follow-ups
- Requests added to STATUS.md: **C-007** (trainer and PPO settings for scaling).
- Spike D (multi-node) is on hold (user, 2026-11-12). Finding 1 says one node already has far more simulation capacity than one trainer can use.
- Run folders on Cradle: `~/beanRant/runs/spikeB-20261112-{151810,151811,151812,151931,151932}/` (73 MB).
- Combat rerun: `~/beanRant/runs/spikeB-20261112-{152032,152033,152034,152035}/` on Cradle.
