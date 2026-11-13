# Spike C: fixed 60 Hz step and uncapped tick, headless
- Date / side: 2026-11-12, CLUSTER
- Build: `build-07eac52` · Slurm job(s): **151809** (this spike), plus the logs of the L-005 jobs 151806 (training), 151807 (policy playback) and 151808 (scripted playback). All on `gpu001` (`gpuq`).
- **Rerun 2026-11-12 (L-007, optional part):** the C-006 probe `-Stage=SpikeC` on `build-10520b5`, job **152036** (`gpu002`, CPU only, `combat.sbatch MODE=probe`).
- Question (README → Roadmap): do the fixed 60 Hz step and the uncapped tick behave correctly headless?
- Pass criterion: movement speeds and fire rate match the config within 1%.

## Result
**PASS** (2026-11-12, with the C-006 probe on `build-10520b5`, job 152036). The Linux probe line is identical to LOCAL's Windows line:
`SpikeC probe: run=540.00 (cfg 540.00) walk=300.00 (cfg 300.00) crouch=190.00 (cfg 190.00) cm/s; fireRate=9.744 (cfg 9.750) rounds/s over 20 shots; taps=8/8; tick=0.01667 s; verdict=PASS`, then `SpikeC: exiting (code 0): probe finished: PASS`.

First pass on `build-07eac52` (job 151809): **PARTIAL**. Everything that build could show passed, but half the criterion couldn't be tested on it:

| Check | Result |
|---|---|
| Fixed 60 Hz step, playback and training | **PASS**: exact |
| Uncapped tick | **PASS** |
| Run speed (540 cm/s) | **PASS**: 540.00 cm/s, −0.001% |
| Walk speed (300 cm/s), crouch speed, fire rate (9.75 rounds/s) | not testable on `build-07eac52`: request **C-006**. **PASS on `build-10520b5`** (probe, job 152036): walk 300.00, crouch 190.00 cm/s; 9.744 rounds/s (−0.06%); taps 8/8 |

Why the rest couldn't be tested on `build-07eac52`:
- No headless code path in this build walks, crouches or fires the rifle. The only headless stage is the Spike A reach task, and its agent always runs.
- All 33 C++ tests are `EditorContext`, so the packaged game can't run them.

## What I did
**Where the fixed step comes from.** No `-benchmark` or `-fps=` flag is needed.
- **Training:** Learning Agents' `BeginTraining` sets a fixed 60 Hz step and uncapped FPS, from `FLearningAgentsTrainingGameSettings`.
- **Headless playback:** `AReachTaskActor::BeginPlay` sets the same values (`FApp::SetUseFixedTimeStep`, a delta of 1/60 s, `t.MaxFPS 0`).

**Commands**
```bash
sbatch ~/beanRant/FPS-Agent/cluster/slurm/spikeC.sbatch   # job 151809, CPU only, 8 cores, 124 s
# 24 single-arena -Scripted processes (8 at a time, 20 s each), then one 16-arena -Scripted process (60 s)
~/beanRant/envs/la-train/bin/python -I cluster/scripts/spikeC_analyze.py <game logs...>
```

**How each check works** (also in the docstring of [`spikeC_analyze.py`](../../cluster/scripts/spikeC_analyze.py)):
1. **Fixed step.** A `SpikeA stats` line is logged every 10 s of game time. Each engine log line carries the frame counter modulo 1000. So with exactly one frame per 1/60 s, consecutive stats lines are 600 frames apart. The window's agent-steps (the logged rate × the wall time between the lines) must also equal arenas × 600.
2. **Uncapped tick:** game seconds per wall second, from the same lines.
3. **Run speed.** Each process logs its first 6 finished episodes. The episode return gives the distance walked:
   - `progress = 100·(R + 0.5·n/60 − 5·reached)` cm, from the C++ defaults of `FReachTaskSettings`.
   - A scripted agent walks straight at its target. Single-arena processes give unbiased start distances.
   - A least-squares fit of `progress = v·n + b` gives `v` in cm per step, so the speed is 60·`v`.

## Results (real numbers)
| Check | Logs | Result |
|---|---|---|
| Fixed step, playback | 26 scripted (24 × 1 arena, 2 × 16) + 1 policy playback (16 arenas) | **1,003 / 1,003** full 10 s windows are exactly 600 frames. Steps = arenas × 600 (ratio 0.9998–1.0003). |
| Fixed step, training | 2 games × 16 arenas (151806) | **66 / 66** windows are 600 frames; ratio 1.0000. Learning Agents takes exactly one step per agent per frame. |
| Uncapped tick | same | Game seconds per wall second: <br>• **17.5–17.8** with 1 arena (8 processes sharing 8 cores) <br>• **27.9–28.2** for one scripted 16-arena process <br>• **18.5** for policy playback with 16 arenas <br>• **2.5** in training, where the games pause while the trainer updates (Spike B) |
| Run speed | 156 scripted episodes, 28–126 steps, start distance 3.1–12.0 m | Fit: `progress = 8.9999 cm/step · n − 32.0 cm`. Residual sd 0.03 cm; slope standard error 0.0001. **540.00 cm/s vs `RunSpeed` 540 cm/s: −0.001%.** |
| Walk / crouch speed | `-Stage=SpikeC` probe, job 152036: speed over the last second of each 3 s phase | **walk 300.00 cm/s (cfg 300.00), crouch 190.00 cm/s (cfg 190.00): exact.** Run in the same probe: 540.00. |
| Fire rate | same probe: trigger held for 20 shots, then 8 one-tick taps | **9.744 rounds/s (cfg 9.750): −0.06%**, PASS within 1%. The gap is frame quantization: 19 intervals of 1/9.75 s = 6.15 frames end on frame 117 = 1.95 s, and 19 / 1.95 = 9.744. The remainder carries over, so the long-run rate is exact. **Taps 8/8.** Before the probe, code reading: `FRifleFireState::Update` carries the remainder (`NextShotTime += Interval`), and LOCAL's test `FPSAgent.Rifle.Cadence` steps it at 60 Hz (30 shots in 3 s), so a pass is expected. |

## Findings / numbers
1. **The fixed step is exact on Linux** in both training and playback. One policy decision = one 1/60 s frame (D-019) holds headless on Cradle. `-benchmark` and `-fps=` aren't needed.
2. **The −32 cm intercept is acceleration, as configured.**
   - `CharacterMovement` at 1/60 s with `MaxAcceleration` 4000 cm/s² from rest gives a velocity of min(k·66.7, 540) cm/s at frame k.
   - That's 8 frames to full speed, so the distance after n frames is exactly 9n − 32 cm. Every episode fits this to within 0.1 cm.
   - So `MaxAcceleration` matches the config too.
3. **Uncapped means CPU-bound.**
   - One arena per process wasn't faster than 16 arenas per process: 1,060 against 1,690 frames per second. Fixed per-frame engine cost and 8 processes sharing 8 cores dominate.
   - Spike B measures the scaling properly.

## Follow-ups
- Requests added to STATUS.md: **C-006**, a headless probe for walk/crouch speeds and fire rate. CLUSTER reruns this spike with it. **Done:** LOCAL added `-Stage=SpikeC` in `build-10520b5`; the rerun (job 152036) passes.
- Probe output on Cradle: `~/beanRant/runs/s0probe-20261112-152036/probe/game.log`.
- Analysis output: `~/beanRant/runs/spikeC-20261112-151809/analysis_all.txt` on Cradle.
