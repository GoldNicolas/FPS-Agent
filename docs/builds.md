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
| `build-cccd809` | `cccd809` | 5.7.4 · v26 toolchain | Development | 380 MB (`sha256 422c84df…`) | **First Linux build. Spike A:** `-Stage=SpikeA` toy reach task, Learning Agents with an external trainer only, `-Infer` / `-Scripted` / `-ResumeFrom` / `-Iterations`. Also contains the range, `map01` and the 50-map pool (`-Map=`). **Test:** run Spike A per [`spikes/A-learning-agents-linux.md`](spikes/A-learning-agents-linux.md) (L-004). **Checked locally:** the package has exec bits, cooked BasicShapes, the 52 MapSpecs files and both plugins, and no UnrealMCP. It has **never run on Linux** (no Linux box here). | published, untested on Linux |
