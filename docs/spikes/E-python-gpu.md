# Spike E: the Cradle GPU driver and our PyTorch build work together
- Date / side: 2026-11-11, CLUSTER (request L-003)
- Build: none · Slurm job(s): 151697 (wrong Python, see finding 3) and **151700** (pass), both on `gpu001` (`gpuq`). An interrupted earlier session also reported passing jobs 151698 and 151699 with the same numbers. Slurm keeps no accounting here, so those two can't be re-checked; 151700 is the verified run.
- Question: does the `la-train` env's PyTorch run real work on Cradle's GPUs with the installed driver?
- Pass criterion (README → Roadmap): `torch.cuda.is_available()` in a job, and the trainer runs on the GPU. L-003 adds: run a real computation on the GPU, not just `is_available()`.

## What I did
1. Wrote [`cluster/env/la-train.yml`](../../cluster/env/la-train.yml) from LOCAL's suggested specs (`cluster/learning_agents_python/README.md` → *Suggested `la-train` specs*).
2. The login node has no `__cuda` virtual package (`conda info`), so the env was solved and created with `CONDA_OVERRIDE_CUDA=12.4`. A dry run first confirmed a CUDA build of `pytorch` (not `cpu_*`). Everything stays under `~/beanRant/` (D-021):
   ```bash
   export CONDA_PKGS_DIRS=~/beanRant/envs/.pkgs
   CONDA_OVERRIDE_CUDA=12.4 ~/miniconda3/bin/conda env create -p ~/beanRant/envs/la-train -f cluster/env/la-train.yml
   CONDA_PKGS_DIRS=~/beanRant/envs/.pkgs ~/miniconda3/bin/conda clean --tarballs --index-cache -y   # frees 3 GB of downloads
   ```
3. Ran [`cluster/scripts/gpu_check.py`](../../cluster/scripts/gpu_check.py) in a GPU job. It checks a 4096² fp32 matmul against a CPU float64 reference, a cuDNN GRU forward and backward pass against CPU, 300 Adam steps on a small MLP, and the trainer's imports:
   ```bash
   srun -p gpuq --gres=gpu:1 -c 4 --mem=16G -t 10 bash -c 'unset PYTHONPATH PYTHONHOME VIRTUAL_ENV; export PYTHONNOUSERSITE=1
     ~/beanRant/envs/la-train/bin/python -s cluster/scripts/gpu_check.py'
   ```

**Resolved and installed** (identical to LOCAL's dry run):

| Package | Version / build |
|---|---|
| `python` | 3.11.18 |
| `pytorch` | 2.5.1 `cuda126_mkl_py311_hcada2b2_317` |
| `cuda-version` / `cuda-cudart` | 12.6 / 12.6.77 |
| `cudnn` / `nccl` | 9.10.2.21 / 2.32.3.1 |
| `numpy` | 1.26.4 |
| `smart_open` | 7.3.0 |
| `tensorboard` | 2.20.0 |
| `gh` | 2.103.3 |

## Result
**PASS** (job 151700, 2026-11-11 22:49, final script):
```
NVIDIA A100 80GB PCIe, 550.54.14
python 3.11.18 (/home/<user>/beanRant/envs/la-train/bin/python) | torch 2.5.1 | CUDA build 12.6 | cuDNN 91002 | numpy 1.26.4
[PASS] cuda available: device count 1
device: NVIDIA A100 80GB PCIe | sm_80 | 79 GiB | arch list in this build: ['sm_50', ..., 'sm_80', 'sm_86', 'sm_89', 'sm_90', 'compute_90']
[PASS] matmul 4096x4096 fp32: relative error vs CPU float64 1.15e-06, 9.8 TFLOP/s
[PASS] GRU cuDNN fwd+bwd: max |GPU-CPU| output 6.0e-06, grad 9.1e-09
[PASS] Adam training on GPU: loss 3.767 -> 0.475 in 300 steps
[PASS] trainer imports: smart_open 0.0.0, tensorboard 2.20.0
peak GPU memory 328 MiB
RESULT: all checks passed
```
LOCAL's one-liner (same job) printed `2.5.1 12.6 NVIDIA A100 80GB PCIe 4.0`, as expected.

The second half of the pass criterion ("the trainer runs on the GPU") is checked in Spike A (L-004), from the trainer log and `nvidia-smi` samples taken during training.

## Findings / numbers
1. **A CUDA 12.6 runtime works on the 12.4 driver** (550.54.14), through minor-version compatibility. The build ships `sm_80` kernels for the A100, and the CUDA JIT cache stayed empty (`CUDA_CACHE_PATH=~/beanRant/.cache/nv`: the driver creates the folder, but 0 bytes were written), so no PTX was compiled at runtime.
2. **`CONDA_OVERRIDE_CUDA=12.4` is required** on the login node, which has no `__cuda`. With it, the pinned spec solves to the CUDA build.
3. **`conda activate` isn't enough on this account.** Job 151697 sourced `~/miniconda3/bin/activate ~/beanRant/envs/la-train`, but `python` still resolved to `~/miniconda3/envs/locomujoco/bin/python` (another project's env, first on the inherited `PATH`). That env's torch 2.13.0+cu130 then failed with `The NVIDIA driver on your system is too old (found version 12040)`. Our scripts now:
   - call `~/beanRant/envs/la-train/bin/python` by its full path;
   - put the env's `bin` first on `PATH`;
   - unset `PYTHONPATH` (the shell exports `:/opt/phoenix-2.2.0/lib`), `PYTHONHOME` and `VIRTUAL_ENV`.

   Details: `docs/cradle.md` → *Our paths*.
4. **`smart_open` reports version `0.0.0`.** Its Python metadata (`smart_open-0.0.0.dist-info`) says so in the conda-forge build, while conda installed package 7.3.0 (`pyhe01879c_0`). This is cosmetic: the trainer imports it fine.
5. **Disk:** the env takes 8.1 GB. The package cache (`~/beanRant/envs/.pkgs`) is down to 435 MB after `conda clean` removed 3 GB of archives.
6. **Speed** (not a benchmark): the fp32 matmul ran at 9.8 TFLOP/s (average of 10 calls after a first warm call). Peak GPU memory for the checks was 328 MiB.

## Follow-ups
- L-004 (Spike A) uses this env.
- `gh` 2.103.3 is in the env; `gh auth login` (by the user) is needed before downloading builds (L-002).
