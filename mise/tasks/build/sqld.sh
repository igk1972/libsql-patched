#!/usr/bin/env bash
set -euo pipefail

#MISE description="Build sqld and bottomless-cli (release) from the patched sources"
#MISE depends=["setup:sources"]

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"
BIN="${BIN_DIR:-$ROOT/.build/bin}"

cd "$SRC"

want="$(sed -n 's/^channel *= *"\(.*\)"$/\1/p' rust-toolchain.toml)"
have="$(rustc --version | awk '{print $2}')"
if [ "$have" != "$want" ]; then
	echo "ERROR: rustc is $have, but upstream rust-toolchain.toml pins $want; update [tools] rust in mise.toml" >&2
	exit 1
fi

# Two invocations, as in upstream's Dockerfile: cargo unifies features across everything built
# in one invocation, and bottomless-cli's dependency features must not leak into sqld.
cargo build --locked --release -p libsql-server
cargo build --locked --release -p bottomless-cli

mkdir -p "$BIN"
cp target/release/sqld target/release/bottomless-cli "$BIN/"
echo "built $BIN/sqld, $BIN/bottomless-cli"
