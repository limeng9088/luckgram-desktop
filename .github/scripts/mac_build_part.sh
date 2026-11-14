#!/usr/bin/env bash
# One part of the macOS app build (see the "Build (part N)" steps in
# .github/workflows/build.yml).
#
#   mac_build_part.sh <part> <minutes> <jobs>
#
# Runs `cmake --build` (xcodebuild) for at most <minutes>. When the time is
# up, xcodebuild gets SIGINT first so it can stop cleanly and release
# XCBuildData/build.db, then SIGTERM, then SIGKILL for its whole process
# group. The step timeout of Actions does not stop xcodebuild itself: in run
# 43567868936 it went on in the background after part 1 and every later
# part failed at once with "build.db: database is locked".
#
# Exit code: 0 when the build finished, otherwise non-zero (the next part
# goes on from where this one stopped).

set -u
PART=${1:?part}
MINUTES=${2:?minutes}
JOBS=${3:-2}
OUT_DIR=${OUT_DIR:-luckgram-desktop/out}
KILL_NAMES=(xcodebuild XCBBuildService)

log() { echo "[part $PART $(date -u +%H:%M:%S)] $*"; }

running() {
  local n
  for n in "${KILL_NAMES[@]}"; do
    pgrep -x "$n" >/dev/null 2>&1 && return 0
  done
  return 1
}

# Stops xcodebuild processes left over by an earlier part (or anything else).
stop_leftovers() {
  running || return 0
  log "leftover build processes:"
  ps -axo pid,pgid,etime,command | grep -E 'xcodebuild|XCBBuildService' | grep -v grep || true
  local sig n i
  for sig in INT TERM KILL; do
    for n in "${KILL_NAMES[@]}"; do pkill "-$sig" -x "$n" 2>/dev/null || true; done
    for i in $(seq 1 30); do running || break; sleep 2; done
    running || { log "leftovers stopped (SIG$sig)"; return 0; }
  done
  log "::warning::build processes still running after SIGKILL"
}

# Waits until no process has XCBuildData/build.db open.
wait_build_db() {
  local dbs i
  dbs=$(find "$OUT_DIR" -path '*XCBuildData*' -name 'build.db*' 2>/dev/null)
  [ -n "$dbs" ] || return 0
  for i in $(seq 1 60); do
    # shellcheck disable=SC2086
    if ! lsof $dbs >/dev/null 2>&1; then
      [ "$i" -gt 1 ] && log "build.db is free"
      return 0
    fi
    [ "$i" -eq 1 ] && { log "build.db is still open:"; lsof $dbs || true; }
    sleep 5
  done
  log "::warning::build.db still open after 5 minutes"
}

# vm_stat / df every 10 minutes.
( while true; do
    echo "::group::resources $(date -u +%H:%M)"; vm_stat; df -h / "${GITHUB_WORKSPACE:-.}"; echo "::endgroup::"
    sleep 600
  done ) &
MON=$!

BUILD_PID=
group_alive() { pgrep -g "$BUILD_PID" >/dev/null 2>&1 || kill -0 "$BUILD_PID" 2>/dev/null; }

stop_build() {
  # The whole process group of cmake --build (set -m gives it its own group),
  # so xcodebuild, XCBBuildService and the compilers stop with it.
  local sig i
  for sig in INT TERM KILL; do
    group_alive || break
    log "sending SIG$sig to the build (process group $BUILD_PID)"
    kill "-$sig" -- "-$BUILD_PID" 2>/dev/null || true
    case $sig in INT) i=90 ;; TERM) i=30 ;; *) i=10 ;; esac
    while [ "$i" -gt 0 ] && group_alive; do sleep 2; i=$((i - 2)); done
  done
}

cleanup() {
  pkill -P "$MON" 2>/dev/null || true
  kill "$MON" 2>/dev/null || true
  [ -n "$BUILD_PID" ] && stop_build
  stop_leftovers
}
trap cleanup EXIT
trap 'log "step cancelled"; exit 130' INT TERM

# The time limit counts from here, so the clean-up below stays inside the
# step timeout as well.
START=$(date +%s)
stop_leftovers
wait_build_db

cd "$OUT_DIR" || exit 1
DEADLINE=$((START + MINUTES * 60))
log "building for at most $MINUTES min with $JOBS jobs"

set -m
cmake --build . --config Release -- -jobs "$JOBS" &
BUILD_PID=$!
set +m

TIMED_OUT=
while kill -0 "$BUILD_PID" 2>/dev/null; do
  if [ "$(date +%s)" -ge "$DEADLINE" ]; then
    TIMED_OUT=1
    log "time is up ($MINUTES min), stopping the build"
    stop_build
    break
  fi
  sleep 10
done

wait "$BUILD_PID" 2>/dev/null
CODE=$?
BUILD_PID=
log "part took $(( ($(date +%s) - START) / 60 )) min, exit code $CODE"
[ -n "${BUILD_START:-}" ] && log "build so far $(( ($(date +%s) - BUILD_START) / 60 )) min"

if [ -n "$TIMED_OUT" ]; then
  log "not finished in this part, the next part goes on"
  [ "$CODE" -ne 0 ] || CODE=124
fi
exit "$CODE"
