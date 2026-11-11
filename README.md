# FPS-Agent

Train reinforcement-learning agents to move and shoot in a low-fidelity tactical-shooter environment built in **Unreal Engine 5.7**, using Epic's **Learning Agents** plugin, with training running **headless and in parallel on the UTRGV Cradle HPC cluster**.

The agents ("beans with guns") learn by trial and error (PPO) **only against scripted bots**. After training, a **deathmatch field test** pits the trained agents against each other on a full map to show how they perform. The long-term goal is to turn the trained policies into an in-game bot mechanic with tunable difficulty.

> **Status (2026-11-11):** Core sandbox done (Plan 1, [`docs/superpowers/plans/2026-11-11-core-sandbox.md`](docs/superpowers/plans/2026-11-11-core-sandbox.md)): bean characters, Valorant-like movement, hitscan rifle (damage per hitbox, spread, recoil, cadence, reload), health + armor, and a runtime-built shooting range with static and strafing bots. 20 automation tests pass headless (`-nullrhi`), and it was verified in play-in-editor through Unreal-MCP. Next: Plan 2 (original blockout maps from JSON specs) and Phase 0 spikes. **The live state of both sides is in [`docs/STATUS.md`](docs/STATUS.md).**

---

## Table of contents

1. [Goals and non-goals](#goals-and-non-goals)
2. [Architecture](#architecture)
3. [Tech stack and versions](#tech-stack-and-versions)
4. [Repository layout](#repository-layout)
5. [Game environment](#game-environment)
6. [Maps](#maps)
7. [RL design](#rl-design)
8. [Evaluation: deathmatch field test](#evaluation-deathmatch-field-test)
9. [Collaboration workflow: LOCAL ↔ CLUSTER via GitHub](#collaboration-workflow-local--cluster-via-github)
10. [Windows dev setup](#windows-dev-setup)
11. [Cluster: running on Cradle](#cluster-running-on-cradle)
12. [Roadmap](#roadmap)
13. [Open questions and things to verify](#open-questions-and-things-to-verify)
14. [References](#references)

---

## Goals and non-goals

**Goals**
- A tactical-shooter training environment whose *mechanics* closely match Valorant: movement speeds, counter-strafe accuracy, rifle damage per hitbox, fire rate, recoil and moving inaccuracy.
- Agents that learn to find, aim at and shoot enemies, and to avoid taking damage, by interacting with the environment.
- Fast training: many headless game processes feeding a single trainer, plus Slurm job arrays that run independent experiments side by side.
- A deathmatch field test between trained agents.
- Policies that can later run inside a shipped game, with knobs to make them more human-like.

**Non-goals**
- **No computer vision.** Agents perceive the world through ray casts and line-of-sight checks only. No camera images, no rendering.
- **No self-play training.** Agents never train against other agents. Agent-vs-agent play happens only in the deathmatch evaluation.
- **No copies of Riot's maps, names or assets.** Maps are original designs that follow the *structural conventions* of the genre (see [Maps](#maps)). Mechanics are emulated with our own tunable data.
- No abilities, economy, bomb plant or round structure (for now).
- **No local training.** All training (and large-scale evaluation) runs on Cradle. The Windows machine builds the game, play-tests mechanics in the editor, packages Linux builds, and replays trained snapshots for viewing.

---

## Architecture

```
┌──────────────────────── Windows dev machine ────────────────────────┐
│                                                                      │
│  Claude Code ──MCP──► Unreal-MCP plugin ──► UE 5.7 Editor            │
│      │                 (RQL, C++ source+compile, python-execute)     │
│      │                                                               │
│      ├── edits C++ (Source/) ──► compile                             │
│      ├── maps/specs/*.json ──► tools/mapgen builder (python-execute) │
│      │                          ──► levels (.umap)                   │
│      └── tools/package_linux ──► cross-compile (v26 toolchain)       │
│                                   ──► Linux build (tarball)          │
└───────────────────────────────────────┬──────────────────────────────┘
                                        │ GitHub (private repo):
                                        │  git = code + markdown docs
                                        │  Releases = builds / snapshots
┌───────────────────────────────────────▼──── Cradle (Slurm) ─────────┐
│  One training job = one node (or more):                              │
│                                                                      │
│   train.py (PPO, GPU) ◄──socket──┬── FPSAgent.sh -nullrhi  (game 0)  │
│        │                         ├── FPSAgent.sh -nullrhi  (game 1)  │
│        │                         └── ... N headless game processes   │
│        │                              each = K isolated arenas       │
│        ▼                              (1 agent + its bots each)      │
│   snapshots/ + logs  (Weka storage)                                  │
│                                                                      │
│  Job arrays: seeds / reward weights / hyperparameters in parallel    │
└───────────────────────────────────────┬──────────────────────────────┘
                                        │ run reports (git) +
                                        │ snapshots (Release run-<id>)
                                        ▼
        Deathmatch field test: rendered on Windows (showcase)
        or headless on Cradle (stats)
```

**Why this works without OpenGL:** observations are ray casts and line-of-sight checks, so the game runs with `-nullrhi`, which creates no graphics device at all. Physics, collision, traces and character movement keep working; only drawing is skipped.

---

## Tech stack and versions

| Component | Choice | Notes |
|---|---|---|
| Engine | **Unreal Engine 5.7** | Multi-process Learning Agents training needs 5.6+. Unreal-MCP is CI-tested on 5.7. |
| RL | **Learning Agents** (Epic, experimental) | PPO trainer plus behavior cloning. The Python trainer lives in the engine's plugin folder. |
| Gameplay code | **C++** | Blueprints only for thin data and asset wiring. |
| Editor automation | **[Unreal-MCP](https://github.com/IvanMurzak/Unreal-MCP)** (IvanMurzak, beta) | Editor-only. Used on Windows, never on the cluster. |
| Linux build | Cross-compile from Windows, **toolchain v26 (clang 20.1.8, Rocky Linux 8 sysroot)** | Matches Cradle's Rocky Linux 8.6. |
| Cluster | **UTRGV Cradle**: Slurm, Rocky Linux 8.6 | 12 GPU nodes (A100 / A30 / L40S), 56–64 cores per node. |
| Trainer Python | Miniforge env in `$HOME` with a current PyTorch | Cradle's preinstalled PyTorch 1.10.2 / CUDA 11.6 is too old. |
| Fallback RL stack | [AMD Schola 2.1](https://gpuopen.com/manuals/schola/guides/distributed_training/) (UE 5.5–5.7, MIT) | Only if Epic's trainer becomes limiting. The RL layer is kept separate so this swap stays possible. |

---

## Repository layout

Planned layout. So far the docs, `.mcp.json`, git config and the project scaffold exist. The scaffold is `Game/FPSAgent`: the `.uproject` and an empty `FPSAgent` module. `Plugins/UnrealMCP` is installed locally but not committed.

```
FPS-Agent/
├── README.md                  # this file: design + runbook
├── CLAUDE.md                  # rules for both Claude sessions (LOCAL + CLUSTER roles)
├── PLAN.md                    # original idea notes
├── .mcp.json                  # Claude Code → local Unreal-MCP server
├── .gitignore / .gitattributes  # LF for cluster scripts, LFS for .uasset/.umap
├── Game/FPSAgent/             # UE 5.7 C++ project (FPSAgent.uproject)
│   ├── Source/FPSAgent/
│   │   ├── Character/         # ABeanCharacter, movement tuning, hitboxes
│   │   ├── Weapons/           # hitscan rifle, URifleStats DataAsset, recoil/spread
│   │   ├── Bots/              # scripted range bots, behaviors, difficulty
│   │   ├── Arena/             # arena instances, spawn zones, episode reset, randomization, seeds
│   │   ├── Learning/          # Learning Agents manager/interactor/environment/trainer setup
│   │   └── Eval/              # deathmatch game mode, metrics logging
│   ├── Config/                # DefaultEngine.ini, DefaultGame.ini
│   └── Content/Data/          # DataAssets / DataTables (mechanics values)
├── maps/specs/                # JSON map layout specs (source of truth for levels)
├── tools/
│   ├── mapgen/                # spec validator, procedural generator, in-editor builder
│   └── package_linux.ps1      # RunUAT BuildCookRun wrapper for Linux
├── cluster/
│   ├── env/                   # Miniforge env setup for the trainer
│   ├── learning_agents_python/  # Learning Agents + NNE Python copied from the engine, version-pinned
│   ├── slurm/                 # train_single_node.sbatch, train_multi_node.sbatch, sweep_array.sbatch, eval.sbatch
│   ├── sweeps/                # sweep definitions (array index → params)
│   └── scripts/               # launch_games.sh, helpers
└── docs/
    ├── STATUS.md              # the mailbox: requests both ways, latest build, runs
    ├── decisions.md           # append-only decision log
    ├── builds.md              # build log (LOCAL)
    ├── cradle.md              # cluster facts (CLUSTER)
    ├── spikes/                # spike reports
    └── runs/                  # run reports (CLUSTER)
```

Version control: a private GitHub repo with a single `main` branch. **Git LFS** handles `*.uasset` and `*.umap`. Builds and snapshots go to GitHub Releases, never git. See [Collaboration workflow](#collaboration-workflow-local--cluster-via-github).

---

## Game environment

### Characters: beans with guns
- A capsule body, a **sphere head hitbox** and a **box legs hitbox**, all simple primitives.
- **No skeletal mesh in the training build.** Under `-nullrhi`, animation and bone updates can stop running, so hitboxes attached to bones would be wrong. Primitive hitboxes avoid that problem completely.
- Eye height and head position are fixed offsets on the capsule (crouch lowers both).

### Movement (Valorant-like)
All values live in a DataAsset, never hard-coded. UE units are centimetres.

| Parameter | Starting value | Confidence |
|---|---|---|
| Run speed (rifle equipped) | ~5.4 m/s (540 uu/s) | community-sourced, **verify** |
| Run speed (knife) | 6.75 m/s | community-sourced, **verify** |
| Walk speed | TBD (roughly half of run) | **unknown, verify** |
| Crouch speed | TBD | **unknown, verify** |
| Accurate-while-moving threshold | ≤ 27.5% of max speed | secondary source, **verify** |
| Acceleration / braking | High ("snappy" counter-strafe) | tune by feel |
| Horizontal FOV (for LOS checks) | 103° | **verify** |

### Rifle (Vandal-like stats, generic name)

| Parameter | Starting value | Confidence |
|---|---|---|
| Damage: head / body / legs | 160 / 40 / 34, no falloff | 2025–26 guides, **verify** |
| Fire rate | 9.75 rounds/s (ADS 8.775) | guides, **verify** |
| Magazine | 25 | **verify** |
| Reload | ~2.5 s | **verify** |
| First-shot spread | 0.25° | guide, **verify** |
| Recoil | Our own pattern: first few shots near-accurate, then vertical climb, then horizontal sway. Recovers when you stop firing. | original design |
| Moving / jumping spread | Large spread above the movement threshold, even larger in the air | tune |

Health: **100 HP + 50 armor**, so one headshot (160) kills from full. The armor absorption model is a config option.

The weapon is **hitscan**. Recoil and spread use a **seeded RNG per episode**, so runs are reproducible.

### Scripted bots (training opponents)
Bots are modelled on a shooting-range setup but grow into real opponents as training progresses:

| Mode | Behavior |
|---|---|
| Static | Stands still |
| Strafe | ADAD strafing with a random period |
| Peek | Peeks out from cover and back |
| Crouch | Crouch-spams |
| Shooter | Any of the above, and shoots back |
| Hunter | Roams the nav mesh, holds angles, engages on sight. The most agent-like bot, there to prepare agents for the deathmatch. |

Difficulty settings are reaction time, aim error (degrees), tracking speed, burst discipline, strafe frequency and HP. They are randomized within each curriculum stage's range.

### Arenas and episodes
- Each game process hosts **K isolated arena instances**, spaced far apart in the same world. Each arena holds **one learning agent plus its bots**.
- Agents never observe or damage other agents during training.
- **Per episode, randomize:** map variant, agent spawn (inside "safe" spawn zones that are out of line of sight of the bots at spawn time), bot count, bot positions, bot modes and bot difficulty.
- **An episode ends** when the agent dies, when all bots are dead, or on timeout (for example 60 s). Each arena resets on its own.
- **Fixed timestep:** physics and gameplay tick at a fixed 60 Hz and run as fast as the CPU allows (validated in Spike C).

---

## Maps

**Rule: original maps only.** We do not reproduce Riot's map layouts (Ascent, Bind, Haven, ...), not even approximately, and we use no Riot names or assets. We do follow the genre's structural conventions:
- 2–3 sites connected by lanes, plus a mid that connects them
- Chokes, corners and angles to hold, with off-angles and cover
- Engagement distances in roughly genre-typical proportions: close (< 10 m), mid (10–25 m), long (25–50 m)

This is also better for RL. Randomized original layouts keep the agent from memorizing a single map.

### Pipeline
1. **JSON layout spec** (`maps/specs/<name>.json`) is the source of truth. It lists a grid or polygon blockout of floors, walls, boxes and ramps, plus tagged regions: `site_a`, `mid`, `spawn_zone_*`, `choke_*`.
2. **Validator** (`tools/mapgen`) checks each spec before it is built:
   - every region is reachable,
   - the site count is right,
   - spawn zones are not in line of sight of each other,
   - the sightline-length histogram falls inside the target bands.
3. **Procedural generator** produces randomized variant specs from templates (lane count, site shape, cover density).
4. **Runtime builder (C++).** `FPSArena` spawns level geometry from data when the game starts, in the editor and headless on Cradle alike, so procedural variants need no rebuild or `.umap` (decision D-011). The shooting range already works this way.
5. **Unreal-MCP's role:** preview, play-test (`StartPlay` / `AdvancePlay`) and screenshot levels in the editor, and inspect actors with RQL.

---

## RL design

### Observations (fair: no information through walls)
| Group | Contents |
|---|---|
| Self | Velocity (local frame), grounded, crouched, health, armor, ammo fraction, reloading, current spread, recoil state / time since last shot, aim pitch |
| Geometry | Fan of about 72 ray casts from eye height (e.g. 24 yaw × 3 pitch), normalized distance and hit type |
| Enemies (set, up to 5) | Only enemies that pass a **line-of-sight check** (traces to head and chest, inside the 103° FOV), or were seen in the last ~2 s (flagged as "remembered"). For each: relative position in the local frame, distance, **aim error to the head** (yaw and pitch), relative velocity, whether it is facing the agent |
| Optional, later | Directional footstep sound cues within a radius |

### Actions
- Movement on 2 continuous axes (forward/back, left/right)
- Walk, crouch, jump (on/off)
- Aim: change in yaw and pitch, **capped by a maximum turn-rate setting**
- Fire, reload (on/off)
- The agent decides **15–20 times per second**; physics runs at a fixed 60 Hz.

### Human-likeness settings (the "staged" end goal)
Max turn rate, reaction delay (observations delayed by N ms) and aim noise. All are configurable. Agents train with fair observations, and these settings can be relaxed for research runs or tightened for in-game difficulty levels.

### Reward (revised from PLAN.md)
| Term | Value | Why |
|---|---|---|
| Damage dealt | `+ damage / 150` | The damage table already makes headshots worth 4×, so no separate headshot bonus is needed |
| Kill | `+1.0` | |
| Shot that hits nothing | small `−` | Discourages spraying without making "never shoot" optimal |
| First sighting of each enemy per life | `+0.2` | The "finding bots" term, computed with line-of-sight traces (no CV). Only once per enemy per life, so it can't be farmed |
| Damage taken | `− damage / 150` | |
| Death | `−1.0` | |
| Each step | tiny `−` | Stops the agent from camping forever |

**Accuracy and headshot percentage are logged metrics, not rewards.** Rewarding the accuracy ratio directly teaches the agent to stop shooting: no shots means no misses.

Every weight lives in config, so sweeps can tune them.

### Curriculum (training against bots only)
| Stage | Setup | Learns |
|---|---|---|
| **S0 Aim gallery** | Agent can't move. Static bots at random distances and angles. | Aim and timing of shots |
| **S1 Movers** | Strafe / peek / crouch bots. Agent can move. | Tracking. Counter-strafing (stop before shooting to be accurate) |
| **S2 Duels** | Bots shoot back. Difficulty ramps (reaction time 600 → 200 ms) | Trading damage, using cover, peeking |
| **S3 Full-map hunt** | Random map variants, random spawns, several bots including Hunters | Searching for and finding enemies, angles, fighting while moving |

Each stage is a new training run that starts from the previous stage's best snapshot. A stage is promoted when it hits a threshold (for example kill rate and damage per life), measured against a fixed set of evaluation seeds.

### Learning Agents classes (C++)
The Epic course uses Blueprints on 5.5; we port to C++ on 5.7. The expected classes are `ULearningAgentsManager`, `ULearningAgentsInteractor`, `ULearningAgentsTrainingEnvironment`, `ULearningAgentsPolicy`, `ULearningAgentsCritic`, `ULearningAgentsPPOTrainer` and `ULearningAgentsCommunicatorLibrary`. **Verify names and signatures against the 5.7 headers** before writing code. The course author says the 5.5 → 5.6 changes were minor.

---

## Evaluation: deathmatch field test

The deathmatch is **not training**. It is a showcase and field test of agents that have finished training.

- **Setup:** a full original map, free-for-all, 6–10 agents with respawns. Each agent runs a chosen snapshot **with training switched off**. Snapshots can come from the same run, different stages, or different sweep runs.
- **Modes:**
  - **Showcase:** rendered on Windows with a spectator camera, for watching and recording.
  - **Stats:** headless on Cradle (`eval.sbatch`), many matches with different seeds.
- **Metrics:** K/D, damage per life, headshot %, accuracy, time-to-kill, first-contact win rate and survival time. There is also a pairwise win matrix and an **Elo** rating across snapshots and runs.
- **Bot ladder:** the same metrics against fixed-difficulty bots, as a stable benchmark that is easy to compare across runs.
- **Expect surprises.** Agents never faced agents in training, so the deathmatch is a new situation for them. Those surprises are the point of the field test. Hunter bots in S3 are there to make the jump smaller.

---

## Collaboration workflow: LOCAL ↔ CLUSTER via GitHub

Two Claude Code sessions (and you) work in parallel: **LOCAL** on Windows (game, builds) and **CLUSTER** on Cradle (training). They never talk directly. The cluster's security policy means the LOCAL session never connects to Cradle. **The private GitHub repo is the only channel**:

```
 LOCAL (Windows)                         GitHub                          CLUSTER (Cradle)
 ───────────────                ──────────────────────────               ────────────────
 code, docs, requests  ──push──►  main branch (markdown,   ◄──pull──  reads requests, runs jobs
 Linux build ──gh release──►      code, small CSVs)        ──pull──►  writes run/spike reports,
                                  Releases: build-<sha>     ◄──push──  cradle.md, STATUS.md
 watches agents  ◄──download──    Releases: run-<run-id>    ◄──gh release── snapshots
```

- **The mailbox:** [`docs/STATUS.md`](docs/STATUS.md) holds requests in both directions (`L-xxx` from LOCAL, `C-xxx` from CLUSTER), the latest build and the runs table.
- **Reports:** builds go in [`docs/builds.md`](docs/builds.md), cluster facts in [`docs/cradle.md`](docs/cradle.md), spikes in [`docs/spikes/`](docs/spikes/), runs in [`docs/runs/`](docs/runs/), and design changes in [`docs/decisions.md`](docs/decisions.md).
- **Rhythm:** pull at the start of a session; after each advancement, update the docs, commit with a `[local]` / `[cluster]` prefix, then pull with rebase and push. Never force-push.
- **Ownership:** each side only edits its own files (table in `CLAUDE.md`), so the two sides rarely conflict.

---

## Windows dev setup

| Requirement | Status / how |
|---|---|
| Unreal Engine 5.7 (Epic Launcher) | Installed (5.7.4). UE 5.8 is also installed but unused. |
| C++ compiler: **Visual Studio 2022 Build Tools** (17.8+), "Desktop development with C++" workload (MSVC 14.38+, Windows SDK 10.0.22621+) **plus the .NET Framework 4.8 SDK** component (`Microsoft.Net.Component.4.8.SDK`; without it UnrealBuildTool fails with "Could not find NetFxSDK install dir") | Installed: 17.14, MSVC 14.44, SDK 10.0.26100, NetFx SDK 4.8. VS 2019 isn't supported by UE 5.7. VS Code works as the editor, but it doesn't contain a compiler, so the MSVC toolchain must be installed underneath it. |
| Node.js ≥ 22.12 | Installed (22.19), for `unreal-mcp-cli` |
| `unreal-mcp-cli` | Installed globally with `npm install -g unreal-mcp-cli` (0.30.0) |
| Linux target platform + v26 cross-compile toolchain | Not installed yet. Needed only for cluster builds (Phase 3 / Spike A). |

### Unreal-MCP (local mode)
- **Plugin:** `Game/FPSAgent/Plugins/UnrealMCP` (0.30.0). The Windows bridge sidecar comes bundled, so no .NET SDK is needed.
- **Editor-only:** the `.uproject` lists the plugin with `TargetDenyList: ["Game","Client","Server"]`, so it is **never compiled into game builds**, including the Linux cluster build.
- **Local server:** `Game/FPSAgent/.env` sets `UNREAL_MCP_CONNECTION_MODE=Custom`, `UNREAL_MCP_START_SERVER=true` and `UNREAL_MCP_TRANSPORT=http`.
  - Opening the editor starts the local `gamedev-mcp-server` (in `Intermediate/UnrealMCP/server/`) on `http://localhost:24268`.
  - No ai-game.dev account is needed, and nothing leaves the machine.
- **Claude Code config:** `.mcp.json` at the repo root (a copy lives in `Game/FPSAgent/`) points at `http://localhost:24268/mcp/p/d2699ae8`. Restart Claude Code after the editor is running, and approve the `unreal-mcp` server when prompted.
- **Check the connection:** `unreal-mcp-cli status -p Game/FPSAgent`. It shows `reachable: yes` while the editor is open.
- **Open the editor:** `unreal-mcp-cli open Game/FPSAgent`, which also builds when needed. For automated checks, use `--headless` (windowless, CLI-owned) and close it with `unreal-mcp-cli close Game/FPSAgent --owned-only`. CLI 0.30.0 has no `compile` or `test` commands; use the scripts below.
- **Update:** close the editor, then run `unreal-mcp-cli update Game/FPSAgent`.
- **Fresh clone:** the plugin folder is not in git (about 580 MB of binaries), so reinstall it before opening the project:
  ```bash
  unreal-mcp-cli install-plugin Game/FPSAgent --version 0.30.0 --with-server
  unreal-mcp-cli configure -p Game/FPSAgent --mode Custom --start-server --transport http
  ```
  The repo-root `.mcp.json` is committed and doesn't need regenerating. If the port in `unreal-mcp-cli status` differs, re-run `unreal-mcp-cli setup-mcp claude-code -p Game/FPSAgent` and copy the file to the repo root.

### Build and test (editor closed)
- **Build:** `powershell -File tools/build.ps1`. It caps parallel compiles at 4 (`-Jobs`) because the default ran a 16 GB machine out of memory. The first build takes about 3–10 min; incremental builds take about 1 min.
- **Tests (headless, `-nullrhi`):** `powershell -File tools/run_tests.ps1` prints each test. Exit 0 means all passed.
- **Play-test:** open the editor and press Play.
  - Controls: WASD move, mouse look, LMB fire, Shift walk, Ctrl crouch, Space jump, R reload.
  - Console: `ShowHitboxes 1` draws hitboxes.
  - The HUD shows HP, armor, ammo, speed and the current spread.
- **Automated play-in-editor check via MCP:**
  ```bash
  unreal-mcp-cli open Game/FPSAgent --headless --no-build
  unreal-mcp-cli run-tool rql-mutate -p Game/FPSAgent --input '{"query":"editor | call StartPlay()"}'
  unreal-mcp-cli run-tool rql-mutate -p Game/FPSAgent --input '{"query":"editor | call AdvancePlay(1.0, 0, \"W\")"}'
  unreal-mcp-cli run-tool rql-query  -p Game/FPSAgent --input '{"query":"actors | where $ is RangeBot | select Label Location bHidden"}'
  unreal-mcp-cli run-tool rql-mutate -p Game/FPSAgent --input '{"query":"editor | call StopPlay()"}'
  unreal-mcp-cli close Game/FPSAgent --owned-only
  ```
- **Tuning:** Project Settings → Game → FPS Agent (saved to `Game/FPSAgent/Config/DefaultGame.ini`).
- **Gotcha (UE 5.7):** `FVector`, `FVector2D` and `FRotator` hold doubles. Cast to `float` before `TestEqual(..., float, tolerance)`, or the call is ambiguous.

---

## Cluster: running on Cradle

### Facts (from [hpc.utrgv.edu](https://hpc.utrgv.edu/specifications))
- 12 nodes. Node types:

  | Node type | GPUs per node | CPU cores | RAM |
  |---|---|---|---|
  | PCIe | 8× A100 80GB | 56 | 640 GB |
  | SXM | 8× A100 80GB | 56 | 640 GB |
  | A30 | 6× A30 | 56 | 1 TB |
  | L40S | 8× L40S | 64 | 1 TB |

- Rocky Linux 8.6, Slurm, 200 Gb/s InfiniBand, WekaIO parallel storage.
- Login: `ssh <user>@login.cradle.utrgv.edu`, **over the UTRGV VPN**.
- Access: request through UTRGV IT with a faculty supervisor. You already have it.
- The sample job script uses `-p kimq` and `--gres=gpu:1`. The real partition names and time limits are not published.
- No module system or containers are documented. **We don't need containers**: the packaged Linux game is self-contained and runs straight from disk.
- It is a small, shared cluster. Be considerate: whole-node jobs are expensive for other users, so check fair-share policy with the admins.

Note: the older page at `utrgv.edu/hpcc` (55 nodes, 4 GB RAM each) is out of date.

### Prep checklist (in order)
| # | Step | Where | Notes |
|---|---|---|---|
| 1 | VPN + SSH login (you); Miniforge + `gh`; `gh auth login`; clone the repo to `~/fps-agent/repo`; start Claude Code there | Cradle login node | The CLUSTER session takes over from here. The LOCAL session never connects to Cradle. |
| 2 | Recon (below), plus questions for the admins | Cradle login node | Record the answers in `docs/cradle.md` |
| 3 | Miniforge + `la-train` env from `cluster/env/la-train.yml` | Login node (has internet) | conda-forge only. Pick the PyTorch CUDA build after seeing `nvidia-smi` in a GPU job |
| 4 | Spike E: GPU job runs `torch.cuda.is_available()` | Compute node | Confirms the driver and PyTorch work together |
| 5 | Copy the Learning Agents + NNE Python files from UE 5.7.4 into `cluster/learning_agents_python/` and commit | LOCAL → git → CLUSTER | Pinned to the engine version. Cradle has no UE install, so these must come through the repo. |
| 6 | Install the Linux target platform (Epic Launcher) and the v26 toolchain; set `LINUX_MULTIARCH_ROOT` | Windows | Needed to package for Linux |
| 7 | Spike A: minimal Learning Agents game, packaged for Linux, trains on Cradle against an external `train.py`; test resume | Both | The first end-to-end proof |
| 8 | Spikes C, B, D: fixed timestep, throughput (N × K), multi-node | Cradle | Set the default job sizes |
| 9 | Write the real `train_single_node`, `sweep_array`, `train_multi_node` and `eval` job scripts | Repo → Cradle | Built from the spike findings |

**Questions for the Cradle admins:**
- What is the maximum time limit for each partition?
- Are whole-node (`--exclusive`) jobs allowed? What are the fair-share limits?
- Can processes on different compute nodes connect to each other on arbitrary TCP ports?
- Do compute nodes have outbound internet access?
- What is the Weka path for run outputs, and what are the home and Weka quotas?
- Is Apptainer available? (Not needed, but good to know.)

### One-time recon (run on the login node)
```bash
sinfo -o "%P %l %D %c %m %G"          # partitions, time limits, nodes, cores, mem, GPUs
scontrol show partition               # full limits (MaxTime, MaxNodes, QOS)
sacctmgr show assoc user=$USER        # your account / QOS
nvidia-smi                            # (inside a GPU job) driver + max CUDA version
ldd --version                         # glibc (expect 2.28)
df -h ~ ; ls /                        # home quota, find the Weka mount for run outputs
```
Record the answers in `docs/cradle.md`.

Note: Epic warns that glibc older than 2.35 makes the engine **start more slowly**, because `dlopen` is slower. Rocky 8 ships glibc 2.28, so expect a slower startup only. It's a one-time cost per process.

### Trainer environment (one time)
1. Install Miniforge in `$HOME`.
2. Create the environment: `conda create -n la-train python=3.11 numpy`.
3. Install the PyTorch build that matches the driver's CUDA version (from `nvidia-smi`), plus `tensorboard` or `mlflow`.
4. Copy the trainer's Python files **from your UE 5.7 install** into `cluster/learning_agents_python/`, pinned to that engine version:
   - `Engine/Plugins/Experimental/LearningAgents/Content/Python/`
   - `Engine/Plugins/Experimental/NNERuntimeBasicCpu/Content/Python/`

   Verify the exact paths in 5.7.

The policy and critic networks are small multilayer perceptrons, so the GPU mostly helps with large batches when many game processes are connected. The trainer also runs on CPU.

### Build and ship (Windows)
1. Install the **v26 cross-compile toolchain** ([download](https://cdn.unrealengine.com/CrossToolchain_Linux/v26_clang-20.1.8-rockylinux8.exe)) and check that `LINUX_MULTIARCH_ROOT` is set. Make sure the Linux target platform is installed in the Epic Launcher options.
2. Package:
   ```powershell
   & "<UE_5.7>\Engine\Build\BatchFiles\RunUAT.bat" BuildCookRun `
     -project="<repo>\Game\FPSAgent\FPSAgent.uproject" -platform=Linux `
     -clientconfig=Development -build -cook -stage -pak -archive `
     -archivedirectory="<repo>\Builds\Linux" -noP4 -utf8output
   ```
   Development config, with the Learning Agents training modules included.
3. Make a tarball plus `SHA256SUMS` and publish it as GitHub Release `build-<short-sha>`. Log it in [`docs/builds.md`](docs/builds.md) and add a request in `STATUS.md`. The CLUSTER side downloads it with `gh release download` into `~/fps-agent/builds/<sha>/` and checks the checksum.

### How a training job works
These are the official Learning Agents scale-out flags from Epic's *Scaling Out with Learning Agents* doc. The job script is an **untested sketch** until Spikes A–D pass.

```bash
#!/bin/bash
#SBATCH --job-name=fpsagent-S0
#SBATCH -N 1
#SBATCH -p <partition>              # from recon
#SBATCH --gres=gpu:1
#SBATCH --cpus-per-task=<cores>     # whole node if allowed
#SBATCH --time=<= partition max>
#SBATCH --signal=B:USR1@300         # warning before walltime, so we can stop cleanly
#SBATCH --output=logs/%x-%j.out
set -euo pipefail

BUILD=~/fps-agent/builds/$BUILD_SHA/Linux
RUN=<weka>/fps-agent/runs/$SLURM_JOB_ID
PORT=$((40000 + SLURM_JOB_ID % 20000))     # unique per job: no collisions
NUM_GAMES=${NUM_GAMES:-16}                  # ~3 cores per game process (tune in Spike B)
ARENAS=${ARENAS:-16}                        # arenas (= agents) per process (tune in Spike B)

source ~/miniforge3/bin/activate la-train
python3 cluster/learning_agents_python/train.py FPSAgent -l \
  --nne-cpu-path cluster/learning_agents_python/nne_runtime_basic_cpu \
  Socket 127.0.0.1:$PORT "$RUN/trainer" &

for i in $(seq 0 $((NUM_GAMES-1))); do
  "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended \
    -LearningAgentsUseExternalTrainingProcess \
    -LearningAgentsIpAddress=127.0.0.1 -LearningAgentsPort=$PORT \
    -LearningAgentsTimeout=30 \
    -Stage=S0 -Seed=$((SLURM_JOB_ID*1000+i)) -Arenas=$ARENAS \
    -abslog="$RUN/game_$i.log" &
done
wait
```
`-Stage`, `-Seed` and `-Arenas` are our own command-line arguments, parsed in C++.

**Kinds of parallelism:**
1. **Within a job:** N game processes × K arenas = N·K agents feeding one PPO trainer. This makes each run faster in wall-clock time. Epic tested 512 game processes against one trainer.
2. **Across nodes:** `train_multi_node.sbatch` runs the trainer on the first node, and game processes on the other nodes connect over InfiniBand (validated in Spike D).
3. **Across experiments:** `sweep_array.sbatch` uses `#SBATCH --array=0-7`. Each array index picks a seed, reward weights or hyperparameters from `cluster/sweeps/*.yaml`. These are independent runs that train side by side.

**Checkpoints and time limits:**
- The trainer writes snapshots to `$RUN/trainer` on Weka.
- On `USR1`, the job shuts the game processes down cleanly.
- Long runs chain jobs with `sbatch --dependency=afterany:<jobid>`, and each new job resumes from the latest snapshot. How resume works is checked in Spike A.

**Results:**
- The CLUSTER side writes `docs/runs/<run-id>.md` (plus small CSVs) and publishes the snapshots worth watching as Release `run-<run-id>`.
- LOCAL pulls the report, downloads the release, then loads the snapshots in the editor to watch the agents or run the deathmatch.

---

## Roadmap

### Phase 0: Spikes (prove the risky parts first; findings go in `docs/spikes/`)
| Spike | Question | Pass criterion |
|---|---|---|
| **A** | Does a minimal packaged 5.7 Learning Agents game run on Cradle with `-nullrhi` and train against an external `train.py`? | Reward goes up on a toy task. Snapshots save, and training resumes from them. |
| **B** | How fast is each process, and what N (processes) × K (arenas) works best? | A table of steps/s against N and K on one node, and a chosen default |
| **C** | Do the fixed 60 Hz step and uncapped tick behave correctly headless? Candidates are `-benchmark -fps=60` or setting the fixed delta in C++. | Movement speeds and fire rate match the config within 1% |
| **D** | Can game processes on other nodes connect to the trainer? | A 2-node job trains |
| **E** | Do the Cradle GPU driver and our PyTorch build work together? | `torch.cuda.is_available()` in a job; the trainer runs on GPU |
| **F** | Can Unreal-MCP's `python-execute` build a level from a JSON spec? | A spec round-trips into a playable level with a nav mesh |

### Phase 1: Core mechanics (Windows)
Bean character, movement, hitscan rifle (damage per hitbox, spread, recoil), health/armor. Unreal automation tests for the weapon and movement math.

### Phase 2: Bots + Learning Agents integration (built on Windows, trained on Cradle)
Range bots and difficulty, arena instances, episode reset and randomization, and the Learning Agents integration in C++. Play-test in the editor without training, then package for Linux. The first real S0 training run happens on Cradle.

### Phase 3: Cluster training
Single-node job, then sweeps (job arrays), then multi-node. Train S0–S2.

### Phase 4: Maps + S3
Spec format, validator, generator and in-editor builder. Original full maps and variants. Hunter bots. Train S3.

### Phase 5: Deathmatch field test
Eval game mode, inference-only agents, metrics, Elo, showcase recording.

### Phase 6: Game integration
Human-likeness settings mapped to difficulty levels; the policy runs in a shipped game build.

---

## Open questions and things to verify
- Cradle partition names, maximum time limits, fair-share/QOS limits, and whether whole-node jobs are allowed.
- Whether nodes can reach each other on arbitrary TCP ports (needed for multi-node training).
- The Weka path for run outputs, and home directory quotas.
- Every mechanic value marked **verify** above. Public numbers change with patches.
- Learning Agents 5.7 class and API names compared with the 5.5 course; how the trainer resumes from a snapshot; how the trainer's settings (PPO hyperparameters) are passed in external-trainer mode.
- Whether Unreal-MCP's beta works smoothly with 5.7 for level building (Spike F).

---

## References
- Epic: [Learning Agents (5.5) course](https://dev.epicgames.com/community/learning/courses/GAR/unreal-engine-learning-agents-5-5) · [course forum thread](https://forums.unrealengine.com/t/course-learning-agents-5-5/2123144) · [Scaling Out with Learning Agents (PDF)](https://forums.unrealengine.com/uploads/short-url/jjyyeiAv1Z626WJiKV6okXagq9b.pdf) · [Linux development requirements / toolchains](https://dev.epicgames.com/documentation/unreal-engine/linux-development-requirements-for-unreal-engine)
- Cradle: [home](https://hpc.utrgv.edu) · [specifications](https://hpc.utrgv.edu/specifications) · [sample Slurm script](https://hpc.utrgv.edu/getting-started/sample) · [preinstalled software](https://hpc.utrgv.edu/getting-started/pre-installed) · [legacy HPCC page](https://www.utrgv.edu/hpcc/)
- [Unreal-MCP README](https://github.com/IvanMurzak/Unreal-MCP/blob/main/README.md)
- [AMD Schola](https://github.com/GPUOpen-LibrariesAndSDKs/Schola) · [distributed training guide](https://gpuopen.com/manuals/schola/guides/distributed_training/) · [tips](https://gpuopen.com/manuals/schola/guides/tips_and_tricks/)
- Mechanics references (to verify): [Dot Esports rifle comparison](https://dotesports.com/valorant/guides/valorant-vandal-vs-phantom-vs-warden) · [Hotspawn Phantom vs Vandal](https://www.hotspawn.com/valorant/guide/phantom-vs-vandal)
