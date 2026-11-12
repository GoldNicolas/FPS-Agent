# Learning Agents trainer (Python), pinned to UE 5.7.4

Verbatim copies from the engine install. **Do not edit**: re-copy them when the engine version changes (owner: LOCAL).

| Source (UE 5.7.4, CL 51494982) | Here |
|---|---|
| `Engine/Plugins/Experimental/LearningAgents/Content/Python/train.py` | `train.py` |
| `Engine/Plugins/Experimental/LearningAgents/Content/Python/learning_agents/**/*.py` | `learning_agents/` |
| `Engine/Plugins/Experimental/NNERuntimeBasicCpu/Content/Python/*.py` | `nne_runtime_basic_cpu/` |

## Python packages the socket PPO path imports
- `numpy`, `torch`: always.
- `smart_open`: always (`train_common.py` imports it at module load). The engine pins `smart_open==7.3.0`.
- `tensorboard`: optional. The game asks for TensorBoard logging; when the package is missing, the trainer prints a warning and keeps training.
- `boto3` / `mlflow`: only for the File communicator / MLflow tracking, which we don't use.

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
- PPO hyperparameters are **not** set here: the game sends them when it connects (`UFPSAgentSettings` → `SpikeA` in `Game/FPSAgent/Config/DefaultGame.ini`).
- Device: the game asks for GPU. Without CUDA, the trainer logs `GPU does not support CUDA. Defaulting to CPU training.` and continues.
- Progress line printed per iteration with `-l`: `Iter: N | Avg Rew: ... | Avg Rew Sum: ... | Avg Return: ... | Avg Episode Len: ...`.

Resuming is done on the **game** side: start the game with `-ResumeFrom=<dir with the four .bin files>`. The game loads them before it connects, and the trainer starts from those weights. The optimizer state and iteration counter start fresh.
