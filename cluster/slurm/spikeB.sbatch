#!/bin/bash
# Spike B on Cradle (docs/spikes/B-throughput.md): agent-steps/s against N game processes x K arenas on
# one node, and the trainer's --batch-size. Paths per D-021 (docs/cradle.md -> "Our paths").
#   MODE=sim:   policy playback (-Infer, no trainer, CPU only) for each "NxK" in CONFIGS
#               (N processes x K arenas, SECONDS_EACH s): raw simulation speed per process and per node.
#   MODE=train: external trainer + N games x ARENAS arenas for each "NxB[:cpu][:poll]" in CONFIGS
#               (N games, trainer --batch-size B), BATCHES experience batches each.
#               :cpu   hide the GPU from the trainer (CUDA_VISIBLE_DEVICES=""), so it falls back to CPU.
#               :poll  run a copy of the trainer in the run folder with its three 1 s polls shortened
#                      (receive_experience and _worker 0.01 s, _sender 0.05 s). Measurement only: the
#                      pinned trainer in cluster/learning_agents_python/ (LOCAL-owned) is not changed.
# Each config gets its own folder in the run; cluster/scripts/spikeB_summarize.py turns the run into
# tables + CSV.
# STAGE=S0 (build-10520b5 and newer, L-007): combat stage S0 instead of the SpikeA reach task.
#   sim:   a config may end in @L (difficulty 0-1, default 0): the games run -Eval with a huge episode count at
#          that fixed level (no ramp), from RESUME_FROM or with SIM_SCRIPTED=1 the -Scripted reference.
#          @0 = 1 static bot per arena, @1 = 5, so the pair shows what the bots cost.
#   train: the games also write -StatsCsv=<config>/stats_<i>.csv; SET="..." passes -Set= (D-025).
# Since D-029 the pinned trainer already has the shortened polls, so :poll (which patches a copy) only
# applies to a trainer from before commit c5784a5; on the patched one its exact-match check stops the job.
#
#   mkdir -p ~/beanRant/logs
#   S=~/beanRant/FPS-Agent/cluster/slurm/spikeB.sbatch
#   sbatch -t 30 --export=ALL,MODE=sim,RESUME_FROM=<dir with the 4 .bin> $S
#   sbatch -t 60 --gres=gpu:1 --export=ALL,MODE=train,ARENAS=32 $S
#   sbatch -t 120 --gres=gpu:1 --export=ALL,MODE=train,CONFIGS="1x1:poll 8x8:poll 1x1:cpu 8x8:cpu" $S
#   sbatch -t 30 --gres=none --export=ALL,STAGE=S0,BUILD_SHA=10520b5,MODE=sim,RESUME_FROM=<dir>,CONFIGS="1x32@0 1x32@1" $S
#   sbatch -t 120 --gres=gpu:1 -c 48 --export=ALL,STAGE=S0,BUILD_SHA=10520b5,MODE=train,ARENAS=32,CONFIGS="4x4 8x8" $S
#SBATCH --job-name=spikeB
#SBATCH -p gpuq
#SBATCH -N 1
#SBATCH --cpus-per-task=32
#SBATCH --mem=64G                 # not enforced (CR_CORE); documents the need
#SBATCH --time=01:00:00
#SBATCH --output=/home/%u/beanRant/logs/%x-%j.out
set -uo pipefail

MODE=${MODE:-sim}
STAGE=${STAGE:-SpikeA}            # SpikeA or S0
BUILD_SHA=${BUILD_SHA:-07eac52}
SIM_SCRIPTED=${SIM_SCRIPTED:-0}   # S0 sim: the -Scripted reference instead of RESUME_FROM's policy
SET=${SET:-}                      # S0 train: -Set=... overrides (D-025)
RESUME_FROM=${RESUME_FROM:-}      # sim: trained snapshots, so agents act like trained agents
ARENAS=${ARENAS:-32}              # train: arenas per game
BATCHES=${BATCHES:-25}            # train: experience batches per config
SECONDS_EACH=${SECONDS_EACH:-45}  # sim: seconds per config
CONFIG_LIMIT=${CONFIG_LIMIT:-600} # train: give up on a config after this many seconds
CONNECT_TIMEOUT=${CONNECT_TIMEOUT:-120}
if [ "$MODE" = sim ]; then CONFIGS=${CONFIGS:-"1x1 1x4 1x16 1x64 1x256 2x64 4x64 8x64 16x64 32x64 32x16"}
else CONFIGS=${CONFIGS:-"1x1 2x1 2x2 4x1 4x4 8x1 8x8 16x16"}; fi

