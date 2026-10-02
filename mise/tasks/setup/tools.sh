#!/usr/bin/env bash
set -euo pipefail

#MISE description="Install the pinned Rust toolchain and print versions"

mise install

echo "--- toolchain ---"
echo "rustc: $(mise exec -- rustc --version 2>/dev/null || echo MISSING)"
echo "cargo: $(mise exec -- cargo --version 2>/dev/null || echo MISSING)"
