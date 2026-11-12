#!/bin/bash
# Spike A on Cradle (docs/spikes/A-learning-agents-linux.md, request L-004).
#   MODE=train: one external trainer + NUM_GAMES headless game processes (optionally resumed).
#   MODE=infer: playback of snapshots with no trainer (stopped after INFER_SECONDS).
# Paths per D-021 (docs/cradle.md -> "Our paths"): everything under ~/beanRant.
#
#   mkdir -p ~/beanRant/logs        # Slurm doesn't create the --output folder
#   S=~/beanRant/FPS-Agent/cluster/slurm/spikeA.sbatch
#   sbatch --export=ALL,BATCHES=30 $S                                     # training run (30 experience batches)
#   sbatch --export=ALL,BATCHES=2,RESUME_FROM=<dir with the 4 .bin> $S    # resume test
#   sbatch --gres=none -c 4 -t 10 --export=ALL,MODE=infer,RESUME_FROM=<dir> $S   # playback, no GPU
#   sbatch --gres=none -c 4 -t 10 --export=ALL,MODE=infer,INFER_FLAGS=-Scripted $S
#
# Run length: BATCHES=N passes -Batches=N (build-07eac52 and newer; = N x 32 gradient iterations).
# ITERATIONS=N passes -Iterations=N, counted in *gradient* iterations (C-005). BATCHES wins.
# build-cccd809 has no -Batches, so there BATCHES is converted to -Iterations=N*32.
#SBATCH --job-name=spikeA
#SBATCH -p gpuq
#SBATCH -N 1
#SBATCH --gres=gpu:1
#SBATCH --cpus-per-task=8
#SBATCH --mem=32G                 # not enforced (CR_CORE); documents the need
#SBATCH --time=01:00:00           # gpuq has no time limit: always set one
#SBATCH --output=/home/%u/beanRant/logs/%x-%j.out
set -uo pipefail

MODE=${MODE:-train}
BUILD_SHA=${BUILD_SHA:-07eac52}
NUM_GAMES=${NUM_GAMES:-2}
ARENAS=${ARENAS:-16}
BATCHES=${BATCHES:-}
ITERATIONS=${ITERATIONS:-150}
RESUME_FROM=${RESUME_FROM:-}
INFER_SECONDS=${INFER_SECONDS:-120}
INFER_FLAGS=${INFER_FLAGS:-}
CONNECT_TIMEOUT=${CONNECT_TIMEOUT:-120}

BEAN=$HOME/beanRant
REPO=$BEAN/FPS-Agent
BUILD=$BEAN/builds/$BUILD_SHA/Linux
RUN_ID=spikeA-$(date +%Y%m%d)-$SLURM_JOB_ID
RUN=$BEAN/runs/$RUN_ID
GAME_HOME=$RUN/home               # the engine writes its user files here, not in ~ (D-021)
mkdir -p "$RUN" "$GAME_HOME"
cd "$RUN"

# This account's login shell exports another project's Python setup (docs/cradle.md). Drop it.
unset PYTHONPATH PYTHONHOME VIRTUAL_ENV
export PYTHONNOUSERSITE=1
export XDG_CACHE_HOME=$BEAN/.cache CUDA_CACHE_PATH=$BEAN/.cache/nv

T0=$(date +%s)
{
  echo "run_id=$RUN_ID mode=$MODE job=$SLURM_JOB_ID host=$(hostname) start=$(date -Is)"
  echo "build=$BUILD_SHA repo_commit=$(git -C "$REPO" rev-parse --short HEAD) games=$NUM_GAMES arenas=$ARENAS"
  echo "batches=${BATCHES:-none} iterations=$ITERATIONS resume_from=${RESUME_FROM:-none} infer_flags=${INFER_FLAGS:-none}"
  echo "cpus=${SLURM_CPUS_PER_TASK:-?} cuda_visible_devices=${CUDA_VISIBLE_DEVICES:-none}"
} | tee "$RUN/run_info.txt"
[ -x "$BUILD/FPSAgent.sh" ] || { echo "missing build: $BUILD/FPSAgent.sh"; exit 1; }

PIDS=()
cleanup() { for p in "${PIDS[@]}"; do kill "$p" 2>/dev/null; done; }
trap cleanup EXIT
trap 'echo "$(date -Is) got a stop signal"; exit 143' TERM USR1

GAME_ARGS=(-nullrhi -nosound -unattended -Stage=SpikeA -Arenas="$ARENAS")
[ -n "$RESUME_FROM" ] && GAME_ARGS+=(-ResumeFrom="$RESUME_FROM")

