#!/usr/bin/env python3

from pathlib import Path
import sys


def main():
    content = Path(sys.argv[1]).read_bytes()
    return 1 if b"TRIGGER_FAILURE" in content else 0


if __name__ == "__main__":
    raise SystemExit(main())
