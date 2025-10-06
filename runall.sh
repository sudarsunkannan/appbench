#!/usr/bin/env bash
set -x

# --- config / defaults (keeps original behavior) ---
BASE="${APPBENCH:?APPBENCH not set}"
APPBASE="$BASE"
cd "$BASE" || exit 1

RUNNOW="${RUNNOW:-1}"
# If OUTPUTDIR not set, create a timestamped default under $BASE
OUTPUTDIR="${OUTPUTDIR:-$BASE/outputs/$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUTPUTDIR"
#rm -f "$OUTPUTDIR"/* 2>/dev/null || true

USAGE(){
  echo "Usage: $0 [maxhotpage] [BW] [outputdir] [app]"
  echo "If [app] is omitted, runs all apps."
}

RUNAPP(){
  echo "$APPBASE"
  cd "$APPBASE" || exit 2
  if [[ ! -x "$APPBASE/run.sh" ]]; then
    echo "ERROR: run.sh not found or not executable in $APPBASE" >&2
    exit 3
  fi
  # Per-app log goes to $OUTPUTDIR/$APP (stdout+stderr)
  "$APPBASE/run.sh" "$RUNNOW" "$OUTPUTDIR/$APP" &> "$OUTPUTDIR/$APP"
}

# ---- embed graph500 run.sh if missing (no overwrite) ----
embed_graph500_runsh() {
  local gdir="$BASE/graph500/src"
  [[ -d "$gdir" ]] || return 0
  [[ -f "$gdir/run.sh" ]] && return 0
  cat >"$gdir/run.sh" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
# Args:
#   $1 = RUNNOW (unused but kept for compatibility)
#   $2 = OUTPUT_BASE (path prefix for artifacts)
RUNNOW="${1:-1}"
OUT_BASE="${2:-/tmp/graph500}"
OUT_DIR="$(dirname "$OUT_BASE")"
mkdir -p "$OUT_DIR"

MPIEXEC="${MPIEXEC:-mpiexec}"         # or mpirun
NP="${NP:-${SLURM_NTASKS:-}}"
SCALE="${SCALE:-26}"
EDGEFACTOR="${EDGEFACTOR:-16}"
OMP_NUM_THREADS="${OMP_NUM_THREADS:-1}"
MPI_OPTS="${MPI_OPTS:-}"
HOSTFILE="${HOSTFILE:-}"

if [[ -z "${NP}" ]]; then
  NP="$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)"
fi

if [[ -n "$HOSTFILE" && -f "$HOSTFILE" ]]; then
  if command -v ompi_info >/dev/null 2>&1; then
    MPI_OPTS="$MPI_OPTS --hostfile $HOSTFILE"
  else
    MPI_OPTS="$MPI_OPTS -f $HOSTFILE"
  fi
fi

find_bin() {
  for b in \
    ./graph500_reference_bfs \
    ./graph500_mpi_simple \
    ./graph500_mpi \
    ./graph500_bfs \
    ./bfs* ; do
    [[ -x "$b" ]] && { echo "$b"; return; }
  done
  return 1
}

BIN="$(find_bin)" || { echo "No Graph500 BFS binary found in $(pwd)" >&2; exit 2; }

CMD="$MPIEXEC -np $NP $MPI_OPTS $BIN $SCALE $EDGEFACTOR"

META="$OUT_BASE.meta"
{
  echo "datetime: $(date '+%F %T')"
  echo "bin: $BIN"
  echo "np: $NP"
  echo "scale: $SCALE"
  echo "edgefactor: $EDGEFACTOR"
  echo "omp_num_threads: $OMP_NUM_THREADS"
  echo "mpiexec: $MPIEXEC"
  echo "mpi_opts: $MPI_OPTS"
} > "$META"

echo "Running: $CMD"
export OMP_NUM_THREADS
exec bash -lc "$CMD"
EOF
  chmod +x "$gdir/run.sh"
}

# --- single app runner helpers (reuse your original paths) ---
run_graph500(){
  embed_graph500_runsh
  APPBASE="$BASE/graph500/src"
  APP="graph500"
  echo "running $APP..."
  RUNAPP
}

run_gtc(){
  APPBASE="$BASE/gtc"
  APP="gtc"
  echo "running $APP..."
  RUNAPP
}

run_metis(){
  APPBASE="$BASE/Metis"
  APP="Metis"
  echo "running $APP..."
  RUNAPP
}

run_graphchi(){
  APPBASE="$BASE/graphchi"
  APP="graphchi"
  echo "running $APP ..."
  RUNAPP
  /bin/rm -rf com-orkut.ungraph.txt.* 2>/dev/null || true
}

run_redis(){
  APPBASE="$BASE/redis-3.0.0/src"
  APP="redis"
  echo "running $APP..."
  RUNAPP
}

run_leveldb(){
  APPBASE="$BASE/leveldb"
  APP="leveldb"
  echo "running $APP..."
  RUNAPP
}

run_xstream(){
  APPBASE="$BASE/xstream_release"
  APP="xstream_release"
  # optional ini sync if envs are set
  if [[ -n "${HOSTIP:-}" && -n "${SHARED_DATA:-}" ]]; then
    scp -r "$HOSTIP:${SHARED_DATA}"*.ini "$APPBASE" 2>/dev/null || true
    cp "$APPBASE"/*.ini "$SHARED_DATA" 2>/dev/null || true
  fi
  echo "running $APP ..."
  RUNAPP
}

# --- selection: run one app (if provided) or all apps ---
# Back-compat: if $4 present, use that as the app; else also accept $1
APP_ARG="${4:-${1:-}}"

if [[ -n "$APP_ARG" ]]; then
  case "${APP_ARG,,}" in
    graph500|g500) run_graph500 ;;
    gtc)           run_gtc ;;
    metis)         run_metis ;;
    graphchi)      run_graphchi ;;
    redis)         run_redis ;;
    leveldb)       run_leveldb ;;
    xstream* )     run_xstream ;;
    *)
      echo "Unknown app: $APP_ARG"
      echo "Valid: graph500 gtc Metis graphchi redis leveldb xstream_release"
      exit 1
      ;;
  esac
  exit 0
fi

# --- no app provided: run ALL (no early exits) ---
run_graph500
run_gtc
run_metis
run_graphchi
run_redis
run_leveldb
run_xstream