if [ "$MODE" = infer ]; then
  # timeout puts itself in a new process group and signals all of it, so the engine stops too.
  # But FPSAgent.sh doesn't exec the engine: the sh wrapper dies at once and timeout returns while the
  # engine is still shutting down. Wait for the group to empty, or Slurm kills the engine when this
  # script ends (eval.log then loses its last lines; L-004 playback jobs).
  HOME=$GAME_HOME timeout "$INFER_SECONDS" "$BUILD/FPSAgent.sh" "${GAME_ARGS[@]}" -Infer $INFER_FLAGS \
    -Seed=$((SLURM_JOB_ID * 1000)) -abslog="$RUN/eval.log" > "$RUN/eval.stdout" 2>&1 &
  TPID=$!
  wait "$TPID"; RC=$?
  for _ in $(seq 1 60); do pgrep -g "$TPID" > /dev/null || break; sleep 1; done
  pgrep -g "$TPID" > /dev/null && echo "engine still running 60 s after the timeout"
  echo "playback exit=$RC (124 = stopped by timeout, the normal end) after $(( $(date +%s) - T0 )) s"
  grep -E "SpikeA: (ready|exiting)" "$RUN/eval.log"
  grep "SpikeA stats" "$RUN/eval.log" | tail -3
  tail -2 "$RUN/eval.log"
  exit 0
fi

# 1. Trainer first. Its output gets a timestamp per line (the trainer prints none) for seconds/iteration.
# Full path on purpose: `activate` alone leaves ~/miniconda3/envs/locomujoco/bin first on PATH (docs/cradle.md).
PY=$BEAN/envs/la-train/bin/python
export PATH=$BEAN/envs/la-train/bin:$PATH
"$PY" -s -u "$REPO/cluster/learning_agents_python/train.py" FPSAgent -l \
  --nne-cpu-path "$REPO/cluster/learning_agents_python/nne_runtime_basic_cpu" \
  Socket 127.0.0.1:$((40000 + SLURM_JOB_ID % 20000)) "$RUN/trainer" \
  > >(while IFS= read -r line; do printf '[%(%F %T)T] %s\n' -1 "$line"; done > "$RUN/trainer.log") 2>&1 &
TRAINER=$!
PIDS+=("$TRAINER")
nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used --format=csv -l 5 > "$RUN/gpu.csv" 2>&1 &
PIDS+=($!)

# 2. Wait for the listener line. Never probe the port: a connect+close wedges the 5.7 trainer (D-020).
for _ in $(seq 1 180); do
  grep -q "Server listening on" "$RUN/trainer.log" && break
  kill -0 "$TRAINER" 2>/dev/null || { echo "trainer exited during startup"; tail -30 "$RUN/trainer.log"; exit 1; }
  sleep 1
done
grep -q "Server listening on" "$RUN/trainer.log" || { echo "no listener after 180 s"; tail -30 "$RUN/trainer.log"; exit 1; }
echo "trainer listening after $(( $(date +%s) - T0 )) s"

# 3. Games. Each one exits 0 when the trainer finishes the run, 1 if it loses the trainer.
if [ -z "$BATCHES" ]; then LENGTH=(-Iterations="$ITERATIONS")
elif [ "$BUILD_SHA" = cccd809 ]; then LENGTH=(-Iterations=$((BATCHES * 32)))   # no -Batches in that build
else LENGTH=(-Batches="$BATCHES"); fi
GAMES=()
for i in $(seq 0 $((NUM_GAMES - 1))); do
  HOME=$GAME_HOME "$BUILD/FPSAgent.sh" "${GAME_ARGS[@]}" -Seed=$((SLURM_JOB_ID * 1000 + i)) "${LENGTH[@]}" \
    -LearningAgentsUseExternalTrainingProcess -LearningAgentsIpAddress=127.0.0.1 \
    -LearningAgentsPort=$((40000 + SLURM_JOB_ID % 20000)) -LearningAgentsTimeout="$CONNECT_TIMEOUT" \
    -abslog="$RUN/game_$i.log" > "$RUN/game_$i.stdout" 2>&1 &   # stdout repeats the log; keeps the Slurm log short
  GAMES+=($!)
  PIDS+=($!)
done
echo "games launched at $(date +%T) (epoch $(date +%s))" | tee -a "$RUN/run_info.txt"

RCS=()
for p in "${GAMES[@]}"; do wait "$p"; RCS+=("$?"); done
echo "game exit codes: ${RCS[*]} at $(( $(date +%s) - T0 )) s"

# 4. The trainer exits on its own after sending "complete"; give it 2 minutes.
for _ in $(seq 1 120); do kill -0 "$TRAINER" 2>/dev/null || break; sleep 1; done
kill -0 "$TRAINER" 2>/dev/null && { echo "trainer still running 120 s after the games: stopping it"; kill "$TRAINER"; }
wait "$TRAINER"; echo "trainer exit=$?"

# 5. Keep the final snapshots (the trainer overwrites them in place) and summarize.
SNAP=$(ls -d "$RUN"/trainer/FPSAgent*/Snapshots 2>/dev/null | head -1)
[ -n "$SNAP" ] && cp -a "$SNAP" "$RUN/snapshots_final"
echo "== Iter lines (first 3, last 3; $(grep -c 'Iter:' "$RUN/trainer.log") total)"
grep "Iter:" "$RUN/trainer.log" | head -3; grep "Iter:" "$RUN/trainer.log" | tail -3
echo "== game lines"
grep -hE "SpikeA: (ready|trainer settings|exiting)" "$RUN"/game_*.log
grep -h "SpikeA stats" "$RUN/game_0.log" | tail -3
echo "== files"
ls -l "$RUN"/snapshots_final "$RUN"/trainer/FPSAgent*/Configs 2>&1
echo "== done after $(( $(date +%s) - T0 )) s"
