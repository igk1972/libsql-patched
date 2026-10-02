#!/usr/bin/env bash
set -euo pipefail

#MISE description="Run the sqlean SQL checks in tests/sqlean/ against the built sqld"
#MISE depends=["build:sqld"]

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
BIN="${BIN_DIR:-$ROOT/.build/bin}"
PORT=18090

tmp="$(mktemp -d)"
"$BIN/sqld" --db-path "$tmp/data.sqld" --http-listen-addr "127.0.0.1:$PORT" --no-welcome >"$tmp/sqld.log" 2>&1 &
pid=$!
trap 'kill "$pid" 2>/dev/null || true; wait "$pid" 2>/dev/null || true; rm -rf "$tmp"' EXIT

for _ in $(seq 1 100); do
	curl -sf "http://127.0.0.1:$PORT/health" >/dev/null && break
	sleep 0.1
done

rc=0
python3 "$ROOT/tests/sqlean/run.py" "http://127.0.0.1:$PORT" \
	"$ROOT/tests/sqlean/text.sql" "$ROOT/tests/sqlean/text-extra.sql" \
	"$ROOT/tests/sqlean/crypto-base85.sql" "$ROOT/tests/sqlean/fuzz-regressions.sql" || rc=1

if ! kill -0 "$pid" 2>/dev/null; then
	echo "sqld is not running any more; last log lines:"
	tail -5 "$tmp/sqld.log"
	rc=1
fi
exit "$rc"