BEAN=$HOME/beanRant
REPO=$BEAN/FPS-Agent
BUILD=$BEAN/builds/$BUILD_SHA/Linux
RUN_ID=spikeB-$(date +%Y%m%d)-$SLURM_JOB_ID
RUN=$BEAN/runs/$RUN_ID
GAME_HOME=$RUN/home               # the engine writes its user files here, not in ~ (D-021)
mkdir -p "$RUN" "$GAME_HOME"
cd "$RUN"
unset PYTHONPATH PYTHONHOME VIRTUAL_ENV
export PYTHONNOUSERSITE=1
export XDG_CACHE_HOME=$BEAN/.cache CUDA_CACHE_PATH=$BEAN/.cache/nv

T0=$(date +%s)
{
  echo "run_id=$RUN_ID mode=$MODE stage=$STAGE job=$SLURM_JOB_ID host=$(hostname) start=$(date -Is)"
  echo "set=${SET:-none} sim_scripted=$SIM_SCRIPTED"
  echo "build=$BUILD_SHA repo_commit=$(git -C "$REPO" rev-parse --short HEAD) configs=\"$CONFIGS\""
  echo "arenas=$ARENAS batches=$BATCHES seconds_each=$SECONDS_EACH resume_from=${RESUME_FROM:-none}"
  echo "cpus=${SLURM_CPUS_PER_TASK:-?} cuda_visible_devices=${CUDA_VISIBLE_DEVICES:-none} nproc=$(nproc)"
} | tee "$RUN/run_info.txt"
[ -x "$BUILD/FPSAgent.sh" ] || { echo "missing build: $BUILD/FPSAgent.sh"; exit 1; }

# Every process of a config has "$D/" on its command line (-abslog / trainer dir): that selects them.
sampler() {  # CPU seconds, RSS and threads of this config's processes every 5 s
  while :; do
    echo "T $(date +%s.%N)"
    ps -o pid=,times=,rss=,nlwp=,args= -u "$USER" | grep -F -- "$1/" | grep -v -e grep -e ' ps ' | cut -c1-200
    sleep 5
  done
}
cleanup_config() { pkill -f -- "$1/" 2>/dev/null; sleep 2; pkill -9 -f -- "$1/" 2>/dev/null; true; }
SAMPLER=
trap '[ -n "$SAMPLER" ] && kill "$SAMPLER" 2>/dev/null; pkill -f -- "$RUN/" 2>/dev/null' EXIT

