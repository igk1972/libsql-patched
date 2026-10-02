#!/usr/bin/env bash
set -euo pipefail

#MISE description="Rewrite patches/ from the commits on top of LIBSQL_REF in .build/libsql"

ROOT="${MISE_PROJECT_ROOT:-$PWD}"
SRC="${SRC_DIR:-$ROOT/.build/libsql}"
PATCH_DIR="$ROOT/patches"
APPLIED="$SRC/.git/libsql-patched-applied"

if ! git -C "$SRC" merge-base --is-ancestor "$LIBSQL_REF" HEAD 2>/dev/null; then
	echo "ERROR: $SRC is not based on LIBSQL_REF ($LIBSQL_REF)" >&2
	exit 1
fi
if [ -n "$(git -C "$SRC" status --porcelain)" ]; then
	echo "ERROR: $SRC has uncommitted changes; commit them first" >&2
	exit 1
fi
if [ "$(git -C "$SRC" rev-list --count "$LIBSQL_REF..HEAD")" -eq 0 ]; then
	echo "ERROR: no commits on top of LIBSQL_REF; refusing to empty patches/" >&2
	exit 1
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

# Without the commit id, the git version, "[PATCH n/m]" numbering and abbreviated blob ids (their
# length depends on the number of objects in the clone), an unchanged commit always exports to a
# byte-identical file.
git -C "$SRC" format-patch --quiet --zero-commit --no-signature --no-numbered --full-index \
	-o "$tmp" "$LIBSQL_REF..HEAD"

mkdir -p "$PATCH_DIR"
rm -f "$PATCH_DIR"/*.patch
mv "$tmp"/*.patch "$PATCH_DIR"/

: >"$APPLIED"
for p in "$PATCH_DIR"/*.patch; do
	echo "$(git hash-object "$p")  $(basename "$p")" >>"$APPLIED"
	echo "exported $(basename "$p")"
done
