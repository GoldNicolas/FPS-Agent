# Learning Agents trainer (Python), pinned to UE 5.7.4

Verbatim copies from the engine install, except for the one documented patch below. **Do not edit otherwise**: re-copy them when the engine version changes, then re-apply the patch (owner: LOCAL).

| Source (UE 5.7.4, CL 51494982) | Here |
|---|---|
| `Engine/Plugins/Experimental/LearningAgents/Content/Python/train.py` | `train.py` |
| `Engine/Plugins/Experimental/LearningAgents/Content/Python/learning_agents/**/*.py` | `learning_agents/` |
| `Engine/Plugins/Experimental/NNERuntimeBasicCpu/Content/Python/*.py` | `nne_runtime_basic_cpu/` |

## Local patch: shorter socket polls (D-029, C-007 (1))
`learning_agents/communicators/socket_communicator.py` polls in three places with a fixed 1-second sleep. They cost 1–2 s of every ~3.9 s training update (Spike B). Every patched line is marked `# FPS-Agent patch (D-029, C-007): was 1 s`.

| Line | Function | Epic 5.7.4 | Here | What it waits for |
|---|---|---|---|---|
| 549 | `receive_experience` | `time.sleep(1)` | `time.sleep(0.01)` | enough game buffers for one batch (`--batch-size`) |
| 665 | `_sender` | `time.sleep(1)` | `time.sleep(0.05)` | the next send round: new networks, or a heartbeat ping (now 20/s per game instead of 1/s) |
| 684 | `_worker` | `time.sleep(1)` | `time.sleep(0.01)` | a game to finish receiving the new networks |

- **Measured by CLUSTER** in Spike B, job 151931 (`docs/spikes/B-throughput.md`, the diff in `docs/spikes/B-throughput/trainer-poll-patch.diff`): 15–31% fewer seconds per batch, and all 75 games ran normally.
- The other three `time.sleep(1)` calls (lines 501–517) are left as Epic wrote them.
- **To undo:** set the three values back to `1`. Nothing else depends on them.
- **When the engine version changes:** re-copy the files, re-apply these three edits, and re-measure.

## Python packages the socket PPO path imports
- `numpy`, `torch`: always.
- `smart_open`: always (`train_common.py` imports it at module load). The engine pins `smart_open==7.3.0`.
- `tensorboard`: optional. The game asks for TensorBoard logging; when the package is missing, the trainer prints a warning and keeps training.
- `boto3` / `mlflow`: only for the File communicator / MLflow tracking, which we don't use.

**Versions Epic tests this trainer with.** These are what UE 5.7.4 pip-installs for its own bundled trainer:

| Package | Version | Pinned in |
|---|---|---|
| `torch` | `2.5.1+cu124` | `PythonMLPackages.uplugin` |
| `numpy` | `1.26.4` | `PythonFoundationPackages.uplugin` |
| `smart_open` | `7.3.0` | `LearningAgents.uplugin` |
| Python | 3.11.8 | bundled |

The code above has no calls that break on NumPy 2 (checked: no `np.float`, `np.int`, `np.bool` or `np.object`). It also uses no `torch.load`, `torch.jit` or AMP, so it doesn't depend on torch-version-specific APIs. Still, the engine pins are the safest choice.

## Suggested `la-train` specs
CLUSTER owns `cluster/env/la-train.yml`; this is a suggestion. LOCAL solved it on 2026-11-11 with `conda create --dry-run` for conda-forge linux-64, overriding the virtual packages to Cradle's values (`__glibc` 2.28, `__cuda` 12.4).

```yaml
channels: [conda-forge, nodefaults]
dependencies:
  - python=3.11
  - pytorch=2.5.1=cuda126*   # engine pin is 2.5.1+cu124; conda-forge has no cu124 build of 2.5.1
  - cuda-version=12.6        # keeps the CUDA runtime at 12.6 (unpinned, the solver takes 12.9)
  - numpy=1.26.4
  - smart_open==7.3.0        # "=7.3.0" would pick 7.3.0.post1
  - tensorboard
  - gh
```

It resolved to:

| Package | Version |
|---|---|
| `pytorch` | `2.5.1 cuda126_mkl` |
| `cuda-cudart` | `12.6.77` |
| `numpy` | `1.26.4` |
| `smart_open` | `7.3.0` |
| `tensorboard` | `2.20.0` |
| `gh` | `2.103.3` |

- **Create it with `CONDA_OVERRIDE_CUDA=12.4`** if the login node has no NVIDIA driver (`conda info` lists no `__cuda`). Without the override, conda sees no GPU:
  - an unpinned `pytorch` silently resolves to the **CPU** build (the dry run picked `pytorch-2.14.1-cpu_mkl`);
  - the pinned spec above fails to solve.
- **CUDA 12.6 runs on Cradle's driver** 550.54.14 (CUDA 12.4) through CUDA minor-version compatibility: any 12.x runtime works on a driver ≥ 525.
  - conda-forge allows this: `cuda-version 12.x` only requires `__cuda >=12`.
  - The A100 kernels (sm_80) are prebuilt, so no PTX JIT is needed.
- **Never take a CUDA 13 build.** The newest conda-forge pytorch (2.14.1) is `cuda134`, and those builds need driver ≥ 580.
- **Spike E check:** `torch.cuda.is_available()` alone isn't enough. Run a kernel in the GPU job:

  ```bash
  python -c "import torch; print(torch.__version__, torch.version.cuda, torch.cuda.get_device_name(0), (torch.ones(2, device='cuda') * 2).sum().item())"
  ```

  It should print `2.5.1`, `12.6`, the A100 name and `4.0`.

## How it is started (external trainer, socket mode)
```bash
python3 cluster/learning_agents_python/train.py FPSAgent -l \
  --nne-cpu-path cluster/learning_agents_python/nne_runtime_basic_cpu \
  Socket 127.0.0.1:$PORT "$RUN/trainer"
```
- `FPSAgent` is the task name. The trainer writes to `$RUN/trainer/FPSAgent<N>/` (`<N>` = first free index, usually 0):
  - `Snapshots/{policy,critic,encoder,decoder}.bin`: overwritten every `IterationsPerSnapshot` iterations.
  - `Configs/*.json`: the config the game sent.
- TensorBoard events go to `$RUN/trainer/TensorBoard/runs/`.
- PPO hyperparameters are **not** set here: the game sends them when it connects. Only the **first** game's config is used and saved.
  - They are the Learning Agents struct defaults, plus the few fields Spike A sets in `AReachTaskActor::SetupLearning` (`Game/FPSAgent/Source/FPSAgent/Learning/ReachTaskActor.cpp`).
  - The task values are the C++ defaults of `FReachTaskSettings` (`Source/FPSAgent/Common/FPSAgentTypes.h`).
  - `DefaultGame.ini` could override them under `[/Script/FPSAgent.FPSAgentSettings]` (`SpikeA=(...)`), but it holds no overrides today.
  - The expected values are listed in `docs/spikes/A-learning-agents-linux.md` → *Expected trainer config*.
- Device: the game asks for GPU. Without CUDA, the trainer logs `GPU does not support CUDA. Defaulting to CPU training.` and continues.
- Progress line printed per iteration with `-l`: `Iter: N | Avg Rew: ... | Avg Rew Sum: ... | Avg Return: ... | Avg Episode Len: ...`.

Resuming is done on the **game** side: start the game with `-ResumeFrom=<dir with the four .bin files>`. The game loads them before it connects, and the trainer starts from those weights. The optimizer state and iteration counter start fresh.
