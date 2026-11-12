# Build log

**Owner: LOCAL side.** Every Linux build that goes to Cradle gets one row, newest first. Build archives are attached to a GitHub Release, never committed to git.

- **Release tag:** `build-<short-sha>`. The SHA is the commit the build was made from. The working tree must be clean when packaging.
- **Asset:** `FPSAgent-Linux-<short-sha>.tar.gz` plus `SHA256SUMS`.
- **Publish (LOCAL):** `gh release create build-<sha> <tarball> SHA256SUMS --prerelease --title "build-<sha>" --notes "<summary>"`
- **Fetch (CLUSTER):**
  ```bash
  gh release download build-<sha> -D ~/beanRant/builds/<sha>   # D-021: everything under ~/beanRant
  cd ~/beanRant/builds/<sha>
  sha256sum -c SHA256SUMS
  tar xzf FPSAgent-Linux-<sha>.tar.gz
  ```

- **Make (LOCAL):** `powershell -File tools/package_linux.ps1 [-Publish]` (clean, pushed tree; editor closed). Each build contains `Linux/BUILD_INFO.txt` with the commit, engine and toolchain.

| Tag | Source commit | UE | Config | Size | What changed / what to test | Status |
|---|---|---|---|---|---|---|
| `build-10520b5` | `10520b5` | 5.7.4 · v26 toolchain | Development | 382 MB (`sha256 be5e1472…`) | **Combat build A: `-Stage=S0` (aim gallery).**<br>**Stage:** `-Arenas` generated 80 m floors with one agent plus 1–5 static bots each. 15 decisions/s, discount 0.995 (D-023), and a per-arena automatic difficulty ramp.<br>**Inputs and rewards:** fair observations: rays and line of sight hit level geometry only; enemies 15 values each, with fine aim channels; a 103° × 70.5° view; a 2 s memory; a hit indicator. Reward terms per spec §3, each logged separately. Timeout = termination (D-028).<br>**New args:** `-StartDifficulty`, `-Eval`/`-EvalEpisodes`/`-EvalJson` (fixed per-arena quotas), `-StatsCsv` (appends), `-Set=` (D-025), `-Scripted` reference agent. Also the `-Stage=SpikeC` mechanics probe (C-006) and the PPO batch/replay settings in `Combat.Ppo` (C-007 (2)). Unknown `-Stage` or bad args exit with code 2.<br>Spike A is unchanged; its plumbing moved to `UTrainingSession`, with the same log lines and exit codes.<br>**Test (L-007):** smoke, Spike B on combat, S0 training, promotion `-Eval`.<br>**Checked locally:**<br>• 55/55 C++ tests.<br>• Scripted S0, 4 arenas: clear 100%, clear time 2–5 s, acc 22–48%, hs ≈ 100%, `level` 0.1 → 1.0 in about 30 s of game time, about 2000 decisions/s.<br>• Scripted `-Eval`, 40 episodes at level 1: `passed`, clear rate 1.0, 3.4 s average clear.<br>• Untrained `-Infer`: no Learning Agents errors.<br>• Exits: no trainer → 1; bad `-Set`, `-Stage=S1`/`S4`, `-Eval` without snapshots and overlapping arenas → log `exiting (code 2)` (Windows reports 0 for non-forced exits; Linux keeps the code).<br>• SpikeC probe: PASS. | published, untested on Linux |
| `build-07eac52` | `07eac52` | 5.7.4 · v26 toolchain | Development | 380 MB (`sha256 e02ad0ff…`) | **Spike A fix pack.** **C-004:** completion is a pure distance check, and the reach bonus is paid once per episode, so training returns now match playback. **C-005:** new `-Batches=N` (= N × 32 gradient iterations), logged as `iterations=… (= … batches of 32)`. Otherwise the same as `build-cccd809`. **Test (L-005):** a short Spike A training run with `-Batches=30` (as in run 1b); the training `avgReturn` / `Avg Rew Sum` should end near **+10**, like playback, not +15. **Checked locally:** 33/33 C++ tests; scripted headless playback 100% reached, avgReturn about +10. | runs on Linux (L-005, Spike B/C) |
| `build-cccd809` | `cccd809` | 5.7.4 · v26 toolchain | Development | 380 MB (`sha256 422c84df…`) | **First Linux build. Spike A:** `-Stage=SpikeA` toy reach task, Learning Agents with an external trainer only, `-Infer` / `-Scripted` / `-ResumeFrom` / `-Iterations`. Also contains the range, `map01` and the 50-map pool (`-Map=`). **Test:** run Spike A per [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) (L-004). **Checked locally:** the package has exec bits, cooked BasicShapes, the 52 MapSpecs files and both plugins, and no UnrealMCP. It has **never run on Linux** (no Linux box here). | published, untested on Linux |
