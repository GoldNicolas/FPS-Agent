# Run reports

**Owner: CLUSTER side.** One file per training or evaluation run: `docs/runs/<run-id>.md`.

- **Run ID (D-033):** `<stage>-<mobility>-<YYYYMMDD>-<slurm-job-id>` (e.g. `S1-walk-20261114-48213`), mobility `static`, `walk` or `full`. It MUST match the run's `-Set` and the `mobility=` field of every game's `Combat: ready` line. `combat.sbatch` does both from `MOBILITY=walk|full|static` and fails a step on a mismatch. Sweeps: `<stage>-<mobility>-<sweep>-<arrayjob>_<index>`.
  - Older runs keep their IDs: `S0-…` = static, `S1-20261113-L009` = full.
- **Stays on Cradle, in `~/beanRant/runs/<run-id>/` (D-021):** full snapshots, logs, TensorBoard event files.
- **Goes in git:** this report plus small CSV metric summaries (< 1 MB) in `docs/runs/<run-id>/`.
- **Goes to GitHub Releases:** snapshots the LOCAL side should watch or evaluate, tagged `run-<run-id>`:
  `gh release create run-<run-id> <snapshot files> SHA256SUMS --prerelease --target <pushed commit> --notes "<summary>"`
  - On Cradle, `gh` is `~/beanRant/envs/la-train/bin/gh` (see `docs/cradle.md`).
  - Push the run report's commit first, so the tag points at it.
  - Afterwards, check the release by downloading it into an empty folder and running `sha256sum -c SHA256SUMS`.
  - First example: [`run-spikeA-20261111-151705`](spikeA-20261111-151705.md).

## Template
```markdown
# Run <run-id>
- Build: build-<sha> · Stage: S<n> · Slurm job: <id> · Node(s): <names> · Walltime: <h>
- Started from: <scratch | run-<id> snapshot>
- Config: N=<games> K=<arenas> seed=<...> reward weights=<...> PPO hyperparameters=<...>
- Release with snapshots: run-<run-id> (or "none")

## Outcome
<one paragraph: did it learn? promoted to the next stage?>

## Metrics (final / best)
| Metric | Value |
|---|---|
| Mean episode reward | |
| Kills per life | |
| Damage per life | |
| Headshot % | |
| Accuracy | |
| Steps/s (total) | |

## Problems / observations

## Next
- requests added to STATUS.md: C-xxx
```
