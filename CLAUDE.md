# CLAUDE.md

Guidance for **every** Claude Code session in this repo. Two sessions work in parallel, one on Windows and one on the Cradle cluster, and **this GitHub repo is the only channel between them**. `README.md` is the design and runbook; `docs/STATUS.md` is the live mailbox.

## What this project is
A headless RL training environment in **UE 5.7**: "beans with guns" agents with Valorant-like mechanics, trained with **Learning Agents (PPO)** on the **UTRGV Cradle** Slurm cluster, then shown off in a **deathmatch field test**. The long-term goal is an in-game bot mechanic.

## Two roles, one repo

### Which role am I?
- **Windows** (the user's laptop, UE 5.7 installed, path like `C:\Users\...\FPS-Agent`) → **LOCAL role**
- **Linux on Cradle** (Rocky Linux 8.6, hostname `login*`, Slurm commands available) → **CLUSTER role**

If unsure, run `uname -a` / `hostname`. Say which role you are taking in your first reply.

### Start of every session
1. `git pull --rebase`
2. Read `docs/STATUS.md`: the requests addressed to your role, the latest build, and the runs.
3. Read the docs relevant to your task (README section, `docs/cradle.md`, spike or run reports).

### After every advancement (a finished step, result or fix)
1. Update the docs: your rows in `docs/STATUS.md`, plus the spike, run or build report you produced. Add a `docs/decisions.md` entry if a design decision changed, and update `README.md` if the design or workflow changed.
2. Commit with the role prefix: `[local] ...` or `[cluster] ...`. Keep commits small and focused.
3. `git pull --rebase`, then `git push`. **Never force-push.** On a conflict in `STATUS.md`, keep both sides' entries.

Ask the user before pushing anything unusual (history rewrites, deleting the other side's files, new releases outside the conventions below).

### Who owns what (only edit files your role owns; ask through STATUS.md otherwise)
| Path | Owner |
|---|---|
| `Game/`, `tools/`, `environment.yml`, `docs/maps/`, `cluster/learning_agents_python/`, `docs/builds.md` | LOCAL |
| `cluster/slurm/`, `cluster/env/`, `cluster/scripts/`, `cluster/sweeps/`, `docs/cradle.md`, `docs/runs/` | CLUSTER |
| `docs/spikes/<X>.md` | Whichever side ran that spike |
| `docs/STATUS.md` | Shared: only your own rows, and the Status/Answer cells of requests addressed to you |
| `README.md`, `CLAUDE.md`, `docs/decisions.md` | Shared: small edits; record design changes in `decisions.md` |

### What travels how
| Thing | Channel |
|---|---|
| Code, scripts, configs, markdown, small CSVs (< 1 MB) | git |
| Linux game builds | GitHub Release `build-<short-sha>` (see `docs/builds.md`) |
| Trained snapshots for LOCAL to watch or evaluate | GitHub Release `run-<run-id>` (see `docs/runs/README.md`) |
| Full snapshots, logs, TensorBoard events | Stay on Cradle (`~/beanRant/runs/`, D-021). Never committed. |

## Hard rules (do not break without the user's explicit OK)
1. **No computer vision.** Never add camera, render-target or pixel observations. Agents perceive the world through ray casts and line-of-sight checks only.
2. **Everything must work under `-nullrhi -nosound`.** Training gameplay code must not depend on rendering:
   - no skeletal-mesh or bone-based hitboxes (use primitive components),
   - nothing that relies on animation ticking, render-thread queries or screenshots.
   - Cradle has no OpenGL or display.
3. **Agents train ONLY against scripted bots.** No self-play and no agent-vs-agent play in training. In training, agents must never observe or damage other agents (each arena instance holds 1 agent plus its bots). Agent-vs-agent play exists **only** in the deathmatch evaluation, which runs inference-only snapshots.
4. **Fair observations.** An enemy shows up in observations only if it passes a line-of-sight check inside the FOV, or was seen within the short memory window. No information through walls, with two human-equivalent exceptions (D-024): a hit indicator (direction of damage taken) and hearing enemy gunshots and footsteps within a radius.
5. **Original maps only.** Never reproduce Riot's maps (layouts, callouts, proportions traced from real maps), names, characters or assets, not even approximately. Follow genre structure (sites, mid, chokes, sightline bands) with original layouts. Name the rifle and maps generically (no "Vandal", "Ascent" and so on in code or assets).
6. **No local training.** All training and large evaluation runs happen on Cradle. Locally, only build, play-test (no training), package and replay snapshots.
7. **Mechanics are data, not constants.** Movement, weapon, health, bot-difficulty and reward values live in DataAssets or config, each with a source or `verify` note. Never hard-code them in gameplay logic.
8. **The LOCAL session never connects to Cradle** (no ssh, scp or remote commands). Anything that must happen on the cluster becomes a request in `docs/STATUS.md`.

## Versions (pinned)
- Unreal Engine **5.7** (5.7.4 installed); Linux cross-compile toolchain **v26 (clang 20.1.8, Rocky Linux 8)**.
- Windows compiler: VS 2022 Build Tools 17.14, MSVC 14.44, Windows SDK 10.0.26100.
- Cradle: Rocky Linux 8.6 (glibc 2.28), Slurm.
- Learning Agents Python trainer: copied from *this* engine version into `cluster/learning_agents_python/`. Never mix files from different engine versions. The only change to Epic's files is the documented socket-poll patch (D-029; listed in that folder's README).
- The Epic course is for 5.5 and uses Blueprints. When porting, verify class and function names against the 5.7 headers; don't trust the 5.5 names.
- Python 3.11 everywhere we control (it matches UE 5.7's bundled Python 3.11.8). Conda envs use **conda-forge only**.

---

## LOCAL role (Windows)

### Code conventions
- **C++ for everything with logic.** That covers character, movement, weapons, bots, arenas/episodes, the Learning Agents classes and eval. Blueprints hold data and asset wiring only.
- Module folders: `Source/FPSAgent/{Character,Weapons,Bots,Arena,Learning,Eval}`.
- UE units are centimetres. Document unit conversions where config values come from real-world m/s.
- **Determinism:** all randomness (spawns, bot behavior, recoil, spread, map variant) goes through one per-episode seeded RNG, derived from the `-Seed=` command-line argument plus the arena index plus the episode count.
- Our command-line arguments are `-Stage=`, `-Seed=`, `-Arenas=` and so on. Parse them in one place in C++, log the parsed values at startup, and document every one in README → Cluster, because the CLUSTER side depends on them.
- Keep the RL layer (`Learning/`) behind a thin interface over the gameplay code, so the trainer stack could be swapped (e.g. to AMD Schola) without touching gameplay.
- Reward terms and weights live in one config struct, and each term is logged separately (to TensorBoard or MLflow) so sweeps can be compared.
- Accuracy and headshot % are **metrics, not rewards**.
- `docs/FPS-Agent-Constraints.md` is only the v1 hand-off reference; the game project keeps the maintained copy. Touch it only when a change alters observations, actions, mechanics values, perception or hearing rules, or decision timing. Then write a new contract version and tell the user to give it to the game project.

### Unreal-MCP (editor-only)
- Setup: plugin 0.30.0, **local mode** with no cloud account (`Game/FPSAgent/.env`). The `unreal-mcp` server is in the repo-root `.mcp.json` (`http://localhost:24268/mcp/p/d2699ae8`). It is only reachable while the editor is open; check with `unreal-mcp-cli status -p Game/FPSAgent`.
- **The plugin is not in git** (decision D-010). On a fresh clone, reinstall it before opening the project (README → Windows dev setup).
- Never switch to Cloud mode or sign in to ai-game.dev without the user's OK. Never commit `.env`.
- Keep `"TargetDenyList": ["Game","Client","Server"]` on the UnrealMCP entry in `FPSAgent.uproject`. Never add `UnrealMcp*` modules to `FPSAgent.Build.cs`; that would override the deny list and pull the plugin into the Linux builds.
- **C++:** edit files under `Source/`, then build with `powershell -File tools/build.ps1` (editor closed) and fix the reported errors before moving on. Includes are relative to the module root (`"Common/FPSAgentSettings.h"`). In UE 5.7, `FVector`, `FVector2D` and `FRotator` are double: cast to float in `TestEqual` calls. Never give a test a name that is a dot-prefix of another test's name (`X.Stats` next to `X.Stats.ClearTop`): UE turns `X.Stats` into a group and silently skips it. After adding tests, check that the suite count rose by exactly that many.
- **Levels are data, built at runtime** (D-011). Never hand-place level actors or author `.umap` files.
  - Specs live in `Game/FPSAgent/Content/MapSpecs/` (format v1, README → Maps).
  - `map01.json` is generated from `tools/mapgen/maps/map01.py`, so edit the script, not the JSON. `gen_*.json` and `pool.json` come from `python -m mapgen generate`.
  - Every spec must pass `python -m mapgen validate` before it is committed.
  - Python always runs as `conda run -n fps-agent ...` from `tools/mapgen`. Calling `python.exe` directly crashes because of a DLL conflict on PATH.
  - Commit the preview PNGs (`docs/maps/`) together with the maps.
- **Console tool caveat:** Unreal-MCP's `console-run-command` runs against the **editor** world, so player-controller Exec commands never reach a play-in-editor session. Expose play-time switches as console variables (`fps.PlayMap`, `fps.TopDown`, `fps.ShowHitboxes`, `fps.WatchAgent`, `fps.ShowShots`).
- Use RQL (`rql-query` / `rql-mutate`) for inspecting and small edits to actors, assets and settings.
- Screenshot tools need a GPU editor. Never put them in anything that runs headless.
- `python-execute` and `console-run-command` can run arbitrary code (RCE-class). Keep scripts in the repo (`tools/`) so they can be reviewed; avoid long ad-hoc snippets.

### Shipping a build to the cluster
1. Commit everything first. Builds come only from a clean working tree, so the release tag's SHA is exact.
2. Package for Linux (Development config) and make a tarball plus `SHA256SUMS`.
3. Publish GitHub Release `build-<short-sha>` (prerelease). Add a row to `docs/builds.md` saying what changed and what to test, then add a request in `STATUS.md`.

---

## CLUSTER role (Cradle)

- **Login-node etiquette:** only light work on the login node (git, editing, `sbatch`/`squeue`, conda installs, `gh`). `sacct` doesn't work on Cradle (accounting is off); use `squeue` and the job logs. **Never run training, game processes, or anything heavy there.** Everything runs through Slurm (`cluster/slurm/*.sbatch`, or `srun` for short tests).
- **You cannot build the game here** (no Unreal Engine on Cradle). Don't edit `Game/` or `tools/`. Choose arenas at runtime with `-Map=range|map01|gen_007|pool` (+ `-Seed=`). If the game needs a change, add a `C-xxx` request with the exact error or log excerpt.
- **Get builds** with `gh release download build-<sha>` into `~/beanRant/builds/<sha>/`, and verify with `sha256sum -c SHA256SUMS` before use.
- **Paths (D-021): everything stays under `~/beanRant/`.**
  - Repo: `~/beanRant/FPS-Agent`
  - Builds: `~/beanRant/builds/<sha>/`
  - Run outputs: `~/beanRant/runs/<run-id>/`
  - Slurm output: `~/beanRant/logs/`
  - Trainer env: `~/beanRant/envs/la-train`

  Never write project files elsewhere. The only exception is `gh`'s login in `~/.config/gh`. Details are in `docs/cradle.md` → *Our paths*.
- Partition names and limits come **only** from `docs/cradle.md`. If a value there is still `TBD`, measure it or ask the user; don't guess.
- The trainer port is derived from the job ID (`40000 + SLURM_JOB_ID % 20000`). Never hard-code ports.
- Jobs must survive time limits: save snapshots periodically, stop cleanly on `--signal=B:USR1@300`, and resume by chaining jobs (`--dependency=afterany`).
- It is a small, shared cluster (12 nodes). Don't hold GPUs or whole nodes idle; size jobs from the Spike B measurements.
- **Report every run** in `docs/runs/<run-id>.md` and in the STATUS.md Runs table. Publish snapshots LOCAL should look at as Release `run-<run-id>`. Never commit snapshots, logs or TensorBoard files.
- Never commit secrets (the `gh` token, `.env`). Shell and sbatch scripts must keep LF line endings (enforced by `.gitattributes`).

---

## Testing and verification
- **Weapon and movement math (LOCAL):** Unreal automation tests covering damage per hitbox, spread versus speed and the threshold, a deterministic recoil sequence for a given seed, fire-rate timing, and movement speeds under the fixed timestep.
- **Map tools (LOCAL):** `conda run -n fps-agent python -m pytest -q` in `tools/mapgen` covers the grid movement/line-of-sight model, all 8 validation rules, `map01`, and the generator (validity, determinism, variety). C++ `FPSAgent.Map.*` tests cover parsing, blocks, ramps, loading `map01`, and world checks (floor heights, cover blocking shots).
- **Commands (LOCAL):** `powershell -File tools/build.ps1`, then `powershell -File tools/run_tests.ps1` (editor closed; tests run with `-nullrhi`). For play-in-editor checks: `unreal-mcp-cli open Game/FPSAgent --headless --no-build`, then `unreal-mcp-cli run-tool rql-mutate -p Game/FPSAgent --input '{"query":"editor | call StartPlay()"}'` / `AdvancePlay(seconds, 0, "W")`, then close with `unreal-mcp-cli close Game/FPSAgent --owned-only`. Only close editors the CLI started.
- **Headless smoke test (CLUSTER):** before any long run on a new build, submit a short (about 5 min) Slurm debug job: 1–2 game processes with `-nullrhi` against the external trainer. They must connect, step, log and save a snapshot.
- Don't call something done until it has been run. Report real output, including failures, in the docs.

## Repo hygiene
- Git LFS handles `*.uasset` and `*.umap` (`.gitattributes`). Never commit `Binaries/`, `Intermediate/`, `Saved/`, `DerivedDataCache/`, packaged builds, the UnrealMCP plugin folder, or run outputs (`.gitignore`).
- `PLAN.md` holds the user's original notes. Leave it alone unless asked; `README.md` supersedes it where they differ (e.g. original maps instead of 1:1 copies).
