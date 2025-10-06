#!/usr/bin/env bash
set -u  # fail on undefined vars; don't use -e so we keep going
# set -x  # uncomment if you want shell tracing in per-workload logs

# ------------------------------------------------------------------------------
# Usage:
#   APPBENCH=/abs/path SHARED_LIBS=/abs/path ./compile_all.sh            # build all
#   APPBENCH=/abs/path SHARED_LIBS=/abs/path ./compile_all.sh list       # list apps
#   APPBENCH=/abs/path SHARED_LIBS=/abs/path ./compile_all.sh graph500   # single
#   APPBENCH=/abs/path SHARED_LIBS=/abs/path ./compile_all.sh redis-3.0.0 phoenix-2.0_core
#
# Env knobs you can override:
#   MPICC=mpicc    JOBS=<parallelism>   LOG_TS_FMT='+%F %T'
# ------------------------------------------------------------------------------

# -------- config --------
BASE="${APPBENCH:?APPBENCH not set}"
LOG_DIR="$BASE/build-logs/$(date +%Y%m%d-%H%M%S)"
MASTER_LOG="$LOG_DIR/build.log"
SUMMARY="$LOG_DIR/summary.csv"

MPICC="${MPICC:-mpicc}"
JOBS="${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 8)}"
LOG_TS_FMT="${LOG_TS_FMT:-+%F %T}"

mkdir -p "$LOG_DIR"
: > "$MASTER_LOG"
echo "workload,status,timestamp,log_path" > "$SUMMARY"

log_note () { echo "[$(date "$LOG_TS_FMT")] $*" | tee -a "$MASTER_LOG"; }

build_step () {
  local name="$1"; shift
  local cmd="$*"
  local wl_log="$LOG_DIR/${name//\//_}.log"

  log_note "=== START: $name ==="
  {
    echo "### $(date "$LOG_TS_FMT") :: $name"
    echo "### CMD: $cmd"
    echo
    ( set -x; bash -lc "$cmd" )
  } >"$wl_log" 2>&1
  local rc=$?

  {
    echo
    echo "----- LOG [$name] BEGIN -----"
    cat "$wl_log"
    echo "----- LOG [$name] END -----"
    echo
  } >> "$MASTER_LOG"

  if (( rc == 0 )); then
    log_note "=== SUCCESS: $name (rc=0) ==="
    echo "$name,SUCCESS,$(date "$LOG_TS_FMT"),$wl_log" >> "$SUMMARY"
  else
    log_note "=== FAIL: $name (rc=$rc) ==="
    echo "$name,FAIL,$(date "$LOG_TS_FMT"),$wl_log" >> "$SUMMARY"
  fi

  return 0  # never stop the whole script
}

# -------- task registry (ordered) --------
declare -a ORDER=()
declare -A TASKS_CMD=()

add_task () {
  local key="$1"; shift
  local cmd="$*"
  ORDER+=("$key")
  TASKS_CMD["$key"]="$cmd"
}

# -------- prelude --------
log_note "Using MPICC=$MPICC, parallel jobs=$JOBS"
touch "$BASE/dummy.txt"

# -------- define tasks --------
add_task "INSTALL_SHAREDLIB_hoardlib" \
"cd \"$SHARED_LIBS/hoardlib\" && ./compile_install_hoard.sh"

add_task "INSTALL_SHAREDLIB_mmap_lib" \
"cd \"$SHARED_LIBS/mmap_lib\" && make clean && make -j\"$JOBS\" && sudo make install"

# Redis (bypass optional deps if missing)
add_task "redis-3.0.0" \
"cd \"$BASE/redis-3.0.0/src\" && make distclean >/dev/null 2>&1 || true && make clean && make IGNORE_MISSING_DEPS=1 -j\"$JOBS\" all"

# Phoenix 2.0
add_task "phoenix-2.0_core" \
"cd \"$BASE/phoenix-2.0\" && make clean && make -j\"$JOBS\""

add_task "phoenix-2.0_word_count" \
"cd \"$BASE/phoenix-2.0/tests/word_count\" && rm -rf results tmp*.txt && mkdir -p results && make clean && make -j\"$JOBS\" && cp \"$BASE/dummy.txt\" tmp1.txt"

# GraphChi
add_task "graphchi" \
"cd \"$BASE/graphchi/graphchi-cpp\" && make clean && make -j\"$JOBS\""

