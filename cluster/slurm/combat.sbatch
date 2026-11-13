#!/bin/bash
# Combat stages S0/S1 on Cradle (requests L-007, L-009; README -> "Game command-line arguments", "Game log lines to grep",
# "Game exit codes"). Paths per D-021 (docs/cradle.md -> "Our paths"): everything under ~/beanRant.
# STAGE=S0|S1 (default S0) picks the combat stage for every mode; BUILD_SHA picks the build (S1 needs 0eef702+).
#   MODE=smoke  L-007 (1) / L-009 (1), all in one job: train 3 batches; resume those snapshots for 2 batches; resume
#               them with -Eval -EvalEpisodes=20; then check that -Stage=S2 and a bad -Set exit with code 2.
#               Optional: SMOKE_LOAD_SNAP=<dir> (a snapshot of the same network, e.g. the promoted S0 one) must load
#               into STAGE (-Eval, 20 episodes, exit 0); SMOKE_OTHER_SNAP=<dir> (another network, e.g. Spike A's)
#               must exit with code 2 and log "doesn't match this build's network" (build-0eef702+).
#   MODE=train  external trainer (--batch-size BATCH_SIZE, default NUM_GAMES) + NUM_GAMES games x ARENAS arenas,
#               BATCHES experience batches. Chainable: give every job of a chain the same RUN_ID. Each job works
#               in $RUN/job_<jobid>/, the games append to $RUN/stats_<i>.csv, and the job adds one line to
#               $RUN/chain.txt (snapshots, last mean level=, promotion trigger). Without RESUME_FROM /
#               START_DIFFICULTY a job continues from the last chain.txt line. After the promotion trigger
#               (mean level >= PROMOTE_LEVEL over the last PROMOTE_WINDOWS windows of all games, and the pooled
#               clearTop over the same windows >= PROMOTE_CLEARTOP with >= PROMOTE_MIN_TOP gate-level episodes;
#               D-031 (6), (11)) or a failed job, later jobs of the chain exit at once. Start a new RUN_ID per run:
#               build-A CSVs have no clearTop columns and never promote.
#   MODE=eval   one game, no trainer: -Eval -EvalEpisodes=EVAL_EPISODES -StartDifficulty=START_DIFFICULTY
#               -Seed=SEED -Arenas=ARENAS from RESUME_FROM, or the -Scripted reference with SCRIPTED=1.
#   MODE=probe  -Stage=SpikeC mechanics probe (C-006).
#
#   mkdir -p ~/beanRant/logs
#   S=~/beanRant/FPS-Agent/cluster/slurm/combat.sbatch
#   Partition: always -p <partition> (gpuq is off-limits); e.g. -p gpul40q.
#   sbatch -p gpul40q -t 30 --export=ALL,MODE=smoke,STAGE=S1 $S
#   J=$(sbatch --parsable --export=ALL,MODE=train,NUM_GAMES=4,ARENAS=32,BATCHES=300 $S)          # first job: S0-<date>-$J
#   sbatch --dependency=afterany:$J --export=ALL,MODE=train,RUN_ID=S0-<date>-$J,NUM_GAMES=4,ARENAS=32,BATCHES=300 $S  # next ones
#   sbatch --gres=none -c 4 -t 30 --export=ALL,MODE=eval,RESUME_FROM=<dir>,START_DIFFICULTY=1,SEED=1000 $S
#   sbatch --gres=none -c 4 -t 30 --export=ALL,MODE=eval,SCRIPTED=1,START_DIFFICULTY=1,SEED=1000 $S
#   sbatch --gres=none -c 2 -t 10 --export=ALL,MODE=probe $S
#SBATCH --job-name=combat
# Partition: pass -p <partition> to sbatch (docs/cradle.md). gpuq is off-limits until further notice (user, 2026-11-13).
#SBATCH -N 1
#SBATCH --gres=gpu:1
#SBATCH --cpus-per-task=8
#SBATCH --mem=32G                 # not enforced (CR_CORE); documents the need
#SBATCH --time=01:00:00           # always set a time limit
#SBATCH --signal=B:USR1@300       # 5 min warning before the time limit: stop the games, keep the snapshots
#SBATCH --output=/home/%u/beanRant/logs/%x-%j.out
set -uo pipefail
[ "${SLURM_JOB_PARTITION:-}" = gpuq ] && { echo "gpuq is off-limits until further notice (docs/cradle.md); resubmit with -p <other partition>"; exit 1; }

