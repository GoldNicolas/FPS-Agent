# Decision log

Append-only, newest last. Either side may add an entry. Don't rewrite old entries; supersede them with a new one. Design details live in `README.md`.

| ID | Date | Decision | Why |
|---|---|---|---|
| D-001 | 2026-11-10 | Target **UE 5.7** | Multi-process Learning Agents training (many game processes → one trainer) needs 5.6+. Unreal-MCP is CI-tested on 5.7. The v26 toolchain targets Rocky Linux 8, the same OS as Cradle. |
| D-002 | 2026-11-10 | **Learning Agents (PPO)** as the RL stack. AMD Schola is the fallback. | Native, policies run in the shipped game, no containers needed. The RL layer stays separate so a swap is possible. |
| D-003 | 2026-11-10 | **C++** for all logic; Blueprints only for data and wiring | Diffable, reviewable, editable by Claude. |
| D-004 | 2026-11-10 | **Original maps only**, built from JSON specs plus a procedural generator. No copies of Riot's maps, names or assets. | Avoids IP problems, and randomized layouts reduce overfitting. |
| D-005 | 2026-11-10 | **Fair observations** (line of sight only) plus adjustable human-likeness settings | The end goal is in-game opponents; the settings can be relaxed for research. |
| D-006 | 2026-11-10 | **Train ONLY against scripted bots.** The deathmatch is a field test, never training. | User decision. Agent-vs-agent play is evaluation only. |
| D-007 | 2026-11-10 | Unreal-MCP in **local mode** (no ai-game.dev account) | Editor data stays on the machine. |
| D-008 | 2026-11-11 | **No local training**; all training runs on Cradle | User decision. |
| D-009 | 2026-11-11 | **The GitHub repo (private) is the only channel between LOCAL and CLUSTER.** Markdown docs plus commits and pushes after each advancement. Binaries (builds, snapshots) travel via GitHub Releases. Claude Code runs on both sides; the LOCAL session never SSHes into Cradle. | The cluster's security policy; the user works on both sides in parallel. |
| D-010 | 2026-11-11 | The UnrealMCP plugin is **not committed**; it is reinstalled with `unreal-mcp-cli` | ~580 MB of binaries, editor-only, never needed on the cluster. |
| D-011 | 2026-11-11 | Level geometry (the range, later the maps) is **spawned at runtime from data in C++** (`FPSArena`), not authored as `.umap`. The startup map is the empty engine `/Engine/Maps/Entry`. | One code path for the editor and headless Cradle; per-episode procedural variants need no repackaging; no LFS assets. MCP is used for preview and play-testing. |
| D-012 | 2026-11-11 | Mechanics tuning lives in `UFPSAgentSettings` (text config, `DefaultGame.ini`), not in DataAsset `.uasset` files | Diffable, mergeable, readable by both sides of the repo. |
| D-013 | 2026-11-11 | Input actions and mapping are **built in C++ at runtime** (no input assets) | Same reason as D-012; works in any build without content. |
| D-014 | 2026-11-11 | Map format v1 is a **tile grid (4 m tiles) with elevation in 1 m levels**. Ramps are the only way up; drops are one-way; the walkable graph must be strongly connected. | User wanted elevation from the start. A grid keeps generation, validation, line of sight and previews simple. |
| D-015 | 2026-11-11 | The procedural **pool is generated offline in Python** (validated), shipped inside the build as JSON, and **C++ picks a map per run or episode by seed** (`-Map=pool`) | Python is fast to iterate and test; the cluster gets new layouts with no Unreal work. |
| D-016 | 2026-11-11 | Play-time switches are **console variables** (`fps.PlayMap`, `fps.TopDown`, `fps.ShowHitboxes`) | Unreal-MCP's console tool runs against the editor world, so they must be global to be scriptable. |
| D-017 | 2026-11-11 | Sightline "close" floor calibrated **0.30 → 0.20** | Tile-pair sampling makes pairs under 10 m geometrically rare; extra cover on map01 moved it only 0.23 → 0.25. The long cap (≤ 30%) is the gameplay-relevant guard. |
| D-018 | 2026-11-11 | Spike A uses a **toy reach task with the real bean** (`-Stage=SpikeA`). The game **only ever connects to an external trainer** (never spawns one), and the editor always plays back. **Resume = the game loads the `.bin` snapshots before connecting** (weights only). | It proves the whole stack (Linux build, CharacterMovement headless, socket trainer, snapshots) with a task the scripted baseline solves at 100%. External-only enforces hard rule 6. The trainer takes its starting networks from the game, so this is the only resume path in 5.7. |
| D-019 | 2026-11-11 | **One policy step = one fixed 60 Hz tick**, in training (Learning Agents sets it) and in headless playback (we set it) | Without it, `-nullrhi` ticks at about 1 800 Hz with real-time deltas, so "360 steps" meant 0.2 s and episodes were meaningless. |
| D-020 | 2026-11-11 | **Trainer first, games after `Server listening on`; never probe the trainer port.** The game has a wall-clock connect watchdog (exit 1). | The 5.7 trainer wedges on a connection that closes without a config, and the engine's connect timeout ignores time spent in blocking `connect()`. |
