#!/usr/bin/env bash
set -euo pipefail

#MISE description="Clone libsql at the pinned commit into .build/libsql and apply local patches"

#USAGE flag "--reset" help="Reset .build/libsql to LIBSQL_REF and re-apply all patches, discarding local changes"

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"
PATCH_DIR="$ROOT/patches"
# One "<blob sha>  <file>" line per applied patch, in order. Kept inside .git so it never shows
# up as a work-tree change and goes away together with the clone.
APPLIED="$SRC/.git/libsql-patched-applied"
FFI_BUILD_RS="libsql-ffi/build.rs"
SQLEAN_STAMP="$SRC/.git/libsql-patched-sqlean-stamp"

# Each patch becomes a commit in the clone. A fixed identity keeps those commits identical
# across machines; signing and hooks from the user's global git config must not run here.
git_src() {
	git -C "$SRC" \
		-c user.name=libsql-patched -c user.email=libsql-patched@localhost \
		-c commit.gpgsign=false -c core.hooksPath=/dev/null "$@"
}

fetch_ref() {
	git_src cat-file -e "$LIBSQL_REF^{commit}" 2>/dev/null ||
		git_src fetch --depth 1 origin "$LIBSQL_REF"
}

if [ ! -d "$SRC/.git" ]; then
	echo "cloning libsql @ $LIBSQL_REF"
	mkdir -p "$SRC"
	git_src init -q
	git_src remote add origin "$LIBSQL_REPO"
	fetch_ref
	git_src checkout -q --detach "$LIBSQL_REF"
elif [ "${usage_reset:-false}" = "true" ]; then
	echo "resetting $SRC to $LIBSQL_REF"
	git_src am --abort 2>/dev/null || true
	git_src update-index --no-skip-worktree "$FFI_BUILD_RS" 2>/dev/null || true
	fetch_ref
	git_src checkout -q -f --detach "$LIBSQL_REF"
	# Without -x: ignored files (cargo's target/) survive, so rebuilds stay incremental.
	git_src clean -q -fd
	rm -f "$APPLIED"
fi

if ! git_src merge-base --is-ancestor "$LIBSQL_REF" HEAD 2>/dev/null; then
	echo "ERROR: $SRC is not based on LIBSQL_REF ($LIBSQL_REF); re-run with --reset" >&2
	exit 1
fi

shopt -s nullglob
patches=("$PATCH_DIR"/*.patch)
shopt -u nullglob

applied_count=0
if [ -f "$APPLIED" ]; then
	applied_count="$(wc -l <"$APPLIED" | tr -d ' ')"
fi
if [ "$applied_count" -gt "${#patches[@]}" ]; then
	echo "ERROR: $applied_count patches are applied in $SRC, but patches/ has ${#patches[@]}; re-run with --reset" >&2
	exit 1
fi

# New patches go on top of the applied ones, so the clone must hold exactly those commits.
ahead="$(git_src rev-list --count "$LIBSQL_REF..HEAD")"
if [ "${#patches[@]}" -gt "$applied_count" ] && [ "$ahead" -ne "$applied_count" ]; then
	echo "ERROR: $SRC has $ahead commits on top of LIBSQL_REF, but $applied_count applied patches;" >&2
	echo "       export them with patch:export or re-run with --reset" >&2
	exit 1
fi

i=0
for p in ${patches[@]+"${patches[@]}"}; do
	i=$((i + 1))
	name="$(basename "$p")"
	entry="$(git hash-object "$p")  $name"
	if [ "$i" -le "$applied_count" ]; then
		if [ "$(sed -n "${i}p" "$APPLIED")" != "$entry" ]; then
			echo "ERROR: $name differs from the patch applied at position $i; re-run with --reset" >&2
			exit 1
		fi
		echo "already applied $name"
		continue
	fi
	if git_src am --quiet --committer-date-is-author-date "$p"; then
		echo "$entry" >>"$APPLIED"
		echo "applied $name"
	else
		git_src am --abort 2>/dev/null || true
		echo "FAILED to apply $name" >&2
		exit 1
	fi
done

if [ "${#patches[@]}" -eq 0 ]; then
	echo "no patches"
fi

# Local macOS build fix, deliberately not a patch (the image is built on Linux, where it is not
# needed): upstream's build.rs passes pcre2_internal.h to the C compiler, clang turns it into a
# precompiled header, and the resulting liblibsql.a lacks SQLite itself, so linking sqld fails.
# skip-worktree keeps the edit out of patch commits and out of patch:export's clean-tree check.
if [ "$(uname -s)" = "Darwin" ]; then
	pcre2_header='sqlean_patterns.push("regexp/pcre2/pcre2_internal.h");'
	if grep -qF "$pcre2_header" "$SRC/$FFI_BUILD_RS"; then
		grep -vF "$pcre2_header" "$SRC/$FFI_BUILD_RS" >"$SRC/$FFI_BUILD_RS.tmp"
		mv "$SRC/$FFI_BUILD_RS.tmp" "$SRC/$FFI_BUILD_RS"
		git_src update-index --skip-worktree "$FFI_BUILD_RS"
		echo "macOS: dropped pcre2_internal.h from $FFI_BUILD_RS (local build fix, not a patch)"
	fi
fi

# libsql-ffi's build.rs asks cargo to rerun it only when bundled/src/sqlite3.c changes, so an
# incremental build keeps stale objects after the bundled sqlean sources change. Touching the
# file (mtime only) makes the next cargo build recompile libsql-ffi.
if [ ! -f "$SQLEAN_STAMP" ] ||
	[ -n "$(find "$SRC/libsql-ffi/bundled/sqlean" -newer "$SQLEAN_STAMP" -print -quit)" ]; then
	touch "$SRC/libsql-ffi/bundled/src/sqlite3.c" "$SQLEAN_STAMP"
	echo "bundled sqlean sources changed: libsql-ffi will be recompiled"
fi
