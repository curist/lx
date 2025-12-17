#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$ROOT/.." && pwd)"

if [[ -n "${LX:-}" ]]; then
  LX_BIN="$LX"
elif [[ -x "$ROOT/../out/lx" ]]; then
  LX_BIN="$ROOT/../out/lx"
else
  LX_BIN="lx"
fi
PY_BIN="${PYTHON:-python3}"
LUA_BIN="${LUA:-lua}"
TIME_BIN="${TIME_BIN:-}"

# pick a time command and format
if [[ -z "$TIME_BIN" ]]; then
  if command -v gtime >/dev/null 2>&1; then
    TIME_BIN="gtime"
    TIME_FMT="-f %e"
  else
    TIME_BIN="time"
    TIME_FMT=""
  fi
else
  if [[ "$TIME_BIN" == *gtime* ]]; then
    TIME_FMT="-f %e"
  else
    TIME_FMT=""
  fi
fi

# Problem sizes (tune as needed)
N_SUM=${N_SUM:-50000000}
N_FIZZ=${N_FIZZ:-20000000}
N_FIB=${N_FIB:-5000000}
N_ARRAY=${N_ARRAY:-10000000}
N_MAP=${N_MAP:-5000000}
REPEAT=${REPEAT:-1}

have() { command -v "$1" >/dev/null 2>&1; }

run_lang() {
  local lang=$1 bin=$2
  shift 2 || true
  if ! have "$bin"; then
    echo "skipping $lang (missing $bin)"
    return
  fi
  echo "--- $lang ---"
  local cmd_prefix=("$bin")
  if [[ "$lang" == "lx" ]]; then
    cmd_prefix=("$bin" "run")
  fi

  bench() {
    local script=$1 arg=$2
    echo "== $lang :: $script $arg"
    local i=0
    while [[ $i -lt $REPEAT ]]; do
      i=$((i + 1))
      if [[ -n "$TIME_FMT" ]]; then
        (cd "$REPO_ROOT" && /usr/bin/env "$TIME_BIN" $TIME_FMT "${cmd_prefix[@]}" "$script" "$arg")
      else
        (cd "$REPO_ROOT" && /usr/bin/env "$TIME_BIN" "${cmd_prefix[@]}" "$script" "$arg")
      fi
    done
  }

  bench "$ROOT/$lang/sum_loop.$lang" "$N_SUM"
  bench "$ROOT/$lang/fizzbuzz.$lang" "$N_FIZZ"
  bench "$ROOT/$lang/fib_iter.$lang" "$N_FIB"
  bench "$ROOT/$lang/array_fill.$lang" "$N_ARRAY"
  bench "$ROOT/$lang/map_hit_miss.$lang" "$N_MAP"
}

run_lang "lx" "$LX_BIN"
run_lang "py" "$PY_BIN"
run_lang "lua" "$LUA_BIN"
