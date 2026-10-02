#!/usr/bin/env bash
set -euo pipefail

#MISE description="Remove built binaries (keeps the cloned sources and their cargo build cache)"

#USAGE flag "--all" help="Also remove the cloned sources together with their cargo build cache"

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"
BIN="${BIN_DIR:-$ROOT/.build/bin}"

echo "removing $BIN"
rm -rf "$BIN"

if [ "${usage_all:-false}" = "true" ]; then
	echo "removing $SRC"
	rm -rf "$SRC"
fi
