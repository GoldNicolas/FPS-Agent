# Run reports

**Owner: CLUSTER side.** One file per training or evaluation run: `docs/runs/<run-id>.md`.

- **Run ID:** `<stage>-<YYYYMMDD>-<slurm-job-id>` (e.g. `S0-20261120-48213`). Sweeps: `<stage>-<sweep>-<arrayjob>_<index>`.
- **Stays on Cradle, in `~/beanRant/runs/<run-id>/` (D-021):** full snapshots, logs, TensorBoard event files.
- **Goes in git:** this report plus small CSV metric summaries (< 1 MB) in `docs/runs/<run-id>/`.
- **Goes to GitHub Releases:** snapshots the LOCAL side should watch or evaluate, tagged `run-<run-id>`:
  `gh release create run-<run-id> <snapshot files> --prerelease --notes "<summary>"`

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
