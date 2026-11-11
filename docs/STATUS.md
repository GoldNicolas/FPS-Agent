# Status board

The mailbox between the **LOCAL** side (Windows: game, builds) and the **CLUSTER** side (Cradle: training). Both sides read this first after `git pull`, and update it before every push. Protocol: see `CLAUDE.md` → *Two roles, one repo*.

_Last updated: 2026-11-11 by LOCAL_

## Current focus
- **LOCAL:** Step 2: first build and editor launch with Unreal-MCP connected. Then copy the Learning Agents Python files from UE 5.7.4, install the Linux target and the v26 toolchain, and build the Spike A toy game.
- **CLUSTER:** Bootstrap (requests L-001, L-002 below).

## Latest build
None yet. Builds are listed in [`builds.md`](builds.md) and published as GitHub Releases tagged `build-<sha>`.

## Requests: LOCAL → CLUSTER
| ID | Date | Request | Status | Answer / link |
|---|---|---|---|---|
| L-001 | 2026-11-11 | Recon: run the commands in [`cradle.md`](cradle.md), fill in every `TBD`, and list which questions for the admins are still open | open | |
| L-002 | 2026-11-11 | Bootstrap tools: Miniforge in `$HOME` (conda-forge only), install `gh`, run `gh auth login`, clone this repo to `~/fps-agent/repo`. Confirm that `gh release list` works | open | |
| L-003 | 2026-11-11 | Spike E: write `cluster/env/la-train.yml` (Python 3.11, numpy, PyTorch CUDA build matching the driver, tensorboard), create the env, and check `torch.cuda.is_available()` in a GPU job. Report in `spikes/E-python-gpu.md` | open (after L-001) | |

## Requests: CLUSTER → LOCAL
| ID | Date | Request | Status | Answer / link |
|---|---|---|---|---|
| | | | | |

## Runs
| Run ID | Build | Stage | Slurm job | Status | Report |
|---|---|---|---|---|---|
| | | | | | |

## Rules for this file
- Add requests at the bottom of the right table, using the next ID (`L-xxx` from LOCAL, `C-xxx` from CLUSTER).
- Only the side a request is addressed to changes its **Status** (`open` → `in progress` → `done` / `blocked`) and fills in **Answer / link**. Don't edit the other side's request text.
- Keep entries short and link to the detailed doc (spike report, run report, cradle.md).
