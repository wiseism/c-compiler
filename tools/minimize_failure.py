#!/usr/bin/env python3
"""Delta-debug a textual source/IR file against a nonzero-exit predicate."""

import argparse
from pathlib import Path
import subprocess
import tempfile


def parse_args():
    parser = argparse.ArgumentParser(
        description="Remove lines while COMMAND {input} keeps exiting nonzero."
    )
    parser.add_argument("input", type=Path)
    parser.add_argument("-o", "--output", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=10.0)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.command[:1] == ["--"]:
        args.command = args.command[1:]
    if not args.command or not any("{input}" in item for item in args.command):
        parser.error("COMMAND must follow -- and contain an {input} placeholder")
    return args


def main():
    args = parse_args()
    lines = args.input.read_bytes().splitlines(keepends=True)

    with tempfile.TemporaryDirectory(prefix="lumen-reduce-") as directory:
        candidate = Path(directory) / args.input.name

        def interesting(content):
            candidate.write_bytes(b"".join(content))
            command = [item.replace("{input}", str(candidate)) for item in args.command]
            try:
                result = subprocess.run(
                    command,
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=args.timeout,
                    check=False,
                )
            except subprocess.TimeoutExpired:
                return False
            return result.returncode != 0

        if not interesting(lines):
            raise SystemExit("input does not satisfy the failure predicate")

        partitions = 2
        while len(lines) >= 2:
            chunk_size = max(1, (len(lines) + partitions - 1) // partitions)
            reduced = False
            for start in range(0, len(lines), chunk_size):
                trial = lines[:start] + lines[start + chunk_size :]
                if trial and interesting(trial):
                    lines = trial
                    partitions = max(2, partitions - 1)
                    reduced = True
                    break
            if not reduced:
                if partitions >= len(lines):
                    break
                partitions = min(len(lines), partitions * 2)

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(b"".join(lines))
    print(f"reduced to {len(lines)} line(s): {args.output}")


if __name__ == "__main__":
    main()