if [ "$MODE" = sim ]; then
  for C in $CONFIGS; do
    SPEC=${C%@*}; LVL=0; [[ $C == *@* ]] && LVL=${C#*@}
    N=${SPEC%x*}; K=${SPEC#*x}; D=$RUN/sim_n${N}_k${K}$([ "$STAGE" = S0 ] && echo "_l$LVL"); mkdir -p "$D"
    echo "config=$C n=$N k=$K level=$LVL stage=$STAGE start=$(date +%s.%N)" > "$D/info.txt"
    if [ "$STAGE" = S0 ]; then
      # Fixed level, no ramp: -Eval with more episodes than fit in SECONDS_EACH.
      SIMARGS=(-Stage=S0 -Eval -EvalEpisodes=1000000 -StartDifficulty="$LVL")
      if [ "$SIM_SCRIPTED" = 1 ]; then SIMARGS+=(-Scripted); else SIMARGS+=(-ResumeFrom="$RESUME_FROM"); fi
    else
      SIMARGS=(-Stage=SpikeA -Infer ${RESUME_FROM:+-ResumeFrom="$RESUME_FROM"})
    fi
    TPIDS=()
    for i in $(seq 0 $((N - 1))); do
      HOME=$GAME_HOME timeout "$SECONDS_EACH" "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended "${SIMARGS[@]}" \
        -Arenas="$K" -Seed=$((SLURM_JOB_ID * 1000 + i)) \
        -abslog="$D/game_$i.log" > /dev/null 2>&1 &
      TPIDS+=($!)
    done
    sampler "$D" > "$D/cpu.txt" & SAMPLER=$!
    wait "${TPIDS[@]}"
    kill "$SAMPLER"; wait "$SAMPLER" 2> /dev/null; SAMPLER=
    for _ in $(seq 1 60); do pgrep -f -- "$D/" > /dev/null || break; sleep 1; done   # engines finish exiting
    cleanup_config "$D"
    echo "end=$(date +%s.%N)" >> "$D/info.txt"
    echo "sim $C done at $(( $(date +%s) - T0 )) s"
  done
else
  PY=$BEAN/envs/la-train/bin/python
  export PATH=$BEAN/envs/la-train/bin:$PATH
  PORT0=$((40000 + SLURM_JOB_ID % 20000))
  STOCK=$REPO/cluster/learning_agents_python
  PATCHED=$RUN/trainer_poll_patch
  if [[ " $CONFIGS" == *:poll* ]]; then
    # A copy of the pinned trainer with the three per-batch 1 s polls shortened (measurement only).
    cp -r "$STOCK" "$PATCHED" && rm -rf "$PATCHED"/learning_agents/__pycache__ "$PATCHED"/learning_agents/communicators/__pycache__
    "$PY" -I - "$PATCHED/learning_agents/communicators/socket_communicator.py" <<'PYEOF' || { echo "poll patch failed"; exit 1; }
import sys
p = sys.argv[1]
s = open(p).read()
edits = [  # (old, new): each old text must occur exactly once
    ("                if len(buffers) < self.min_batch_size:\n                    time.sleep(1)",
     "                if len(buffers) < self.min_batch_size:\n                    time.sleep(0.01)"),   # receive_experience
    ("        while self.sender_active:\n            time.sleep(1)",
     "        while self.sender_active:\n            time.sleep(0.05)"),                           # _sender (also pings)
    ("                    time.sleep(1)\n                    continue",
     "                    time.sleep(0.01)\n                    continue"),                          # _worker
]
for old, new in edits:
    assert s.count(old) == 1, f"expected exactly one match for: {old!r}"
    s = s.replace(old, new)
open(p, "w").write(s)
print("poll patch applied:", p)
PYEOF
    diff "$STOCK/learning_agents/communicators/socket_communicator.py" "$PATCHED/learning_agents/communicators/socket_communicator.py" > "$RUN/trainer_poll_patch.diff"
  fi
  IDX=0
  for C in $CONFIGS; do
    # Own folder, port and seeds per config, so a config listed twice is a repeat with new seeds.
    SPEC=${C%%:*}; OPTS=${C#"$SPEC"}
    N=${SPEC%x*}; BS=${SPEC#*x}
    DEV=gpu; [[ $OPTS == *:cpu* ]] && DEV=cpu
    POLL=stock; TDIR=$STOCK; [[ $OPTS == *:poll* ]] && { POLL=patched; TDIR=$PATCHED; }
    D=$RUN/train_$(printf %02d "$IDX")_n${N}_bs${BS}_${DEV}$([ "$POLL" = patched ] && echo _poll); mkdir -p "$D"
    PORT=$((PORT0 + IDX)); SEED0=$((SLURM_JOB_ID * 1000 + IDX * 50)); IDX=$((IDX + 1))
    echo "config=$C n=$N batch_size=$BS arenas=$ARENAS batches=$BATCHES port=$PORT seed0=$SEED0 device=$DEV poll=$POLL stage=$STAGE" > "$D/info.txt"
    # :cpu hides the GPU; the trainer then logs "Defaulting to CPU training" and continues.
    if [ "$DEV" = cpu ]; then VIS=(env CUDA_VISIBLE_DEVICES=); else VIS=(env); fi
    # Trainer first; each output line gets an epoch timestamp (ms) for the batch timing.
    "${VIS[@]}" "$PY" -s -u "$TDIR/train.py" FPSAgent -l \
      --nne-cpu-path "$TDIR/nne_runtime_basic_cpu" \
      Socket 127.0.0.1:$PORT "$D/trainer" --batch-size "$BS" \
      > >(while IFS= read -r line; do printf '%s %s\n' "$(date +%s.%3N)" "$line"; done > "$D/trainer.log") 2>&1 &
    TRAINER=$!
    NVS=
    if [ "$DEV" = gpu ]; then
      nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used --format=csv -lms 1000 > "$D/gpu.csv" 2>&1 &
      NVS=$!
    fi
    # Never probe the port (D-020): wait for the listener line.
    for _ in $(seq 1 180); do grep -q "Server listening on" "$D/trainer.log" && break; sleep 1; done
    grep -q "Server listening on" "$D/trainer.log" || { echo "$C: no listener after 180 s"; cleanup_config "$D"; [ -n "$NVS" ] && kill "$NVS"; continue; }
    echo "launch=$(date +%s.%N)" >> "$D/info.txt"
    GAMES=()
    for i in $(seq 0 $((N - 1))); do
      STAGEARGS=(-Stage="$STAGE")
      [ "$STAGE" = S0 ] && STAGEARGS+=(-StatsCsv="$D/stats_$i.csv" ${SET:+-Set="$SET"})
      HOME=$GAME_HOME "$BUILD/FPSAgent.sh" -nullrhi -nosound -unattended "${STAGEARGS[@]}" -Arenas="$ARENAS" \
        -Seed=$((SEED0 + i)) -Batches="$BATCHES" \
        -LearningAgentsUseExternalTrainingProcess -LearningAgentsIpAddress=127.0.0.1 \
        -LearningAgentsPort=$PORT -LearningAgentsTimeout="$CONNECT_TIMEOUT" \
        -abslog="$D/game_$i.log" > /dev/null 2>&1 &
      GAMES+=($!)
    done
    sampler "$D" > "$D/cpu.txt" & SAMPLER=$!
    DEADLINE=$(( $(date +%s) + CONFIG_LIMIT ))
    for p in "${GAMES[@]}"; do
      while kill -0 "$p" 2>/dev/null && [ "$(date +%s)" -lt "$DEADLINE" ]; do sleep 1; done
    done
    LIMIT_HIT=0
    for p in "${GAMES[@]}"; do kill -0 "$p" 2>/dev/null && LIMIT_HIT=1; done
    [ "$LIMIT_HIT" = 1 ] && { echo "$C: hit the ${CONFIG_LIMIT} s limit"; pkill -f -- "-abslog=$D/"; }
    RCS=(); for p in "${GAMES[@]}"; do wait "$p"; RCS+=("$?"); done
    echo "games_end=$(date +%s.%N) game_exit_codes=${RCS[*]} limit_hit=$LIMIT_HIT" >> "$D/info.txt"
    for _ in $(seq 1 120); do kill -0 "$TRAINER" 2>/dev/null || break; sleep 1; done
    kill -0 "$TRAINER" 2>/dev/null && { echo "$C: trainer still running 120 s after the games: stopping it"; kill "$TRAINER"; }
    wait "$TRAINER"; echo "trainer_exit=$?" >> "$D/info.txt"
    kill "$SAMPLER" $NVS 2>/dev/null; wait "$SAMPLER" $NVS 2> /dev/null; SAMPLER=
    cleanup_config "$D"
    echo "train $C: games ${RCS[*]}, $(grep -c 'Iter:' "$D/trainer.log") batches, done at $(( $(date +%s) - T0 )) s"
  done
fi

"$BEAN/envs/la-train/bin/python" -I "$REPO/cluster/scripts/spikeB_summarize.py" "$RUN"
echo "== done after $(( $(date +%s) - T0 )) s"
