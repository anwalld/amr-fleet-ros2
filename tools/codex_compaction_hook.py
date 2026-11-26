#!/usr/bin/env python3
"""Compatibility wrapper for robot_guard.py (SessionStart compaction hook)."""

import json
import sys

from robot_guard import PROJECT, count_compactions, handle_hook, is_robot


def main():
    payload = json.load(sys.stdin)
    out = handle_hook(payload, mode="codex-session")
    if out:
        print(json.dumps(out, ensure_ascii=False))


if __name__ == "__main__":
    main()
