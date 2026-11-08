#!/usr/bin/env python3
"""Commit message checks (per CONTRIBUTING.md).

1. header follows Conventional Commits with type/scope whitelist
   (fixup! / squash! / merge messages rejected)
2. header and body separated by one blank line
3. body lines are '- ' bullets or indented continuation lines
4. footer trailers (Refs:/Fixes:/BREAKING CHANGE:/...) optional
5. every line <= 72 chars
"""
import re
import sys

TYPES = {"feat", "fix", "refactor", "perf", "test", "docs", "chore"}
SCOPES = {"kernel", "driver", "ops", "runtime", "scalar", "ap"}
HEADER = re.compile(r"^([a-z]+)(?:\(([a-z0-9_-]+)\))?(!)?: (.+)$")
TRAILER = re.compile(
    r"^(Refs|Fixes|Closes|Resolves|See|Co-authored-by|Signed-off-by|"
    r"BREAKING CHANGE|Reviewed-by|Acked-by|Tested-by|Reported-by|"
    r"Suggested-by)\s*:"
)


def check_header(line: str, errors: list) -> None:
    if line.startswith(("fixup! ", "squash! ", "Merge branch")):
        errors.append(f"rebase/merge leftover message: {line[:48]!r}")
        return
    m = HEADER.match(line)
    if not m:
        errors.append(f"expected '<type>(<scope>): <subject>', "
                      f"got {line[:48]!r}")
        return
    t, scope, _bang, subject = m.groups()
    if t not in TYPES:
        errors.append(f"unknown type {t!r} - allowed: "
                      f"{', '.join(sorted(TYPES))}")
    if scope and scope not in SCOPES:
        errors.append(f"unknown scope {scope!r} - allowed: "
                      f"{', '.join(sorted(SCOPES))}")
    if subject.endswith("."):
        errors.append("subject must not end with a period")


def main() -> int:
    with open(sys.argv[1], encoding="utf-8") as f:
        lines = [ln.rstrip("\n") for ln in f]
    lines = [ln for ln in lines if not ln.startswith("#")]
    while lines and not lines[-1].strip():
        lines.pop()
    if not lines:
        print("commit-msg: empty commit message")
        return 1

    errors = []
    check_header(lines[0], errors)
    if len(lines[0]) > 72:
        errors.append(f"header exceeds 72 chars ({len(lines[0])})")
    if len(lines) >= 2 and lines[1].strip():
        errors.append("missing blank line between header and body")

    for i, line in enumerate(lines[1:], start=2):
        s = line.strip()
        if not s:
            continue
        if TRAILER.match(s):
            continue
        if not (s.startswith("- ") or line.startswith((" ", "\t"))):
            errors.append(f"line {i} is not a '- ' bullet or indented "
                          f"continuation: {s[:48]!r}")
        if len(line) > 72:
            errors.append(f"line {i} exceeds 72 chars ({len(line)})")

    if errors:
        for e in errors:
            print("commit-msg:", e)
        print("commit-msg: see CONTRIBUTING.md for the commit convention")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
