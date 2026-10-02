# Patches

## How patches are applied

`patches/*.patch` are `git format-patch` files, applied in name order. `setup:sources` clones
upstream at `LIBSQL_REF` into `.build/libsql` (a shallow fetch of that one commit) and applies
each patch with `git am`, so every patch becomes a commit on top of `LIBSQL_REF`. The commits
keep the patch's author and use a fixed committer identity with the author date as the commit
date, so they are identical on every machine.

Which patches are already applied is recorded in `.build/libsql/.git/libsql-patched-applied`
(the git blob id and file name of each patch, in order). Re-running `setup:sources`:

- skips patches that are already applied unchanged;
- applies patches added after the last one applied;
- stops with an error when an applied patch was changed or removed, or when `LIBSQL_REF` no
  longer matches the clone — `setup:sources --reset` then resets the clone to `LIBSQL_REF`
  (discarding local changes, keeping cargo's `target/`) and applies everything again.

## Local build on macOS

At `LIBSQL_REF`, sqld does not link on macOS: `libsql-ffi/build.rs` passes the header
`regexp/pcre2/pcre2_internal.h` to the C compiler, clang emits a precompiled header for it,
and the resulting `liblibsql.a` lacks SQLite itself. Linux, where the image is built, is not
affected, so this is not a patch. On macOS only, `setup:sources` deletes that line from
`.build/libsql/libsql-ffi/build.rs` and marks the file `skip-worktree`: the edit does not show
in `git status`, cannot end up in a patch commit, and stays in place across runs. `--reset`
restores the file, and the edit is made again. A patch that changes `libsql-ffi/build.rs`
itself would conflict with it.

## Incremental builds and the bundled sqlean sources

`libsql-ffi/build.rs` tells cargo to rerun it only when `bundled/src/sqlite3.c` changes, so an
incremental build keeps the old objects when only `bundled/sqlean/` changed (patches 0003 to
0010) — sqld then still runs the old sqlean code. `setup:sources` therefore touches
`bundled/src/sqlite3.c` (mtime only, `git status` stays clean) whenever a file under
`bundled/sqlean/` is newer than `.build/libsql/.git/libsql-patched-sqlean-stamp`. Fresh clones,
as in CI, are not affected.

## Editing patches

The commits in `.build/libsql` are the working copy of the patch set; `patches/` is its
exported form.

- **New patch:** make the change in `.build/libsql`, commit it there, run `patch:export`.
- **Change an existing patch:** commit a fixup in `.build/libsql` and squash it into the
  patch's commit (`git rebase --autosquash`), then run `patch:export`.

`patch:export` regenerates all of `patches/` from `LIBSQL_REF..HEAD` (numbered from `0001`,
named after the commit subjects) and updates the applied-patches record. It refuses to run
with uncommitted changes in `.build/libsql`. The files carry no commit ids, no git version and
only full blob ids, so an unchanged commit exports to a byte-identical file from any clone.

## Patch list

### 0001 — bottomless: fix snapshot upload pipeline silently dropping generation snapshots

Upstream PR [tursodatabase/libsql#2260](https://github.com/tursodatabase/libsql/pull/2260)
(open), commit `9ecbef9a577a26a823cf0eb65a33cb6fcd2cc605`, taken unchanged. The files it
touches are identical at `LIBSQL_REF` and at the PR's base, so it applies cleanly.

- `bottomless/src/replicator.rs`
  - the snapshot `PutObject` is retried (`SNAPSHOT_UPLOAD_ATTEMPTS = 3`, backoff from
    500 ms), rebuilding the body stream on every attempt;
  - an in-flight flag (`snapshot_in_flight`, cleared by a drop guard) allows one snapshot task
    at a time;
  - compression writes a per-generation artifact `db.<generation>.{gz,zstd}` instead of a
    shared `db.{gz,zstd}`; stale artifacts are removed only while no upload runs;
  - stalled-stream protection is disabled on the bottomless S3 client;
  - `compare_with_local` stores the compared generation before its early return, so a new
    generation after a restart gets its `.dep` link;
  - `skip_snapshot_for_current_generation` is removed;
  - log lines name the database and generation: the success line is
    `Snapshot upload of db <db> for generation <uuid> finished (took …)`.
- `bottomless/src/bottomless_wal.rs` — `ensure_snapshotted`: when the current generation has
  no snapshot and none is in flight, a checkpoint re-triggers it (and is still refused with
  `SQLITE_BUSY`) instead of failing until a restart; the empty-WAL branch re-triggers a missing
  snapshot instead of marking the generation as snapshotted.
- `libsql-server/src/test/bottomless.rs` — `snapshot_pipeline` regression tests against a
  mock S3 server with failure injection.
- `.gitignore` — the tests' data directories.

S3 object keys (`<db>-<generation>/db.<compression>`, `.changecounter`, `.dep`, `.meta`) are
unchanged, so existing buckets restore as before.

Not covered by this patch: bottomless sets no request or operation timeout on its S3 client,
and with stalled-stream protection off nothing in bottomless bounds how long an upload may hang
on a stuck connection — see 0002.

### 0002 — bottomless: configurable S3 timeouts, stalled-stream protection and snapshot upload attempts

Bottomless builds its S3 client from `SdkConfig::builder()`, not `aws_config::from_env()`, so the
client has no timeouts at all (not even the 3.1 s connect timeout `aws_config` loads by
default). This patch makes them configurable. Everything is **off unless set**; unset, the
behaviour is exactly that of 0001.

| Variable | Unset | Set |
|---|---|---|
| `LIBSQL_BOTTOMLESS_S3_CONNECT_TIMEOUT_SECS` | no timeout | timeout for establishing a connection |
| `LIBSQL_BOTTOMLESS_S3_ATTEMPT_TIMEOUT_SECS` | no timeout | limit for one attempt of any S3 request: sending it with its body and reading the response; a streamed response body (a snapshot download during restore) is not covered |
| `LIBSQL_BOTTOMLESS_S3_STALLED_STREAM_GRACE_SECS` | protection disabled | SDK stalled-stream protection with this grace period |
| `LIBSQL_BOTTOMLESS_SNAPSHOT_UPLOAD_ATTEMPTS` | 3 | attempts per snapshot upload |

Values must be positive integers; anything else makes sqld fail at startup. Each replicator
logs the values it uses:
`bottomless S3 client: connect timeout …, attempt timeout …, max attempts …, stalled-stream protection grace period …`.

- `bottomless/src/replicator.rs`
  - `Options` gets `s3_connect_timeout`, `s3_attempt_timeout`, `s3_stalled_stream_grace_period`
    and `snapshot_upload_attempts`, read in `from_env`; `SNAPSHOT_UPLOAD_ATTEMPTS` becomes the
    public default;
  - `client_config` sets the SDK `TimeoutConfig` and enables stalled-stream protection only
    when a grace period is set;
  - the snapshot upload uses `snapshot_upload_attempts`; the backoff between attempts is
    capped at 60 s.
- `libsql-server/src/namespace/meta_store.rs` — the meta store backup (`--backup-meta-store`)
  builds its options from CLI flags, not the environment: everything off, 3 attempts.
- `libsql-server/src/test/bottomless.rs` — `hung_snapshot_upload_times_out_and_is_retried`:
  the mock S3 holds the first snapshot PUT without answering; with a 1 s attempt timeout the
  upload is retried and the snapshot lands. The failure injector counts snapshot PUTs.
- `.gitignore` — the new test's data directory.

Behaviour worth knowing when choosing values:

- Stalled-stream protection stops watching once the request body has been sent in full, so a
  request stuck waiting for its response is bounded only by the attempt timeout.
- The SDK retries a timed-out attempt as a transient error, up to
  `LIBSQL_BOTTOMLESS_S3_MAX_RETRIES` attempts (default 10), because bodies read from files can
  be replayed. A snapshot upload therefore makes up to
  `SNAPSHOT_UPLOAD_ATTEMPTS × S3_MAX_RETRIES` attempts before it is reported as failed; WAL
  batch uploads are retried by bottomless every second until they succeed.
- At startup bottomless checks the bucket before the namespace opens. With an attempt timeout,
  an S3 endpoint that accepts connections but never answers makes sqld exit with
  `request has timed out` instead of hanging (checked with the release binary against such an
  endpoint: exit after 2 s with a 2 s timeout, still hanging after 15 s without one).

### 0003 — libsql-ffi: update bundled sqlean text extension to 0.28.4

sqld compiles the [sqlean](https://github.com/nalgeon/sqlean) extensions into SQLite
(`libsql-sys` feature `sqlean-extensions`). The bundled `text` extension
(`libsql-ffi/bundled/sqlean/text/`) is byte-identical to sqlean commit `0b4a0fc7a5`
(2024-08-07); this patch replaces the directory with sqlean release `0.28.4` (same file set,
7 files changed). It brings:

- [#164](https://github.com/nalgeon/sqlean/pull/164) — case conversions that change the UTF-8
  width are no longer done in place. Before, `SELECT text_casefold('ẞ')` from any client
  crashed the whole sqld process (SIGBUS, checked against a build of `LIBSQL_REF`).
- [#165](https://github.com/nalgeon/sqlean/pull/165) — UTF-8 decoding is bounded and invalid
  input is replaced.
- [#163](https://github.com/nalgeon/sqlean/pull/163) — the `text_nocase` collation keeps a
  total order, which indexes using it rely on.
- unused UTF-8 helpers removed.

Only the `text` extension changes; the other bundled sqlean extensions are untouched.

### 0004 — libsql-ffi: let % in sqlean text_like match an empty sequence

`text_like(pattern, str)` (case-insensitive, Unicode-aware `LIKE`) never let `%` match an
empty sequence: it consumed one character as soon as it saw `%`. So
`text_like('%abc%', 'abc')`, `text_like('a%b', 'ab')` and
`text_like('%привет%', 'Привет мир')` all returned 0. Still the case in sqlean 0.28.4 and on
its `main`; its test suite has no such case.

- `libsql-ffi/bundled/sqlean/text/rstring.c` — `rstring_like`: on `%` the rest of the pattern
  is tried at the current position first; on a mismatch `%` absorbs one more character.

Checked by `test:sqlean`: sqlean's own suite (`tests/sqlean/text.sql`, copied unchanged from
sqlean 0.28.4, MIT) plus `tests/sqlean/text-extra.sql`.

### 0005 — libsql-ffi: port sqlean base85 memory safety fix from 0.28.4

`libsql-ffi/bundled/sqlean/crypto/base85.c` replaced with the sqlean 0.28.4 file (commit
[`43a1b3e`](https://github.com/nalgeon/sqlean/commit/43a1b3e45834990c65c83d225a1d79a8e26b799c),
`base85.h` unchanged). Before, `base85_decode` allocated `len * 4 / 5` bytes while every `z`
expands into 4, so `SELECT crypto_decode('zzzzz', 'base85')` from any client wrote past the heap
buffer (AddressSanitizer: heap-buffer-overflow); `base85_encode` read past the end of input whose
length is not a multiple of 4. The rest of the bundled `crypto` extension is older than 0.28.4
and stays as it is. Checked by `test:sqlean` with the base85 checks of sqlean 0.28.4
(`tests/sqlean/crypto-base85.sql`).

### 0006–0010 — fixes for bugs found by fuzzing

Found by `fuzz:sqlean` (below) in the bundled sqlean; all of them are still present in sqlean
0.28.4 and on its `main`.

- **0006 — `crypto/base64.c`:** `base64_decode` looked up every input byte in a 123-entry table,
  so any byte ≥ 123 (`SELECT crypto_decode('Привет', 'base64')`) read past it
  (global-buffer-overflow). The table now covers all 256 byte values; non-base64 bytes map to 0
  and are rejected as before.
- **0007 — `text/extension.c`:** `text_substring(str, start, length)` and `right(str, n)`
  overflowed `int` for `INT_MIN` arguments (undefined behaviour; `right('abc', -2147483648)`
  returned `'abc'`). Both compute in 64 bits now; `right` with `n` below `-length(str)` returns
  `''`.
- **0008 — `text/bstring.c`:** `text_repeat('', n)` looped `n` times copying nothing:
  `repeat('', 2147483647)` makes 2³¹ iterations (over 10 s in the sanitizer build). An empty
  string now returns at once.
- **0009 — `stats/series.c`:** `generate_series` stepped past the `int64` range with signed
  overflow (undefined behaviour); the wrapped value never passed `stop`, so e.g.
  `SELECT count(*) FROM generate_series(9223372036854775800, 9223372036854775807, 5)` never
  finished. The cursor now ends the series when the next value would overflow, a step of
  `INT64_MIN` is negated without overflow, and the descending start is computed unsigned.
  (SQLite's own `ext/misc/series.c` was rewritten against this; the copy in sqlean predates
  that.)
- **0010 — `fuzzy/translit.c`:** the UTF-8 reader shifted an `int` left once per continuation
  byte, overflowing on long runs of them (undefined behaviour). It accumulates unsigned now, as
  SQLite's own reader does; valid UTF-8 decodes as before.

Regressions whose effect shows in SQL (wrong result, statement that never ends) are in
`tests/sqlean/fuzz-regressions.sql`, run by `test:sqlean`; memory errors are covered by the
fuzzer.

## Fuzzing sqlean

`mise run fuzz:sqlean [seconds] [--seed N]` builds `tests/sqlean/fuzz.c` together with SQLite
and sqlean from `.build/libsql` — the sources and flags of `libsql-ffi/build.rs` for sqld — with
AddressSanitizer and UndefinedBehaviorSanitizer, and:

1. discovers every function, table-valued module and collation sqlean registers (210 targets);
2. calls each with every edge value (NULL, integer and float extremes, empty, long, invalid and
   multi-byte UTF-8, pattern metacharacters, encoder names, blobs) in every argument position,
   the other arguments set to each of a few defaults (~300 000 statements);
3. calls each two-argument target with every pair of non-numeric edge values, since parsers
   take their input and its format or pattern together (~740 000 statements);
4. calls random targets with random arguments until the time budget runs out
   (~7 000 statements/s).

Built against the sources without 0005, it stops on the base85 bug
(`crypto_encode('Привет', 'base85')`).

Any sanitizer report, or a statement that runs longer than 10 s, stops the run and prints the
statement as SQL (also written to `.build/fuzz/sqlean-fuzz-repro.sql`). Allocations above 512 MB
fail inside the fuzzer instead of being made, so the code paths for a failed `malloc` are
exercised too.

Not covered: memory exhaustion. Functions such as `text_repeat` or `lpad` with huge counts — like
SQLite's own `printf` or `zeroblob` — build values up to `SQLITE_MAX_LENGTH` (1 GB), which is
enough to exceed a container memory limit.

## Searching text case-insensitively

Both forms work for Cyrillic and other non-ASCII text; `text_like` needs 0004, the
`text_lower` form already works at `LIBSQL_REF`:

```sql
WHERE text_like('%' || :q || '%', col)
WHERE text_lower(col) LIKE '%' || text_lower(:q) || '%'
```

The built-in `lower()`, `upper()`, `LIKE` and `COLLATE NOCASE` stay ASCII-only, as in SQLite.
`text_nocase` is a Unicode case-insensitive collation. `ё` and `е` are different letters for
all of them.