MODE=${MODE:-smoke}
STAGE=${STAGE:-S0}                # combat stage for train, smoke and eval
BUILD_SHA=${BUILD_SHA:-0eef702}   # build B (S0 unchanged + S1); L-008 pins 10520b5
NUM_GAMES=${NUM_GAMES:-2}
ARENAS=${ARENAS:-16}
BATCH_SIZE=${BATCH_SIZE:-$NUM_GAMES}
BATCHES=${BATCHES:-3}
RESUME_FROM=${RESUME_FROM:-}
START_DIFFICULTY=${START_DIFFICULTY:-}
SET=${SET:-}                      # -Set=... overrides (D-025), e.g. "Combat.Ppo.ReplayMaxSteps=20000"
EVAL_EPISODES=${EVAL_EPISODES:-200}
SEED=${SEED:-1000}                # eval seed; the episode seeds depend on -Seed and -Arenas
SCRIPTED=${SCRIPTED:-0}
EVAL_LIMIT=${EVAL_LIMIT:-1800}    # eval: stop the game after this many seconds
PROMOTE_LEVEL=${PROMOTE_LEVEL:-0.9}
PROMOTE_WINDOWS=${PROMOTE_WINDOWS:-10}
PROMOTE_CLEARTOP=${PROMOTE_CLEARTOP:-0.70}
PROMOTE_MIN_TOP=${PROMOTE_MIN_TOP:-50}   # fewer gate-level episodes than this: don't call it
SMOKE_LOAD_SNAP=${SMOKE_LOAD_SNAP:-}
SMOKE_OTHER_SNAP=${SMOKE_OTHER_SNAP:-}
GAME_STDOUT=${GAME_STDOUT:-0}     # 1: keep each game's stdout (it repeats the log) in game_<i>.stdout
GZIP_LOGS=${GZIP_LOGS:-1}         # train: gzip the game logs at the end of the job (after the summary)
LOGCMDS=${LOGCMDS:-}              # train: passed as -LogCmds=..., e.g. "LogLearning Warning" hides Learning Agents' Display lines
CONNECT_TIMEOUT=${CONNECT_TIMEOUT:-120}

BEAN=$HOME/beanRant
REPO=$BEAN/FPS-Agent
BUILD=$BEAN/builds/$BUILD_SHA/Linux
case $MODE in
  train) RUN_ID=${RUN_ID:-$STAGE-$(date +%Y%m%d)-$SLURM_JOB_ID} ;;   # docs/runs/README.md convention
  eval)  RUN_ID=${RUN_ID:-${STAGE,,}eval-$(date +%Y%m%d)-$SLURM_JOB_ID} ;;
  *)     RUN_ID=${RUN_ID:-${STAGE,,}$MODE-$(date +%Y%m%d)-$SLURM_JOB_ID} ;;
esac
RUN=$BEAN/runs/$RUN_ID
GAME_HOME=$RUN/home               # the engine writes its user files here, not in ~ (D-021)
mkdir -p "$RUN" "$GAME_HOME"
cd "$RUN"
unset PYTHONPATH PYTHONHOME VIRTUAL_ENV   # this account's login shell exports another project's Python (docs/cradle.md)
export PYTHONNOUSERSITE=1
export XDG_CACHE_HOME=$BEAN/.cache CUDA_CACHE_PATH=$BEAN/.cache/nv
PY=$BEAN/envs/la-train/bin/python
export PATH=$BEAN/envs/la-train/bin:$PATH
REPORT=("$PY" -I "$REPO/cluster/scripts/combat_report.py")
PORT0=$((40000 + SLURM_JOB_ID % 20000))

T0=$(date +%s)
{
  echo "run_id=$RUN_ID mode=$MODE stage=$STAGE partition=${SLURM_JOB_PARTITION:-?} job=$SLURM_JOB_ID host=$(hostname) start=$(date -Is)"
  echo "build=$BUILD_SHA repo_commit=$(git -C "$REPO" rev-parse --short HEAD) games=$NUM_GAMES arenas=$ARENAS batch_size=$BATCH_SIZE"
  echo "batches=$BATCHES resume_from=${RESUME_FROM:-auto} start_difficulty=${START_DIFFICULTY:-auto} set=${SET:-none}"
  echo "eval_episodes=$EVAL_EPISODES seed=$SEED scripted=$SCRIPTED cpus=${SLURM_CPUS_PER_TASK:-?} cuda_visible_devices=${CUDA_VISIBLE_DEVICES:-none}"
} | tee -a "$RUN/run_info.txt"
[ -x "$BUILD/FPSAgent.sh" ] || { echo "missing build: $BUILD/FPSAgent.sh"; exit 1; }

