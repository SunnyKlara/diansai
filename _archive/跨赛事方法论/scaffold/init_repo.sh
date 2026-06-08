#!/usr/bin/env bash
set -euo pipefail

YEAR=""
PROBLEM=""
MODE="hardware"
ROOT=""

while [[ $# -gt 0 ]]; do
  case "$1" in
    --year)    YEAR="$2";    shift 2;;
    --problem) PROBLEM="$2"; shift 2;;
    --mode)    MODE="$2";    shift 2;;
    --root)    ROOT="$2";    shift 2;;
    *) shift;;
  esac
done

[[ -z "$YEAR" ]]    && { echo "[ERROR] --year missing"; exit 1; }
[[ -z "$PROBLEM" ]] && { echo "[ERROR] --problem missing"; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PY="${PYTHON:-python3}"

ARGS=( --year "$YEAR" --problem "$PROBLEM" --mode "$MODE" --templates "$SCRIPT_DIR/_templates" )
[[ -n "$ROOT" ]] && ARGS+=( --root "$ROOT" )

"$PY" "$SCRIPT_DIR/_render.py" "${ARGS[@]}"
