#!/usr/bin/env bash
# =============================================================================
# flash-verify.sh — flash the Nano + run the on-device end-to-end tests
# =============================================================================
# Local (dev-PC) hardware verification. CI in the cloud has no device attached,
# so the board is flashed and exercised HERE, on the machine the Nano is wired
# to. This is the project's layer-3 "hardware e2e" check (see CONTRIBUTING.md).
#
# What it does, in order, holding the shared board lock the whole time:
#   1. claim the board (scripts/board-lock.sh) so no other agent/terminal
#      flashes or monitors it underneath us
#   2. build the firmware for the Nano RP2040 Connect
#   3. upload (flash) the firmware via picotool
#   4. launch the on-device e2e harness (tests/e2e/run_e2e.py) which opens the
#      serial port, prints every line live (the serial terminal monitor), and
#      runs one test per firmware feature
#   5. release the board
#
# The harness reads serial and asserts each feature's debug output appears
# (e.g. boot sentinel, pedometer totals). Each new feature ships its own
# test_<feature>.py under tests/e2e/ — see tests/e2e/README.md.
#
# Usage:
#   ./scripts/flash-verify.sh [--agent <id>] [--port /dev/ttyACM0] [--baud 115200]
#                             [--e2e-timeout 30] [--no-flash] [--no-build]
#
#   --no-flash  skip the build+upload and just (re)run the e2e harness against
#               the firmware already on the board. Useful after a manual reset,
#               or to re-run tests without re-flashing. (A fresh boot sentinel
#               only appears after a reset, so reset the board first.)
#   --no-build  skip the build step (reuse the last .pio build). Still flashes.
#
# Exit code is non-zero if the build, upload, or any e2e test fails; the board
# is always released in that case.
# =============================================================================
set -euo pipefail

# Resolve repo root (parent of scripts/) so paths work from anywhere.
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$ROOT"

AGENT="${USER:-local}-flash-verify"
PORT="/dev/ttyACM0"
BAUD="115200"
E2E_TIMEOUT="15"
DO_BUILD=1
DO_FLASH=1

while [[ $# -gt 0 ]]; do
    case "$1" in
        --agent)        AGENT="${2:-}"; shift 2 || true ;;
        --port)         PORT="${2:-}"; shift 2 || true ;;
        --baud)         BAUD="${2:-}"; shift 2 || true ;;
        --e2e-timeout)  E2E_TIMEOUT="${2:-}"; shift 2 || true ;;
        --no-flash)     DO_FLASH=0; shift ;;
        --no-build)     DO_BUILD=0; shift ;;
        -h|--help|help) sed -n '2,40p' "$0"; exit 0 ;;
        *) echo "ERROR: unknown option: $1" >&2; exit 1 ;;
    esac
done

PURPOSE="flash-verify (build=$DO_BUILD flash=$DO_FLASH e2e)"

cleanup() {
    ./scripts/board-lock.sh release "$AGENT" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "==> Claiming board ($AGENT)…"
# If another agent holds the board, add --wait to block until it is free
# instead of failing immediately.
./scripts/board-lock.sh claim "$AGENT" --purpose "$PURPOSE"

if (( DO_BUILD )); then
    echo "==> Building firmware (pio run -e nanorp2040connect)…"
    pio run -e nanorp2040connect
fi

if (( DO_FLASH )); then
    echo "==> Flashing firmware to $PORT (pio run -e nanorp2040connect -t upload)…"
    pio run -e nanorp2040connect -t upload
fi

echo "==> Running on-device e2e harness (tests/e2e)…"
python3 tests/e2e/run_e2e.py \
    --port "$PORT" \
    --baud "$BAUD" \
    --timeout "$E2E_TIMEOUT"

echo "==> flash-verify: all e2e tests passed."
# cleanup trap releases the board.