BASE=(-nullrhi -nosound -unattended)
TRAINER_ARGS() { echo -LearningAgentsUseExternalTrainingProcess -LearningAgentsIpAddress=127.0.0.1 -LearningAgentsPort="$1" -LearningAgentsTimeout="$CONNECT_TIMEOUT"; }
SETARG=(); [ -n "$SET" ] && SETARG=(-Set="$SET")
LOGARG=(); [ -n "$LOGCMDS" ] && LOGARG=(-LogCmds="$LOGCMDS")

# Stop handling: USR1 (5 min before the time limit) or TERM (scancel) asks the current games to quit
# (the engine shuts down cleanly on SIGTERM); the step then finishes normally and keeps its snapshots.
STOPPED=0; CUR=
on_stop() { echo "$(date -Is) got a stop signal: stopping the games in ${CUR:-?}"; STOPPED=1; [ -n "$CUR" ] && pkill -TERM -f -- "-abslog=$CUR/"; }
trap on_stop USR1 TERM
trap '[ -n "$CUR" ] && pkill -f -- "$CUR/" 2>/dev/null' EXIT   # leftovers of the current step only

wait_pid() {  # wait for one child, resuming the wait after a trapped signal; returns its exit code
  local rc
  while :; do wait "$1"; rc=$?; { [ $rc -gt 128 ] && kill -0 "$1" 2>/dev/null; } || break; done
  return $rc
}

# One game, no trainer, under a time limit. timeout runs in its own process group; FPSAgent.sh doesn't exec the
# engine, so wait for the group to empty or Slurm kills the engine mid-shutdown (Spike A finding 5).
single() {  # $1 folder, $2 seconds, rest: game args. Sets RC.
  local D=$1 S=$2; shift 2
  mkdir -p "$D"; CUR=$D
  echo "args=$* limit=$S start=$(date +%s.%N)" > "$D/info.txt"
  HOME=$GAME_HOME timeout "$S" "$BUILD/FPSAgent.sh" "${BASE[@]}" "$@" -abslog="$D/game.log" > "$D/game.stdout" 2>&1 &
  local TPID=$!
  wait_pid "$TPID"; RC=$?
  for _ in $(seq 1 60); do pgrep -g "$TPID" > /dev/null || break; sleep 1; done
  pgrep -g "$TPID" > /dev/null && echo "engine still running 60 s after it ended"
  echo "end=$(date +%s.%N) exit=$RC" >> "$D/info.txt"
}

