# Cradle cluster facts

**Owner: CLUSTER side.** Fill in every `TBD` from real command output, never from guesses. The LOCAL side and the job scripts rely on these values.

_Last verified: 2026-11-11 by CLUSTER (login001 + GPU job 151692 on gpu001)_

## Known from public docs (hpc.utrgv.edu, Nov 2026)
- 12 GPU nodes, Slurm, Rocky Linux 8.6, InfiniBand 200 Gb/s, WekaIO storage.
- Node types:

  | Type | CPU | Cores | RAM | GPUs |
  |---|---|---|---|---|
  | PCIe | 2× Xeon Gold 6330 | 56 | 640 GB | 8× A100 80GB |
  | SXM | 2× Xeon Gold 6330 | 56 | 640 GB | 8× A100 80GB |
  | A30 | 2× Xeon Gold 6330 | 56 | 1 TB | 6× A30 24GB |
  | L40S | 2× Xeon Platinum 8562Y+ | 64 | 1 TB | 8× L40S 48GB |

- Login: `login.cradle.utrgv.edu` (UTRGV VPN required).
- The sample script uses `-p kimq` and `--gres=gpu:1`.
- Preinstalled: PyTorch 1.10.2, CUDA 11.6. Too old; we use our own conda env.

## Recon commands (login node unless noted)
```bash
sinfo -o "%P %l %D %c %m %G %N"       # partitions, time limit, nodes, cores, mem, GPUs
scontrol show partition               # MaxTime, MaxNodes, QOS, AllowAccounts
sacctmgr show assoc user=$USER format=account,partition,qos,maxjobs,maxsubmit   # no output here: accounting is off
sshare -U                             # fair-share (empty here, same reason)
ldd --version | head -1               # glibc (expect 2.28)
df -h ~ ; quota -s 2>/dev/null        # home quota
mount | grep -i weka                  # Weka mount point(s)
which apptainer singularity 2>/dev/null
# In a GPU job:  srun -p <partition> --gres=gpu:1 -t 5 nvidia-smi
```

## Partitions (measured: `sinfo`, `scontrol show partition`, 2026-11-11)
Our user is in group `milab`, which unlocks `gpuq` and `sxmq`.

| Partition | Nodes | GPUs/node | Cores/node | MaxTime | Who may use it | Notes |
|---|---|---|---|---|---|---|
| `gpuq` | gpu[001-008] | 8× A100 80GB PCIe | 56 | unlimited | groups astro, admins, **milab** | PriorityTier 1000. Mostly idle on 2026-11-11 (gpu004 drained). **Our default.** |
| `sxmq` | sxm[001-004] | 8 (A100 SXM per public docs) | 56 | unlimited | groups admins, **milab**, csdev | PriorityTier 1000. Busy (56 running, 67 pending on 2026-11-11). |
| `kimq` (default) | sxm[003-004] | 8 | 56 | 12:00:00 | all | PriorityTier 1, so it waits behind `sxmq` on the same nodes. `DefCpuPerGPU=6`. |
| `gpua30q` | gpu[009-010] | 6 (A30) | 56 | 1-12:00:00 | all | Both nodes drained on 2026-11-11. |
| `gpul40q` | gpu[011-012] | 8 (L40S) | 64 | unlimited | all | gpu012 drained on 2026-11-11. |

- Slurm 21.08.8, `select/cons_tres` with **`CR_CORE`**: cores and GPUs are scheduled, **memory is not** (every node reports `RealMemory=1024000` MB, more than the public 640 GB for the A100 nodes). Still pass `--mem` in our scripts as documentation of what a job needs.
- `DefMemPerNode=UNLIMITED`, `DefaultTime=NONE`: always pass `-t` and `--mem`.
- **No accounting:** `AccountingStorageType=accounting_storage/none`. `sacct`, `sacctmgr` and `sshare` return nothing, so there are no QOS or per-user limits, and fair-share isn't tracked. Use `squeue` and the job's own logs instead of `sacct`.
- `PreemptType=preempt/none`, `MaxArraySize=1001`, `KillWait=30 s`.
- GRES has no type names (`gpu:8`), so pick the GPU type by partition, not by `--gres=gpu:a100:1`.

