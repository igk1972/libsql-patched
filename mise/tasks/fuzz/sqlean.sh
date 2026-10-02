#!/usr/bin/env bash
set -euo pipefail

#MISE description="Fuzz the sqlean extensions bundled into sqld under AddressSanitizer + UBSan"
#MISE depends=["setup:sources"]

#USAGE arg "[seconds]" default="120" help="Time budget of the random pass, after the edge-value pass"
#USAGE flag "--seed <seed>" help="Seed of the random pass (default: current time)"

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"
BUNDLED="$SRC/libsql-ffi/bundled"
SQLEAN="$BUNDLED/sqlean"
OUT="$ROOT/.build/fuzz"
BIN="$OUT/sqlean-fuzz"
mkdir -p "$OUT"

# The sources and flags libsql-ffi's build.rs compiles for sqld (feature sqlean-extensions),
# except the pcre2_internal.h header it also passes to the compiler.
sources=(
	"$SQLEAN"/crypto/*.c "$SQLEAN"/fuzzy/*.c "$SQLEAN"/math/*.c "$SQLEAN"/stats/*.c
	"$SQLEAN"/text/*.c "$SQLEAN"/text/*/*.c "$SQLEAN"/uuid/*.c
	"$SQLEAN"/regexp/*.c "$SQLEAN"/regexp/pcre2/*.c
)
cflags=(
	-std=c11 -DSQLITE_CORE -DSQLITE_DEFAULT_FOREIGN_KEYS=1 -DSQLITE_ENABLE_API_ARMOR
	-DSQLITE_ENABLE_COLUMN_METADATA -DSQLITE_ENABLE_DBSTAT_VTAB -DSQLITE_ENABLE_FTS3
	-DSQLITE_ENABLE_FTS3_PARENTHESIS -DSQLITE_ENABLE_FTS5 -DSQLITE_ENABLE_JSON1
	-DSQLITE_ENABLE_LOAD_EXTENSION=1 -DSQLITE_ENABLE_MEMORY_MANAGEMENT -DSQLITE_ENABLE_RTREE
	-DSQLITE_ENABLE_STAT2 -DSQLITE_ENABLE_STAT4 -DSQLITE_SOUNDEX -DSQLITE_THREADSAFE=1
	-DSQLITE_USE_URI -DHAVE_USLEEP=1 -D_POSIX_THREAD_SAFE_FUNCTIONS
	-include "$SQLEAN/regexp/constants.h" -I"$SQLEAN" -I"$BUNDLED/src"
)
# -fno-sanitize=function: SQLite calls extension entry points through a generic function
# pointer type by design.
sanitize=(
	-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined
	-fno-sanitize-recover=undefined -fno-sanitize=function
)

if [ ! -x "$BIN" ] || [ -n "$(find "$SQLEAN" "$BUNDLED/src/sqlite3.c" "$ROOT/tests/sqlean/fuzz.c" -newer "$BIN" -print -quit)" ]; then
	echo "building $BIN"
	cc "${cflags[@]}" "${sanitize[@]}" -w -o "$BIN" "$ROOT/tests/sqlean/fuzz.c" "${sources[@]}"
fi

cd "$OUT"
export ASAN_OPTIONS="abort_on_error=1:detect_leaks=0:allocator_may_return_null=1:max_allocation_size_mb=512"
export UBSAN_OPTIONS="print_stacktrace=1"
export SQLEAN_FUZZ_REPRO="$OUT/sqlean-fuzz-repro.sql"
"$BIN" "${usage_seconds:-120}" ${usage_seed:+"$usage_seed"}
