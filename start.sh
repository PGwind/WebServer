#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${1:-9999}"
ROOT_DIR="${2:-$SCRIPT_DIR}"

make -C "$SCRIPT_DIR/build" clean
make -C "$SCRIPT_DIR/build"

echo
echo "Tiny Web is starting..."
echo "Port: $PORT"
echo "Root directory: $ROOT_DIR"
echo

"$SCRIPT_DIR/server" "$PORT" "$ROOT_DIR"
