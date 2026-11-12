#!/bin/bash
# Spike C on Cradle (docs/spikes/C-fixed-timestep.md): do the fixed 60 Hz step and the uncapped tick
# behave correctly headless, and does movement match the config?
#   - PROCS scripted (-Scripted) single-arena playback processes, PARALLEL at a time. Each logs its
#     first 6 finished episodes, from which cluster/scripts/spikeC_analyze.py fits the run speed.
#   - one scripted process with BIG_ARENAS arenas for the step and tick checks.
# CPU only, no trainer, no GPU. Paths per D-021 (docs/cradle.md -> "Our paths").
#
#   mkdir -p ~/beanRant/logs
#   sbatch ~/beanRant/FPS-Agent/cluster/slurm/spikeC.sbatch
#SBATCH --job-name=spikeC
#SBATCH -p gpuq
#SBATCH -N 1
#SBATCH --cpus-per-task=8
#SBATCH --mem=16G                 # not enforced (CR_CORE); documents the need
#SBATCH --time=00:20:00
#SBATCH --output=/home/%u/beanRant/logs/%x-%j.out
set -uo pipefail

BUILD_SHA=${BUILD_SHA:-07eac52}
PROCS=${PROCS:-24}
PARALLEL=${PARALLEL:-8}
SECONDS_EACH=${SECONDS_EACH:-20}
BIG_ARENAS=${BIG_ARENAS:-16}
BIG_SECONDS=${BIG_SECONDS:-60}

BEAN=$HOME/beanRant
REPO=$BEAN/FPS-Agent
BUILD=$BEAN/builds/$BUILD_SHA/Linux
RUN_ID=spikeC-$(date +%Y%m%d)-$SLURM_JOB_ID
RUN=$BEAN/runs/$RUN_ID
GAME_HOME=$RUN/home               # the engine writes its user files here, not in ~ (D-021)
mkdir -p "$RUN" "$GAME_HOME"
cd "$RUN"
unset PYTHONPATH PYTHONHOME VIRTUAL_ENV
export PYTHONNOUSERSITE=1

T0=$(date +%s)
{
  echo "run_id=$RUN_ID job=$SLURM_JOB_ID host=$(hostname) start=$(date -Is)"
  echo "build=$BUILD_SHA repo_commit=$(git -C "$REPO" rev-parse --short HEAD) procs=$PROCS parallel=$PARALLEL seconds_each=$SECONDS_EACH big_arenas=$BIG_ARENAS"
  echo "cpus=${SLURM_CPUS_PER_TASK:-?}"
} | tee "$RUN/run_info.txt"
[ -x "$BUILD/FPSAgent.sh" ] || { echo "missing build: $BUILD/FPSAgent.sh"; exit 1; }

# timeout makes its own process group; FPSAgent.sh doesn't exec the engine, so wait for each group to
# empty, not just for timeout (see spikeA.sbatch, infer mode).
TPIDS=()
play() {  # play <seconds> <arenas> <seed> <log>
  HOME=$GAME_HOME timeout "$1" "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended -Stage=SpikeA \
    -Arenas="$2" -Infer -Scripted -Seed="$3" -abslog="$4" > /dev/null 2>&1 &
  TPIDS+=($!)
}
drain() {
  wait
  for p in "${TPIDS[@]}"; do for _ in $(seq 1 60); do pgrep -g "$p" > /dev/null || break; sleep 1; done; done
  TPIDS=()
}
trap 'for p in "${TPIDS[@]}"; do kill -- -"$p" 2>/dev/null; done' EXIT

# 1. Single-arena scripted processes (run speed).
for i in $(seq 0 $((PROCS - 1))); do
  play "$SECONDS_EACH" 1 $((SLURM_JOB_ID * 1000 + i)) "$RUN/scripted_k1_$i.log"
  if (( (i + 1) % PARALLEL == 0 )); then drain; fi
done
drain
echo "single-arena processes done after $(( $(date +%s) - T0 )) s"

# 2. One process with many arenas (fixed step, uncapped tick).
play "$BIG_SECONDS" "$BIG_ARENAS" $((SLURM_JOB_ID * 1000 + 999)) "$RUN/scripted_k${BIG_ARENAS}.log"
drain
echo "big process done after $(( $(date +%s) - T0 )) s"

# 3. Analysis.
"$BEAN/envs/la-train/bin/python" -I "$REPO/cluster/scripts/spikeC_analyze.py" "$RUN"/scripted_*.log
echo "== done after $(( $(date +%s) - T0 )) s"
