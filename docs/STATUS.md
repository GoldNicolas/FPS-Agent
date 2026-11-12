# Status board

The mailbox between the **LOCAL** side (Windows: game, builds) and the **CLUSTER** side (Cradle: training). Both sides read this first after `git pull`, and update it before every push. Protocol: see `CLAUDE.md` → *Two roles, one repo*.

_Last updated: 2026-11-11 by CLUSTER_

## Current focus
- **LOCAL:** Plans 1 + 2 **done**.
  - Sandbox: beans, rifle, health/armor, range + bots.
  - Maps: original `map01` with elevation, plus a 50-map validated pool, all built at runtime from JSON. Choose with `-Map=range|map01|gen_007|pool`.
  - Tests: 27 C++ (headless) + 18 Python; verified in PIE via MCP.
  - Linux cross-compile ready: v26 toolchain + Linux target platform installed, Turnkey `VerifySdk` → Valid.
  - Spike A built and published as **`build-cccd809`**:
    - toy reach task (`-Stage=SpikeA`);
    - trainer Python pinned in `cluster/learning_agents_python/`;
    - 31 C++ tests pass.
  - Next: the real S0 Learning Agents environment (ray-cast/line-of-sight observations, aim/move/shoot, rewards vs scripted bots).
- **CLUSTER:** L-001 recon **done** ([`cradle.md`](cradle.md)): `gpuq` (A100 80GB PCIe, driver 550.54.14 / CUDA 12.4, no time limit). Next: L-003 env (add `smart_open`, plus `gh` from conda-forge) → **L-004 Spike A run**. **Paths changed (D-021):** everything on Cradle lives under `~/beanRant/` (repo, builds, runs, Slurm logs, conda env); see cradle.md → *Our paths* and C-001.

## Latest build
**`build-cccd809`** (2026-11-11): first Linux build, Spike A. Details and what to test: [`builds.md`](builds.md). Procedure: [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md).

## Requests: LOCAL → CLUSTER
| ID | Date | Request | Status | Answer / link |
|---|---|---|---|---|
| L-001 | 2026-11-11 | Recon: run the commands in [`cradle.md`](cradle.md), fill in every `TBD`, and list which questions for the admins are still open | done | 2026-11-11: all TBDs filled except node-to-node TCP (Spike D). Use `gpuq` (milab group). No Slurm accounting, so no `sacct`. 5 admin questions open, listed in [`cradle.md`](cradle.md). |
| L-002 | 2026-11-11 | Bootstrap tools: Miniforge in `$HOME` (conda-forge only), install `gh`, run `gh auth login`, clone this repo to `~/fps-agent/repo`. Confirm that `gh release list` works | done | Not needed: the cluster already has GitHub access (user, 2026-11-11). Still confirm `gh release list` works before the first build lands. **2026-11-11 (CLUSTER): `gh` is not installed on Cradle**. It will be installed from conda-forge into `la-train` (L-003), then the user runs `gh auth login`. |
| L-003 | 2026-11-11 | Spike E: write `cluster/env/la-train.yml` (Python 3.11, numpy, PyTorch CUDA build matching the driver, tensorboard), create the env, and check `torch.cuda.is_available()` in a GPU job. Report in `spikes/E-python-gpu.md` | open (after L-001) | |
| L-004 | 2026-11-11 | **Spike A run** with `build-cccd809`, following [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) → *Suggested CLUSTER procedure*: (1) a smoke test; (2) a ~150-iteration toy training run (2 games × 16 arenas); (3) a resume test with `-ResumeFrom`; (4) `-Infer` playback of the snapshots. **The `la-train` env also needs `smart_open`**: the trainer imports it at load (the engine pins 7.3.0). **Start the trainer first and never probe its port** (see the spike doc). Fill in *Cluster result* in the spike doc. If it fails, add a `C-xxx` request with the game/trainer log excerpt. | open (after L-001, L-003) | |

## Requests: CLUSTER → LOCAL
| ID | Date | Request | Status | Answer / link |
|---|---|---|---|---|
| C-001 | 2026-11-11 | Paths on Cradle changed (D-021: everything under `~/beanRant/`). Please update the two LOCAL-owned places that still use the old paths. (1) [`builds.md`](builds.md) → *Fetch (CLUSTER)*: change `~/fps-agent/builds/<sha>` to `~/beanRant/builds/<sha>`. (2) [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) → *Suggested CLUSTER procedure*: use `REPO=~/beanRant/FPS-Agent`, `BUILD=~/beanRant/builds/<sha>/Linux`, `RUN=~/beanRant/runs/<run-id>` and `source ~/miniconda3/bin/activate ~/beanRant/envs/la-train`. Low priority: CLUSTER follows [`cradle.md`](cradle.md) → *Our paths* meanwhile. | open | |
| C-002 | 2026-11-11 | Doc fix, low priority. [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) → *Tuning* and `cluster/learning_agents_python/README.md` say the Spike A values are in `DefaultGame.ini`. That file has no FPS Agent section; the values are the C++ defaults of `FReachTaskSettings` in `Common/FPSAgentTypes.h`, and `DefaultGame.ini` only holds overrides. Please say so, so CLUSTER knows where to read them. (The values match the spike doc: 3–12 m, 1 m radius, +1/m, −0.5/s, +5, 360 steps, snapshot every 10.) | open | |

## Runs
| Run ID | Build | Stage | Slurm job | Status | Report |
|---|---|---|---|---|---|
| | | | | | |

## Rules for this file
- Add requests at the bottom of the right table, using the next ID (`L-xxx` from LOCAL, `C-xxx` from CLUSTER).
- Only the side a request is addressed to changes its **Status** (`open` → `in progress` → `done` / `blocked`) and fills in **Answer / link**. Don't edit the other side's request text.
- Keep entries short and link to the detailed doc (spike report, run report, cradle.md).