# Metis
add_task "Metis" \
"cd \"$BASE/Metis\" && make clean && ./configure && make -j\"$JOBS\""

# LevelDB
add_task "leveldb" \
"cd \"$BASE/leveldb\" && make clean && make -j\"$JOBS\""

# GTC benchmark
add_task "gtc" \
"cd \"$BASE/gtc\" && scripts/compile_gtc.sh"

# Graph500 (auto-detect dir; try common MPI targets with MPICC)
add_task "graph500" "
_g500() {
  set -e
  local G500_DIR=''
  for d in \
    \"$BASE/graph500\" \
    \"$BASE/graph500-reference\" \
    \"$BASE/Graph500\" \
    \"$BASE\"/graph500-* \
    \"$BASE\"/Graph500-* ; do
    [ -d \"\$d\" ] && { G500_DIR=\"\$d\"; break; }
  done
  if [ -z \"\$G500_DIR\" ]; then
    echo 'Graph500 directory not found under $BASE' >&2
    exit 2
  fi
  echo \"Detected Graph500 at: \$G500_DIR\"
  cd \"\$G500_DIR\"
  [ -d src ] && cd src || true
  make distclean >/dev/null 2>&1 || true
  make clean || true
  export CC=\"$MPICC\" MPICC=\"$MPICC\"
  if  make -j\"$JOBS\" CC=\"\$CC\" MPICC=\"\$MPICC\" mpi ; then
    :
  elif make -j\"$JOBS\" CC=\"\$CC\" MPICC=\"\$MPICC\" reference_bfs ; then
    :
  elif make -j\"$JOBS\" CC=\"\$CC\" MPICC=\"\$MPICC\" graph500_reference_bfs ; then
    :
  elif make -j\"$JOBS\" CC=\"\$CC\" MPICC=\"\$MPICC\" all ; then
    :
  else
    echo 'Graph500 build failed: no common target succeeded' >&2
    exit 3
  fi
  echo; echo '----- Graph500 binaries discovered -----'
  ls -l ../graph500_* graph500_* bfs* 2>/dev/null || true
  echo '----------------------------------------'
}; _g500
"

# -------- selection logic --------
print_list() {
  printf '%s\n' "${ORDER[@]}"
}

lower() { printf '%s' "$1" | tr 'A-Z' 'a-z'; }

resolve_names() {
  # maps user args to canonical keys in ORDER
  local arg key larg lkey matches
  for arg in "$@"; do
    if [[ "$arg" == "all" ]]; then
      printf '%s\0' "${ORDER[@]}"; continue
    fi
    if [[ "$arg" == "list" || "$arg" == "--list" || "$arg" == "-l" ]]; then
      print_list; exit 0
    fi
    matches=()
    larg="$(lower "$arg")"
    # exact (case-insensitive)
    for key in "${ORDER[@]}"; do
      lkey="$(lower "$key")"
      if [[ "$lkey" == "$larg" ]]; then
        matches=("$key"); break
      fi
    done
    # substring if no exact
    if [[ ${#matches[@]} -eq 0 ]]; then
      for key in "${ORDER[@]}"; do
        lkey="$(lower "$key")"
        [[ "$lkey" == *"$larg"* ]] && matches+=("$key")
      done
    fi
    if [[ ${#matches[@]} -eq 0 ]]; then
      echo "Unknown app: '$arg'"; echo "Available:"; print_list; exit 1
    elif [[ ${#matches[@]} -gt 1 ]]; then
      echo "Ambiguous app '$arg' matches: ${matches[*]}"; exit 1
    else
      printf '%s\0' "${matches[0]}"
    fi
  done
}

# Determine what to run
if [[ $# -eq 0 ]]; then
  SELECTED=("${ORDER[@]}")
else
  # read NUL-delimited keys from resolver
  mapfile -d '' -t SELECTED < <(resolve_names "$@")
fi

# -------- run selected tasks --------
for name in "${SELECTED[@]}"; do
  build_step "$name" "${TASKS_CMD[$name]}"
done

log_note "All requested steps attempted. Summary at: $SUMMARY"

# Uncomment if you want a failing overall exit when any workload failed
# grep -q ',FAIL,' "$SUMMARY" && exit 1 || exit 0

