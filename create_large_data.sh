#!/usr/bin/env bash
set -euo pipefail

: "${SHARED_DATA:?SHARED_DATA not set}"
mkdir -p "$SHARED_DATA"
cd "$SHARED_DATA"

BASEURL="https://snap.stanford.edu/data/bigdata/communities"

fetch() {
  # fetch <url> <outfile>
  local url="$1" out="$2"
  if [[ -f "$out" ]]; then
    echo "[create_large_data] exists: $out"
    return 0
  fi
  echo "[create_large_data] downloading: $url -> $out"
  ( command -v curl >/dev/null && curl -C - -L "$url" -o "$out" ) \
  || ( command -v wget >/dev/null && wget -c "$url" -O "$out" )
}

# --- Graph datasets ---
# Orkut (uncompressed)
fetch "$BASEURL/com-orkut.ungraph.txt" "com-orkut.ungraph.txt"

# Friendster (large, gzipped)
if [[ ! -f com-friendster.ungraph.txt ]]; then
  fetch "$BASEURL/com-friendster.ungraph.txt.gz" "com-friendster.ungraph.txt.gz"
  echo "[create_large_data] decompressing com-friendster.ungraph.txt.gz"
  gunzip -f com-friendster.ungraph.txt.gz
fi

# --- Big text for key/value or streaming tests (~4GB) ---
size_bytes() {
  if command -v stat >/dev/null; then
    # GNU stat (-c) or BSD stat (-f)
    stat -c%s "$1" 2>/dev/null || stat -f%z "$1"
  else
    wc -c < "$1"
  fi
}

if [[ ! -f crime.data ]]; then
  fetch "https://norvig.com/big.txt" "crime.data"
  # Duplicate until >= 4 GiB
  echo "[create_large_data] growing crime.data to ~4 GiB (this may take a while)"
  while :; do
    sz=$(size_bytes crime.data)
    (( sz >= 4294967296 )) && break
    cat crime.data crime.data > crime.data.tmp && mv crime.data.tmp crime.data
  done
fi

echo "[create_large_data] done. Data in: $SHARED_DATA"

