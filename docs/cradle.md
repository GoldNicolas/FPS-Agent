# Cradle cluster facts

**Owner: CLUSTER side.** Fill in every `TBD` from real command output, never from guesses. The LOCAL side and the job scripts rely on these values.

_Last verified: TBD (date + by whom)_

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
sacctmgr show assoc user=$USER format=account,partition,qos,maxjobs,maxsubmit
sshare -U                             # fair-share
ldd --version | head -1               # glibc (expect 2.28)
df -h ~ ; quota -s 2>/dev/null        # home quota
mount | grep -i weka                  # Weka mount point(s)
which apptainer singularity 2>/dev/null
# In a GPU job:  srun -p <partition> --gres=gpu:1 -t 5 nvidia-smi
```

## Answers
| Item | Value |
|---|---|
| Partition(s) I can use | TBD |
| Max time limit per partition | TBD |
| Max nodes / GPUs per job and per user | TBD |
| Whole-node (`--exclusive`) allowed? | TBD |
| GPU driver version / max CUDA (`nvidia-smi`) | TBD |
| glibc | TBD |
| Home path and quota | TBD |
| Weka path for run outputs and quota | TBD (planned: `<weka>/fps-agent/runs/`) |
| Outbound internet on login node / compute nodes | TBD / TBD |
| Node-to-node TCP on arbitrary ports (for multi-node) | TBD |
| Apptainer available? | TBD (not required) |

## Questions for the admins (still open)
- [ ] Max walltime and fair-share policy for our partition
- [ ] Are whole-node jobs acceptable?
- [ ] Can compute nodes reach each other on arbitrary TCP ports?
- [ ] Recommended Weka path and quota for ~10–100 GB of run outputs