# Trainer + NUM_GAMES games for $2 batches. $1 folder, $3 resume dir ('' = fresh), $4 start difficulty,
# $5 port, $6 stats CSV prefix. Sets GAME_RCS, TRAINER_RC, SNAP (snapshot copy or '').
train_step() {
  local D=$1 B=$2 R=$3 L=$4 P=$5 CSVP=$6
  mkdir -p "$D"; CUR=$D; SNAP=; GAME_RCS=(); TRAINER_RC=
  echo "batches=$B resume_from=${R:-none} start_difficulty=$L port=$P games=$NUM_GAMES arenas=$ARENAS batch_size=$BATCH_SIZE set=${SET:-none} start=$(date +%s.%N)" > "$D/info.txt"
  # Trainer first; every output line gets an epoch timestamp (ms) for the batch timing.
  "$PY" -s -u "$REPO/cluster/learning_agents_python/train.py" FPSAgent -l \
    --nne-cpu-path "$REPO/cluster/learning_agents_python/nne_runtime_basic_cpu" \
    Socket 127.0.0.1:$P "$D/trainer" --batch-size "$BATCH_SIZE" \
    > >(while IFS= read -r line; do printf '%s %s\n' "$(date +%s.%3N)" "$line"; done > "$D/trainer.log") 2>&1 &
  local TRAINER=$!
  nvidia-smi --query-gpu=timestamp,utilization.gpu,memory.used --format=csv -lms 1000 > "$D/gpu.csv" 2>&1 &
  local NVS=$!
  # Never probe the port (D-020): wait for the listener line.
  for _ in $(seq 1 180); do
    grep -q "Server listening on" "$D/trainer.log" && break
    kill -0 "$TRAINER" 2>/dev/null || break
    sleep 1
  done
  if ! grep -q "Server listening on" "$D/trainer.log"; then
    echo "no trainer listener"; tail -30 "$D/trainer.log"; kill "$TRAINER" "$NVS" 2>/dev/null; return 1
  fi
  echo "launch=$(date +%s.%N)" >> "$D/info.txt"
  local GAMES=() i OUT
  for i in $(seq 0 $((NUM_GAMES - 1))); do
    OUT=/dev/null; [ "$GAME_STDOUT" = 1 ] && OUT=$D/game_$i.stdout
    HOME=$GAME_HOME "$BUILD/FPSAgent.sh" "${BASE[@]}" -Stage="$STAGE" -Arenas="$ARENAS" -Seed=$((SLURM_JOB_ID * 1000 + i)) \
      -Batches="$B" ${R:+-ResumeFrom="$R"} -StartDifficulty="$L" -StatsCsv="${CSVP}_$i.csv" "${SETARG[@]}" "${LOGARG[@]}" \
      $(TRAINER_ARGS "$P") -abslog="$D/game_$i.log" > "$OUT" 2>&1 &
    GAMES+=($!)
  done
  for i in "${GAMES[@]}"; do wait_pid "$i"; GAME_RCS+=("$?"); done
  echo "games_end=$(date +%s.%N) game_exit_codes=${GAME_RCS[*]} stopped=$STOPPED" >> "$D/info.txt"
  # The trainer exits on its own after sending "complete"; give it 2 minutes.
  for _ in $(seq 1 120); do kill -0 "$TRAINER" 2>/dev/null || break; sleep 1; done
  kill -0 "$TRAINER" 2>/dev/null && { echo "trainer still running 120 s after the games: stopping it"; kill "$TRAINER"; }
  wait_pid "$TRAINER"; TRAINER_RC=$?
  kill "$NVS" 2>/dev/null; wait "$NVS" 2>/dev/null
  echo "trainer_exit=$TRAINER_RC end=$(date +%s.%N)" >> "$D/info.txt"
  # Keep the final snapshots (the trainer overwrites them in place).
  local S
  S=$(ls -d "$D"/trainer/FPSAgent*/Snapshots 2>/dev/null | head -1)
  if [ -n "$S" ] && [ -f "$S/policy.bin" ]; then cp -a "$S" "$D/snapshots_final"; SNAP=$D/snapshots_final; fi
  echo "step $(basename "$D"): games ${GAME_RCS[*]}, trainer $TRAINER_RC, $(grep -c 'Iter:' "$D/trainer.log") batches, snapshots ${SNAP:-none}, at $(( $(date +%s) - T0 )) s"
  return 0
}

case $MODE in
smoke)
  # (a) fresh, 3 batches
  train_step "$RUN/a_train3" 3 "" 0 "$PORT0" "$RUN/a_train3/stats"
  A_SNAP=$SNAP
  [ -n "$A_SNAP" ] || { echo "smoke: no snapshots after the 3-batch run"; "${REPORT[@]}" step "$RUN/a_train3"; exit 1; }
  A_LEVEL=$("${REPORT[@]}" level "$RUN/a_train3")
  # (b) resume in training, 2 batches, at the last mean level
  [ "$STOPPED" = 0 ] && train_step "$RUN/b_resume2" 2 "$A_SNAP" "$A_LEVEL" $((PORT0 + 1)) "$RUN/b_resume2/stats"
  # (c) resume with -Eval, 20 episodes (no trainer)
  [ "$STOPPED" = 0 ] && single "$RUN/c_eval20" 600 -Stage="$STAGE" -Arenas="$ARENAS" -Seed="$SEED" -Eval -EvalEpisodes=20 \
    -ResumeFrom="$A_SNAP" -StartDifficulty="$A_LEVEL" -EvalJson="$RUN/c_eval20/eval.json"
  # (d) bad arguments must exit with code 2 (a game that tried the trainer instead would exit 1)
  [ "$STOPPED" = 0 ] && single "$RUN/d_stage_s2" 180 -Stage=S2 -Arenas=2 -Seed=1 $(TRAINER_ARGS $((PORT0 + 2)))
  [ "$STOPPED" = 0 ] && single "$RUN/e_bad_set" 180 -Stage="$STAGE" -Arenas=2 -Seed=1 -Set=Combat.Reward.Nope=1 $(TRAINER_ARGS $((PORT0 + 3)))
  # (f) a snapshot of the same network loads into this stage; (g) another network exits with code 2
  [ "$STOPPED" = 0 ] && [ -n "$SMOKE_LOAD_SNAP" ] && single "$RUN/f_load_snap" 600 -Stage="$STAGE" -Arenas="$ARENAS" -Seed="$SEED" \
    -Eval -EvalEpisodes=20 -ResumeFrom="$SMOKE_LOAD_SNAP" -EvalJson="$RUN/f_load_snap/eval.json"
  [ "$STOPPED" = 0 ] && [ -n "$SMOKE_OTHER_SNAP" ] && single "$RUN/g_other_net" 180 -Stage="$STAGE" -Arenas=2 -Seed=1 \
    -Eval -EvalEpisodes=2 -ResumeFrom="$SMOKE_OTHER_SNAP"
  for D in a_train3 b_resume2 c_eval20 d_stage_s2 e_bad_set f_load_snap g_other_net; do [ -d "$RUN/$D" ] && "${REPORT[@]}" step "$RUN/$D"; done | tee "$RUN/summary.md"
  ;;
