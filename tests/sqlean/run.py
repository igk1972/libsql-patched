#!/usr/bin/env python3
"""Run sqlean-style SQL checks against a running sqld over its HTTP API.

usage: run.py <sqld-url> <file.sql>...

Every line starting with `select ` is one check and must return a single row whose second
column is 1. Each check is a separate request, so a statement that kills sqld is named.
"""

from __future__ import annotations

import json
import sys
import urllib.request


def run(url: str, sql: str) -> str | None:
    body = {"requests": [{"type": "execute", "stmt": {"sql": sql}}, {"type": "close"}]}
    req = urllib.request.Request(
        f"{url}/v2/pipeline",
        data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"},
    )
    with urllib.request.urlopen(req, timeout=30) as resp:
        result = json.load(resp)["results"][0]
    if result["type"] != "ok":
        return f"error: {result['error']['message']}"
    rows = result["response"]["result"]["rows"]
    if len(rows) != 1 or rows[0][1].get("value") != "1":
        return f"got {rows}"
    return None


def main() -> int:
    url, paths = sys.argv[1], sys.argv[2:]
    total = failed = 0
    for path in paths:
        with open(path, encoding="utf-8") as f:
            checks = [line.strip().rstrip(";") for line in f if line.startswith("select ")]
        for sql in checks:
            total += 1
            try:
                problem = run(url, sql)
            except OSError as e:
                print(f"FAIL {sql}\n     request failed, sqld may have died: {e}")
                return 1
            if problem:
                failed += 1
                print(f"FAIL {sql}\n     {problem}")
    print(f"{total} checks, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
