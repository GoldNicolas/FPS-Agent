# Build log

**Owner: LOCAL side.** Every Linux build that goes to Cradle gets one row, newest first. Build archives are attached to a GitHub Release, never committed to git.

- **Release tag:** `build-<short-sha>`. The SHA is the commit the build was made from. The working tree must be clean when packaging.
- **Asset:** `FPSAgent-Linux-<short-sha>.tar.gz` plus `SHA256SUMS`.
- **Publish (LOCAL):** `gh release create build-<sha> <tarball> SHA256SUMS --prerelease --title "build-<sha>" --notes "<summary>"`
- **Fetch (CLUSTER):**
  ```bash
  gh release download build-<sha> -D ~/fps-agent/builds/<sha>
  cd ~/fps-agent/builds/<sha>
  sha256sum -c SHA256SUMS
  tar xzf FPSAgent-Linux-<sha>.tar.gz
  ```

| Tag | Source commit | UE | Config | Size | What changed / what to test | Status |
|---|---|---|---|---|---|---|
| | | | | | | |