train)
  JOB=$RUN/job_$SLURM_JOB_ID
  if [ -z "$RESUME_FROM$START_DIFFICULTY" ] && [ -s "$RUN/chain.txt" ]; then
    LAST=$(tail -1 "$RUN/chain.txt")
    get() { sed -n "s/.* $1=\([^ ]*\).*/\1/p" <<< " $LAST"; }
    [ "$(get promoted)" = 1 ] && { echo "chain already promoted ($LAST): nothing to do"; exit 0; }
    [ "$(get ok)" = 1 ] || { echo "previous job of the chain failed ($LAST): stopping the chain"; exit 1; }
    RESUME_FROM=$(get snapshots); START_DIFFICULTY=$(get end_level)
    echo "continuing the chain from: $LAST" | tee -a "$RUN/run_info.txt"
  fi
  START_DIFFICULTY=${START_DIFFICULTY:-0}
  train_step "$JOB" "$BATCHES" "$RESUME_FROM" "$START_DIFFICULTY" "$PORT0" "$RUN/stats" || exit 1
  OK=1
  [ -n "$SNAP" ] || OK=0
  [ "$STOPPED" = 1 ] || for rc in "${GAME_RCS[@]}"; do [ "$rc" = 0 ] || OK=0; done
  read -r END_LEVEL PROMOTED WINDOWS CLEARTOP TOPN <<< "$("${REPORT[@]}" chainstate "$RUN" "$PROMOTE_LEVEL" "$PROMOTE_WINDOWS" "$PROMOTE_CLEARTOP" "$PROMOTE_MIN_TOP")"
  echo "job=$SLURM_JOB_ID ok=$OK batches=$BATCHES iters=$(grep -c 'Iter:' "$JOB/trainer.log") start_difficulty=$START_DIFFICULTY end_level=$END_LEVEL promoted=$PROMOTED windows=$WINDOWS cleartop=$CLEARTOP top_n=$TOPN stopped=$STOPPED games=$(IFS=,; echo "${GAME_RCS[*]}") snapshots=${SNAP:-none} resume_from=${RESUME_FROM:-none} end=$(date -Is)" \
    | tee -a "$RUN/chain.txt"
  "${REPORT[@]}" step "$JOB" | tee "$JOB/summary.md"
  echo "game log sizes: $(du -ch "$JOB"/game_*.log 2>/dev/null | tail -1 | cut -f1)"
  [ "$GZIP_LOGS" = 1 ] && gzip -q "$JOB"/game_*.log
  ;;
eval)
  if [ "$SCRIPTED" = 1 ]; then WHO=(-Scripted); else
    [ -n "$RESUME_FROM" ] || { echo "MODE=eval needs RESUME_FROM (or SCRIPTED=1)"; exit 2; }
    WHO=(-ResumeFrom="$RESUME_FROM"); fi
  D=$RUN/eval_${STAGE,,}_$([ "$SCRIPTED" = 1 ] && echo scripted || echo policy)_l${START_DIFFICULTY:-0}_s${SEED}_k${ARENAS}_j$SLURM_JOB_ID   # job id: a rerun never reuses a folder
  single "$D" "$EVAL_LIMIT" -Stage="$STAGE" -Arenas="$ARENAS" -Seed="$SEED" -Eval -EvalEpisodes="$EVAL_EPISODES" \
    -StartDifficulty="${START_DIFFICULTY:-0}" "${WHO[@]}" "${SETARG[@]}" -EvalJson="$D/eval.json"
  "${REPORT[@]}" step "$D" | tee "$D/summary.md"
  ;;
probe)
  single "$RUN/probe" 300 -Stage=SpikeC -Seed="$SEED"
  "${REPORT[@]}" step "$RUN/probe" | tee "$RUN/probe/summary.md"
  ;;
*) echo "unknown MODE=$MODE"; exit 2 ;;
esac
echo "== done after $(( $(date +%s) - T0 )) s"
