#!/usr/bin/env python3
"""Compatibility wrapper for robot_guard.py (defect ledger counter & PostToolUse hook)."""

import argparse
import json
import sys
from pathlib import Path

from robot_guard import count_rounds, handle_hook


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("ledger", type=Path, nargs="?")
    parser.add_argument("--hook", action="store_true")
    args = parser.parse_args()
    if args.hook:
        out = handle_hook(json.load(sys.stdin), mode="codex-post")
        if out:
            print(json.dumps(out, ensure_ascii=False))
        return
    if args.ledger is None:
        parser.error("a ledger path or --hook is required")
    try:
        print(json.dumps(count_rounds(args.ledger), ensure_ascii=False, indent=2))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(2, f"{error}\n")


if __name__ == "__main__":
    main()