## Answers
| Item | Value |
|---|---|
| Partition(s) I can use | `gpuq` (default for us), `sxmq`, `kimq`, `gpua30q`, `gpul40q` (table above) |
| Max time limit per partition | `gpuq` / `sxmq` / `gpul40q` unlimited; `gpua30q` 1 d 12 h; `kimq` 12 h |
| Max nodes / GPUs per job and per user | No Slurm limit (`MaxNodes=UNLIMITED`, no QOS because accounting is off). Etiquette limit is an admin question. |
| Whole-node (`--exclusive`) allowed? | Slurm doesn't block it (`ExclusiveUser=NO`, `OverSubscribe=NO`). Whether it's acceptable is still an admin question. |
| GPU driver version / max CUDA (`nvidia-smi`) | **550.54.14 / CUDA 12.4** on gpu001 (`gpuq`, A100 80GB PCIe). Other partitions not checked yet. |
| glibc | 2.28 (Rocky Linux 8.6, kernel 4.18.0-372.9.1), login and compute nodes |
| Home path and quota | `/home/<user>` on NFS `riogrande:/home` (110 TB, 36 TB free). `quota -s` reports nothing usable. |
| Weka path for run outputs and quota | **Not used (D-021): run outputs go to `~/beanRant/runs/`.** For reference: `/weka/scratch` (wekafs, 80 TB, 44 TB free) is visible on compute nodes; there's no per-user directory, but `/weka/scratch/milab` exists (group `milab`, setgid, group-writable). Quota and purge policy unknown. |
| Outbound internet on login node / compute nodes | yes / yes (HTTPS to github.com and conda.anaconda.org from login001 and gpu001) |
| Node-to-node TCP on arbitrary ports (for multi-node) | TBD: untested (Spike D) |
| Apptainer available? | No `apptainer`; `/usr/bin/singularity` exists (not required) |
| Environment modules | Yes (`module avail`): `cuda/11.6`, `cuda/12.3`, `gcc/11.2.1`, `gcc/13.1.1`, OpenMPI, Intel oneAPI. We don't need them; our env brings its own CUDA runtime. |
| `gh` CLI | **Not installed** on login001. `git` reaches GitHub over HTTPS (`origin` = `https://github.com/GoldNicolas/FPS-Agent.git`). |
| Conda | Existing `~/miniconda3` (conda 25.7.0, default channel `defaults`); no Miniforge |

## Our paths on Cradle (D-021)
**Everything this project produces on Cradle stays under `~/beanRant/`** (user decision, 2026-11-11). These paths replace the `~/fps-agent/...`, `~/miniforge3` and `<weka>/...` paths in older docs and sketches.

```
~/beanRant/
├── FPS-Agent/          # this repo (git). Nothing below is ever committed.
├── builds/<sha>/       # gh release download build-<sha>; sha256sum -c; untar → Linux/
├── runs/<run-id>/      # one folder per run: trainer/ (snapshots, configs, TensorBoard), trainer.log, game_<i>.log
├── logs/               # Slurm stdout/stderr: #SBATCH --output=/home/%u/beanRant/logs/%x-%j.out
└── envs/la-train/      # conda prefix env from cluster/env/la-train.yml
```

| What | Path | State |
|---|---|---|
| Repo | `~/beanRant/FPS-Agent` | cloned |
| Builds | `~/beanRant/builds/<sha>/` | created on first download (L-004) |
| Run outputs | `~/beanRant/runs/<run-id>/` (run ID per `docs/runs/README.md`) | created by the first job |
| Slurm output | `~/beanRant/logs/%x-%j.out` | created before the first `sbatch` (Slurm doesn't create it) |
| Conda env `la-train` | `~/beanRant/envs/la-train`, created as a **prefix env** with the existing `~/miniconda3`: `conda env create -p ~/beanRant/envs/la-train -f cluster/env/la-train.yml`. Set `CONDA_PKGS_DIRS=~/beanRant/envs/.pkgs` while creating it so the downloaded packages also stay in `beanRant/` (delete that cache afterwards with `conda clean`). | L-003 |
| `gh` login | `~/.config/gh/` (the only file outside `beanRant/`, written by `gh auth login`) | after L-003 |

- **Use the env in scripts by full path:** `~/beanRant/envs/la-train/bin/python`, with `export PATH=~/beanRant/envs/la-train/bin:$PATH`. `source ~/miniconda3/bin/activate ~/beanRant/envs/la-train` is **not enough** on this account (see the shell trap below; Spike E job 151697).
- **conda-forge only:** the yml lists `channels: [conda-forge, nodefaults]`, because this account's conda config defaults to `defaults`.
- **Slurm `#SBATCH` lines don't expand `~` or `$HOME`.** Use `/home/%u/...` there.
- **Shell trap on this account:** login shells (and so Slurm jobs, which inherit them) start with:
  - `VIRTUAL_ENV` and the front of `PATH` pointing at another project's env (`~/miniconda3/envs/locomujoco`);
  - `PYTHONPATH=:/opt/phoenix-2.2.0/lib`.

  `conda activate` swaps only base's `PATH` entry, which sits *behind* locomujoco's, so `python` stays locomujoco's (torch 2.13 / CUDA 13, which fails on our driver). Job scripts must `unset PYTHONPATH PYTHONHOME VIRTUAL_ENV`, set `PYTHONNOUSERSITE=1`, and call la-train's python by full path. Set `VIRTUAL_ENV=$CONDA_PREFIX` before any `pip install`.

## Questions for the admins (still open)
- [ ] Max walltime is unlimited on `gpuq`. Is there a soft policy on job length or on how many GPUs and nodes one user may hold?
- [ ] Are whole-node (`--exclusive`) jobs acceptable on `gpuq`?
- [ ] Can compute nodes reach each other on arbitrary TCP ports? (We can also test this in Spike D.)
- [ ] What is the home (`/home`) quota? `quota -s` doesn't report it. **This matters most now**: builds, runs, logs and the conda env all live in home (D-021).
- [ ] Only if outputs outgrow home: is `/weka/scratch/milab/<user>/` the right place for ~10–100 GB of run outputs, and what are its quota and purge policy?
