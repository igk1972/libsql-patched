# libsql-patched

Patches for [libsql-server](https://github.com/tursodatabase/libsql) (`sqld`) on top of a
pinned upstream commit, plus [mise](https://mise.jdx.dev) tasks to apply and build them
locally. The repository keeps **patches only**: the upstream source is cloned into `.build/`
(gitignored) and patched there.

The container image is built from these patches in
[igk1972/builds](https://github.com/igk1972/builds) (`projects/libsql-server-patched`) and
published as `ghcr.io/igk1972/libsql-server-patched`.

## Upstream pin

`LIBSQL_REF` in `mise.toml`: `e4beacaa266fba930b637515e2082b42c2d6a817` — `main` on
2026-03-25 (libsql-server 0.24.33 + 20 commits), the commit upstream publishes as
`ghcr.io/tursodatabase/libsql-server:e4beaca`.

## Quick start

```sh
mise run setup:tools     # Rust toolchain pinned to upstream's rust-toolchain.toml
mise run setup:sources   # clone upstream at LIBSQL_REF + apply patches/
mise run build:sqld      # release sqld + bottomless-cli into .build/bin/
```

## Tasks

| Task | What it does |
|------|--------------|
| `setup:tools` | `mise install` + print toolchain versions |
| `setup:sources [--reset]` | clone upstream at `LIBSQL_REF` into `.build/libsql`, apply new patches as commits (on macOS also a local build fix, see [docs/patches.md](docs/patches.md#local-build-on-macos)); `--reset` discards local changes and re-applies everything |
| `build:sqld` | `cargo build --locked --release` of `libsql-server` and `bottomless-cli`, copied to `.build/bin/` |
| `test:bottomless [--include-ignored]` | `cargo test` of the `bottomless` crate and of the bottomless tests in `libsql-server`; with the flag, also upstream's `#[ignore]`d `backup_restore` (in a separate run) |
| `test:sqlean` | start the built `sqld` and run the SQL checks in `tests/sqlean/` over HTTP (sqlean's `text` suite and base85 checks, extra cases, fuzzing regressions) |
| `fuzz:sqlean [seconds] [--seed N]` | fuzz every sqlean function, module and collation under AddressSanitizer + UBSan, see [docs/patches.md](docs/patches.md#fuzzing-sqlean) |
| `patch:export` | rewrite `patches/` from the commits on top of `LIBSQL_REF` in `.build/libsql` |
| `clean [--all]` | remove `.build/bin/`; with `--all`, also the cloned sources and their build cache |

## Documentation

- [docs/patches.md](docs/patches.md) — how patches are applied and edited, and what each
  patch changes.
