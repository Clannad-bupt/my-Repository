#!/usr/bin/env python3
"""No CJK chars in code/config (pygrep-equivalent, system-language hook).

Fails (exit 1, printing the matching lines) when any file passed on the
command line contains CJK; passes (exit 0) when all files are clean.

Byte range 0xE4..0xE9 = the UTF-8 lead bytes of U+4000..U+9FFF (CJK
ideographs and punctuation), matching the original pygrep hook that
compiled its pattern as bytes. Which files the hook sees is filtered by
the `files:` regex in .pre-commit-config.yaml.
"""
import re
import sys

PAT = re.compile(rb"[\xe4-\xe9]")


def main() -> int:
    bad = False
    for path in sys.argv[1:]:
        with open(path, "rb") as f:
            for lineno, line in enumerate(f, 1):
                if PAT.search(line):
                    bad = True
                    text = line.decode("utf-8", "replace").rstrip()
                    print(f"{path}:{lineno}:{text}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
