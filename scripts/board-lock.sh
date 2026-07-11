#!/usr/bin/env bash
# =============================================================================
# board-lock.sh — claim/release the shared Nano RP2040 Connect for testing
# =============================================================================
# The Nano is the ONLY physical board and is plugged into this PC, so firmware
# can be flashed and exercised here. But it is a single shared device: two
# agents (or two terminals) flashing / monitoring it at the same time collide
# — one upload clobbers the other and serial output interleaves.
#
# This script serialises access with a lock file. Before you `pio run -t upload`
# or `pio device monitor`, CLAIM the board; RELEASE it when you are done. The
# lock records who holds it, since when, and why, so collisions are avoided and
# stale locks (an agent that crashed) are auto-released after a timeout.
#
# Usage:
#   ./scripts/board-lock.sh claim  <agent_id> [--purpose "..."] [--wait]
#   ./scripts/board-lock.sh release <agent_id> [--force]
#   ./scripts/board-lock.sh status
#   ./scripts/board-lock.sh check  <agent_id>     # 0 = free/you, 1 = someone else
#
# <agent_id> must be unique to you: use your task id, session id, or branch
# name. Pass the SAME id to claim and release.
# =============================================================================
set -euo pipefail

BOARD_DIR=".board"
LOCK_FILE="$BOARD_DIR/lock"          # flock target (serialises claimants)
STATE_FILE="$BOARD_DIR/lock.state"   # human-readable lock record
STALE_SECS=1800                      # 30 min — a lock older than this is reclaimable
POLL_SECS=10                         # sleep between --wait retries

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------
now_secs() { date +%s; }

iso_of() { date -d "@$1" '+%Y-%m-%d %H:%M:%S' 2>/dev/null || echo "$1"; }

log() { printf '%s\n' "$*"; }

die() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

ensure_dir() { mkdir -p "$BOARD_DIR"; }

write_state() {
    # $1=agent $2=pid $3=purpose
    ensure_dir
    {
        printf "AGENT='%s'\n" "$1"
        printf "SINCE='%s'\n" "$(now_secs)"
        printf "PID='%s'\n" "$2"
        printf "PURPOSE='%s'\n" "$3"
    } > "$STATE_FILE"
}

read_state() {
    # sources STATE_FILE into AGENT/SINCE/PID/PURPOSE if it exists
    AGENT=""; SINCE=""; PID=""; PURPOSE=""
    [[ -f "$STATE_FILE" ]] || return 1
    # shellcheck disable=SC1090
    source "$STATE_FILE"
    return 0
}

clear_state() { rm -f "$STATE_FILE"; }

# Soft check: warn (do not block) if no board appears on a serial port.
check_board_connected() {
    if ! command -v pio >/dev/null 2>&1; then
        log "note: 'pio' not found — skipping board-presence check."
        return 0
    fi
    if pio device list 2>/dev/null | grep -Eq '/dev/|COM[0-9]|ttyACM|ttyUSB|cu\.';
    then
        return 0
    fi
    log "warning: no board detected on a serial port — is the Nano plugged in?"
    log "warning: continuing the claim anyway; flashing will fail if it is not."
    return 0
}

# ---------------------------------------------------------------------------
# commands
# ---------------------------------------------------------------------------
cmd_claim() {
    local agent="${1:-}"; shift || true
    local purpose="unspecified"; local wait_mode=0
    [[ -n "$agent" ]] || die "claim requires <agent_id>"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --purpose) purpose="${2:-}"; shift 2 || true ;;
            --wait)    wait_mode=1; shift ;;
            *) die "unknown claim option: $1" ;;
        esac
    done

    ensure_dir
    exec 9>"$LOCK_FILE"            # open lock fd (created if missing)
    flock -x 9                     # serialise concurrent claimants

    while :; do
        if ! read_state; then
            check_board_connected
            write_state "$agent" "$$" "$purpose"
            log "Board claimed by '$agent' (purpose: $purpose)."
            log "Remember to release it: ./scripts/board-lock.sh release $agent"
            exit 0
        fi

        if [[ "$AGENT" == "$agent" ]]; then
            # Re-assert ownership (refresh timestamp so it does not go stale).
            write_state "$agent" "$$" "${PURPOSE:-$purpose}"
            log "Board already claimed by you ('$agent'); timestamp refreshed."
            exit 0
        fi

        local age=$(( $(now_secs) - ${SINCE:-0} ))
        if (( age > STALE_SECS )); then
            log "Stale lock from '$AGENT' (since $(iso_of "${SINCE:-0}")) " \
                "expired — taking over."
            check_board_connected
            write_state "$agent" "$$" "$purpose"
            log "Board claimed by '$agent' (purpose: $purpose)."
            exit 0
        fi

        if (( wait_mode )); then
            log "Board held by '$AGENT' (since $(iso_of "${SINCE:-0}"), " \
                "purpose: ${PURPOSE:-?}). Waiting ${POLL_SECS}s…"
            sleep "$POLL_SECS"
            continue
        fi

        die "Board is claimed by '$AGENT' since $(iso_of "${SINCE:-0}") " \
            "(purpose: ${PURPOSE:-?}). Wait with --wait, or release it first."
    done
}

cmd_release() {
    local agent="${1:-}"; shift || true
    local force=0
    [[ -n "$agent" ]] || die "release requires <agent_id>"
    while [[ $# -gt 0 ]]; do
        case "$1" in
            --force) force=1; shift ;;
            *) die "unknown release option: $1" ;;
        esac
    done

    ensure_dir
    exec 9>"$LOCK_FILE"
    flock -x 9

    if ! read_state; then
        log "No board lock is currently held."
        exit 0
    fi
    if [[ "$AGENT" != "$agent" && "$force" -ne 1 ]]; then
        die "Lock is held by '$AGENT', not '$agent'. Use --force to override."
    fi
    clear_state
    if (( force )); then
        log "Board released (forced)."
    else
        log "Board released."
    fi
    exit 0
}

cmd_status() {
    if ! read_state; then
        log "Board: FREE"
        exit 0
    fi
    local age=$(( $(now_secs) - ${SINCE:-0} ))
    local stale=""; (( age > STALE_SECS )) && stale=" [STALE — reclaimable]"
    log "Board: CLAIMED"
    log "  by     : $AGENT"
    log "  since  : $(iso_of "${SINCE:-0}") (${age}s ago)${stale}"
    log "  pid    : ${PID:-?}"
    log "  purpose: ${PURPOSE:-?}"
    exit 0
}

cmd_check() {
    local agent="${1:-}"; shift || true
    [[ -n "$agent" ]] || die "check requires <agent_id>"
    if ! read_state; then
        exit 0   # free
    fi
    if [[ "$AGENT" == "$agent" ]]; then
        exit 0   # yours
    fi
    exit 1       # someone else's
}

# ---------------------------------------------------------------------------
# dispatch
# ---------------------------------------------------------------------------
cmd="${1:-}"; [[ $# -gt 0 ]] && shift
case "$cmd" in
    claim)   cmd_claim "$@" ;;
    release) cmd_release "$@" ;;
    status)  cmd_status "$@" ;;
    check)   cmd_check "$@" ;;
    ""|-h|--help|help) sed -n '2,22p' "$0" ;;
    *) die "unknown command: $cmd (try: claim | release | status | check)" ;;
esac
