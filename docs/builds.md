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
| `build-07eac52` | `07eac52` | 5.7.4 · v26 toolchain | Development | 380 MB (`sha256 e02ad0ff…`) | **Spike A fix pack.** **C-004:** completion is a pure distance check, and the reach bonus is paid once per episode, so training returns now match playback. **C-005:** new `-Batches=N` (= N × 32 gradient iterations), logged as `iterations=… (= … batches of 32)`. Otherwise the same as `build-cccd809`. **Test (L-005):** a short Spike A training run with `-Batches=30` (as in run 1b); the training `avgReturn` / `Avg Rew Sum` should end near **+10**, like playback, not +15. **Checked locally:** 33/33 C++ tests; scripted headless playback 100% reached, avgReturn about +10. | published, untested on Linux |
| `build-cccd809` | `cccd809` | 5.7.4 · v26 toolchain | Development | 380 MB (`sha256 422c84df…`) | **First Linux build. Spike A:** `-Stage=SpikeA` toy reach task, Learning Agents with an external trainer only, `-Infer` / `-Scripted` / `-ResumeFrom` / `-Iterations`. Also contains the range, `map01` and the 50-map pool (`-Map=`). **Test:** run Spike A per [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) (L-004). **Checked locally:** the package has exec bits, cooked BasicShapes, the 52 MapSpecs files and both plugins, and no UnrealMCP. It has **never run on Linux** (no Linux box here). | published, untested on Linux |
