#!/usr/bin/env bash
# minimal_redis_bench.sh — simple, configurable, few lines

# --- paths (change via env or keep defaults) ---
CODEBASE="${CODEBASE:-$APPBENCH}"
REDIS_DIR="${REDIS_DIR:-$CODEBASE/redis-3.0.0/src}"
REDIS_SERVER="${REDIS_SERVER:-$REDIS_DIR/redis-server}"
REDIS_BENCH="${REDIS_BENCH:-$REDIS_DIR/redis-benchmark}"
REDIS_CLI="${REDIS_CLI:-$REDIS_DIR/redis-cli}"
REDIS_CONF="${REDIS_CONF:-$CODEBASE/redis-3.0.0/redis.conf}"

# --- run params (env overrides) ---
HOST="${HOST:-127.0.0.1}"
PORT="${PORT:-6500}"
KEYSPACE="${KEYSPACE:-2000000}"    # -r
REQUESTS="${REQUESTS:-4000000}"   # -n
CLIENTS="${CLIENTS:-100}"          # -c
PIPELINE="${PIPELINE:-64}"        # -P
DATASIZE="${DATASIZE:-2048}"      # -d (bytes)
TESTS="${TESTS:-get,set}"         # -t
OUTPUT="${OUTPUT:-${OUTPUTDIR:-$PWD}/redis_bench_$(date +%Y%m%d-%H%M%S).log}"
APPPREFIX="${APPPREFIX:-}"        # e.g., $QUARTZSCRIPTS/runenv.sh
FLUSH="${FLUSH:-0}"               # set FLUSH=1 to drop caches (needs sudo)

mkdir -p "$(dirname "$OUTPUT")"
SERVER_LOG="${OUTPUT%.log}.server.log"

flush() {
  [ "$FLUSH" = "1" ] && sudo sh -c 'sync; echo 3 > /proc/sys/vm/drop_caches' || true
}

echo "==> starting redis-server on $HOST:$PORT"
flush
# start server in foreground, background the process; capture PID
${APPPREFIX:+$APPPREFIX }"$REDIS_SERVER" ${REDIS_CONF:+$REDIS_CONF} \
  --bind "$HOST" --port "$PORT" --daemonize no >"$SERVER_LOG" 2>&1 &
SRV_PID=$!

# wait until it answers PING (max ~10s)
for i in {1..50}; do
  "$REDIS_CLI" -h "$HOST" -p "$PORT" PING >/dev/null 2>&1 && break
  sleep 0.2
done

echo "==> running redis-benchmark (logging to $OUTPUT)"
/usr/bin/time -f 'TOTAL WALL CLOCK TIME(SEC): %e' \
  ${APPPREFIX:+$APPPREFIX }"$REDIS_BENCH" \
   -t "$TESTS" -n "$REQUESTS" -r "$KEYSPACE" -c "$CLIENTS" -P "$PIPELINE" -d "$DATASIZE" \
   -q -h "$HOST" -p "$PORT" >>"$OUTPUT" 2>&1

echo "==> stopping redis-server (pid $SRV_PID)"
kill "$SRV_PID" >/dev/null 2>&1 || true
wait "$SRV_PID" 2>/dev/null || true

echo "Done. Output: $OUTPUT"
