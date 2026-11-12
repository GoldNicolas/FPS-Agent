# Spike reports

One file per spike, named `<letter>-<short-name>.md` (e.g. `A-headless-external-trainer.md`). The side that runs the spike writes the report. Spikes A–E run on CLUSTER (A also needs a LOCAL build); F runs on LOCAL.

| Spike | Question | Owner | Report |
|---|---|---|---|
| A | Packaged 5.7 Learning Agents game trains on Cradle with `-nullrhi` against an external `train.py`; snapshots save and resume works | LOCAL build → CLUSTER run | [A-learning-agents-linux.md](A-learning-agents-linux.md) (build done; cluster run pending) |
| B | Steps per second against N processes × K arenas on one node; pick defaults | CLUSTER | |
| C | Fixed 60 Hz step and uncapped tick behave correctly headless | CLUSTER (LOCAL adds the C++ hooks) | |
| D | Game processes on other nodes connect to the trainer | CLUSTER | |
| E | Cradle GPU driver and our PyTorch build work together | CLUSTER | |
| F | Unreal-MCP `python-execute` builds a level from a JSON spec | LOCAL | |

## Template
```markdown
# Spike <X>: <title>
- Date / side: <date>, <LOCAL|CLUSTER>
- Build: build-<sha> (if any) · Slurm job(s): <ids>
- Question: ...
- Pass criterion: ...

## What I did
<commands / settings, copy-pasteable>

## Result
PASS | FAIL | PARTIAL, with the real output (trimmed)

## Findings / numbers

## Follow-ups
- requests added to STATUS.md: L-xxx / C-xxx
```
