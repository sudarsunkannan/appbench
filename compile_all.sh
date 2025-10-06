#!/usr/bin/env bash
set -u  # fail on undefined vars; don't use -e so we keep going
# set -x  # uncomment if you want shell tracing in per-workload logs

# -------- config --------
BASE="${APPBENCH:?APPBENCH not set}"
LOG_DIR="$BASE/build-logs/$(date +%Y%m%d-%H%M%S)"
MASTER_LOG="$LOG_DIR/build.log"
SUMMARY="$LOG_DIR/summary.csv"

mkdir -p "$LOG_DIR"
: > "$MASTER_LOG"
echo "workload,status,timestamp,log_path" > "$SUMMARY"

log_note () { echo "[$(date '+%F %T')] $*" | tee -a "$MASTER_LOG"; }

build_step () {
  local name="$1"; shift
  local cmd="$*"
  local wl_log="$LOG_DIR/${name//\//_}.log"

  log_note "=== START: $name ==="
  {
    echo "### $(date '+%F %T') :: $name"
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
    echo "$name,SUCCESS,$(date '+%F %T'),$wl_log" >> "$SUMMARY"
  else
    log_note "=== FAIL: $name (rc=$rc) ==="
    echo "$name,FAIL,$(date '+%F %T'),$wl_log" >> "$SUMMARY"
  fi

  return 0  # never stop the whole script
}

# -------- prelude --------
touch "$BASE/dummy.txt"

# -------- steps --------
build_step "INSTALL_SHAREDLIB_hoardlib" \
  "cd \"$SHARED_LIBS/hoardlib\" && ./compile_install_hoard.sh"

build_step "INSTALL_SHAREDLIB_mmap_lib" \
  "cd \"$SHARED_LIBS/mmap_lib\" && make clean && make && sudo make install"

build_step "redis-3.0.0" \
  "cd \"$BASE/redis-3.0.0/src\" && make clean && make all -j8"


build_step "phoenix-2.0_core" \
  "cd \"$BASE/phoenix-2.0\" && make clean && make -j4"

build_step "phoenix-2.0_word_count" \
  "cd \"$BASE/phoenix-2.0/tests/word_count\" && rm -rf results tmp*.txt && mkdir -p results && make clean && make -j4 && cp \"$BASE/dummy.txt\" tmp1.txt"

build_step "graphchi" \
  "cd \"$BASE/graphchi/graphchi-cpp\" && make clean && make -j8"


build_step "Metis" \
  "cd \"$BASE/Metis\" && make clean && ./configure && make -j8"

build_step "leveldb" \
  "cd \"$BASE/leveldb\" && make clean && make -j8"

build_step "gtc-benchmark" \
  "cd \"$BASE/gtc-benchmark\" && scripts/compile_gtc.sh"

log_note "All steps attempted. Summary at: $SUMMARY"

# Uncomment if you want a failing overall exit when any workload failed
# grep -q ',FAIL,' "$SUMMARY" && exit 1 || exit 0

