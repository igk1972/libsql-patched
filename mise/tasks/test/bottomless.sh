#!/usr/bin/env bash
set -euo pipefail

#MISE description="Run the bottomless test suites (bottomless crate + libsql-server bottomless tests)"
#MISE depends=["setup:sources"]

#USAGE flag "--include-ignored" help="Also run tests upstream marks #[ignore] (backup_restore, ~1 min)"

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"

cd "$SRC"
cargo test --locked -p bottomless
cargo test --locked -p libsql-server bottomless

# A separate process: the restore tests share one mock S3 on :9000, spawned on the runtime of
# whichever test starts first, so it dies when that test ends and the other one hangs.
if [ "${usage_include_ignored:-false}" = "true" ]; then
	cargo test --locked -p libsql-server bottomless -- --ignored
fi
