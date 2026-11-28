#!/usr/bin/env python3
"""Preflight and collect helpers for dagu/codex-apply.yaml.

定位：受信任代理的范围防呆与 diff 审计门禁（非 OS 安全沙箱）。
"""

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from robot_guard import count_rounds

REQUIRED_SECTIONS = (
    "执行环境与硬边界",
    "允许修改文件白名单",
    "现状锚点与精确修改契约",
    "显式禁止项",
    "固定验证配方",
    "异常停机准则",
    "结构化回执",
)

RECEIPT_SCHEMA = {
    "$schema": "https://json-schema.org/draft/2020-12/schema",
    "type": "object",
    "required": [
        "status",
        "modified_files",
        "verification_runs",
        "invariants_checked",
        "blocked_reason",
        "notes_for_reviewer",
    ],
    "properties": {
        "status": {"type": "string", "enum": ["completed", "blocked", "failed"]},
        "modified_files": {"type": "array", "items": {"type": "string"}},
        "verification_runs": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["command", "exit_code", "log_path", "key_evidence"],
                "properties": {
                    "command": {"type": "string"},
                    "exit_code": {"type": "integer"},
                    "log_path": {"type": "string"},
                    "key_evidence": {"type": "string"},
                },
                "additionalProperties": False,
            },
        },
        "invariants_checked": {"type": "array", "items": {"type": "string"}},
        "blocked_reason": {"type": ["string", "null"]},
        "notes_for_reviewer": {"type": "string"},
    },
    "additionalProperties": False,
}


def git_status_map(worktree):
    out = subprocess.check_output(
        ["git", "-C", str(worktree), "status", "--porcelain=v1", "--untracked-files=all", "-z"],
        text=True,
    )
    status = {}
    entries = out.split("\0")
    i = 0
    while i < len(entries):
        entry = entries[i]
        i += 1
        if not entry:
            continue
        code = entry[:2]
        path = entry[3:]
        if "R" in code or "C" in code:
            status[entries[i]] = {"status": code, "digest": None}
            i += 1
        target = worktree / path
        digest = None
        if target.is_symlink():
            digest = "symlink:" + str(target.readlink())
        elif target.is_file():
            with target.open("rb") as stream:
                hasher = hashlib.sha256()
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    hasher.update(chunk)
            digest = f"{target.stat().st_mode:o}:{hasher.hexdigest()}"
        staged = subprocess.check_output(
            ["git", "-C", str(worktree), "--literal-pathspecs", "ls-files", "--stage", "--", path],
            text=True,
        ) if code[0] not in {" ", "?"} else ""
        status[path] = {"status": code, "digest": digest, "staged": staged}
    return status


def validate_rel_path(raw_path):
    """Validate and normalize a worktree-relative allowlist/diff path."""
    if not isinstance(raw_path, str) or not raw_path.strip():
        raise ValueError(f"Empty path is not allowed: {raw_path!r}")
    path = raw_path.strip()
    if path.startswith(("/", "\\")) or Path(path).is_absolute():
        raise ValueError(f"Absolute path is not allowed: {raw_path}")
    if len(path) >= 2 and path[1] == ":" and path[0].isalpha():
        raise ValueError(f"Drive-prefixed path is not allowed: {raw_path}")
    while path.startswith("./"):
        path = path[2:]
    path = path.rstrip("/")
    if not path or path == ".":
        raise ValueError(f"Invalid relative path: {raw_path}")
    parts = [p for p in re.split(r"[/\\]", path) if p]
    if ".." in parts:
        raise ValueError(f"Parent traversal ('..') is not allowed: {raw_path}")
    if ".git" in parts:
        raise ValueError(f".git path is not allowed: {raw_path}")
    return "/".join(parts)


def validate_worktree_path(worktree, raw_path):
    """Validate relative path syntax and ensure symlinks do not escape worktree root."""
    rel = validate_rel_path(raw_path)
    wt = Path(worktree).resolve()
    candidate = wt / rel
    try:
        resolved = candidate.resolve(strict=False)
    except (OSError, RuntimeError) as exc:
        raise ValueError(f"Failed to resolve path {rel}: {exc}") from exc
    if not resolved.is_relative_to(wt):
        raise ValueError(f"Path escapes worktree root via symlink: {rel} -> {resolved}")
    resolved_rel = resolved.relative_to(wt)
    if ".git" in resolved_rel.parts:
        raise ValueError(f"Path resolves into .git directory: {rel} -> {resolved}")
    return rel


def extract_allowlist(brief_text, worktree=None):
    lines = brief_text.splitlines()
    in_section = False
    allowlist = set()
    for line in lines:
        if re.match(r"^#{1,3}\s+.*允许修改文件白名单", line):
            in_section = True
            continue
        if in_section and re.match(r"^#{1,3}\s+", line):
            break
        if in_section:
            for match in re.findall(r"`([^`\s]+)`", line):
                if "/" in match or "." in match:
                    if worktree is not None:
                        allowlist.add(validate_worktree_path(worktree, match))
                    else:
                        allowlist.add(validate_rel_path(match))
    return sorted(allowlist)


def extract_session_id(events_path):
    """Extract session_id / thread_id from Codex JSONL event stream if present."""
    path = Path(events_path)
    if not path.is_file():
        return None
    fallback_thread_id = None
    with path.open("r", encoding="utf-8", errors="replace") as stream:
        for line in stream:
            line = line.strip()
            if not line:
                continue
            try:
                event = json.loads(line)
            except ValueError:
                continue
            if not isinstance(event, dict):
                continue
            containers = [event]
            for key in ("payload", "session", "thread", "data"):
                nested = event.get(key)
                if isinstance(nested, dict):
                    containers.append(nested)
            for obj in containers:
                sid = obj.get("session_id")
                if isinstance(sid, str) and sid.strip():
                    return sid.strip()
                tid = obj.get("thread_id")
                if fallback_thread_id is None and isinstance(tid, str) and tid.strip():
                    fallback_thread_id = tid.strip()
    return fallback_thread_id


def git_value(worktree, *args):
    return subprocess.check_output(["git", "-C", str(worktree), *args], text=True).strip()


def preflight(args):
    wt = Path(args.worktree).resolve()
    if not wt.is_dir():
        raise ValueError(f"Worktree does not exist: {wt}")
    head = git_value(wt, "rev-parse", "HEAD")
    branch = git_value(wt, "symbolic-ref", "--short", "HEAD")
    if args.expected_head and not head.startswith(args.expected_head):
        raise ValueError(f"HEAD mismatch: expected {args.expected_head}, got {head}")
    if not re.fullmatch(r"[a-z0-9]+(?:-[a-z0-9]+)*", args.change):
        raise ValueError(f"Invalid OpenSpec change name: {args.change}")
    if not (wt / "openspec/changes" / args.change / "tasks.md").is_file():
        raise ValueError(f"Missing OpenSpec change: {args.change}")

    ledger = wt / (args.ledger or f"docs/handoffs/{args.change}-defects.tsv")
    if args.ledger or ledger.is_file():
        chains = count_rounds(ledger)
        if any(d["escalate"] for d in chains.values()):
            raise ValueError(f"Defect ledger escalated; must redesign before apply: {ledger}")

    brief_text = Path(args.brief).read_text(encoding="utf-8")
    headings = list(re.finditer(r"^#{1,3} +(.+)$", brief_text, flags=re.M))
    section_indices = []
    for section in REQUIRED_SECTIONS:
        matches = [i for i, heading in enumerate(headings) if section in heading[1]]
        if len(matches) != 1:
            raise ValueError(f"Apply brief requires one heading for: {section}")
        section_indices.append(matches[0])
    if section_indices != sorted(section_indices):
        raise ValueError("Apply brief sections are out of order")
    begin = section_indices[0]
    environment = brief_text[headings[begin].end():headings[begin + 1].start()]
    expected = {"worktree": str(wt), "branch": branch, "HEAD": head, "change": args.change}
    for key, actual in expected.items():
        values = re.findall(rf"^\s*(?:- +)?`?{key}`?[:：]\s*`([^`\n]+)`\s*$", environment, flags=re.M)
        if len(values) != 1 or values[0] != actual:
            raise ValueError(f"Apply brief {key} must match {actual}")
    allowlist = extract_allowlist(brief_text, worktree=wt)
    if not allowlist:
        raise ValueError("Apply brief allowlist section yielded no file paths")

    run_dir = Path(args.run_dir)
    run_dir.mkdir(parents=True, exist_ok=True)
    (run_dir / "apply-receipt.schema.json").write_text(
        json.dumps(RECEIPT_SCHEMA, ensure_ascii=False, indent=2) + "\n"
    )
    (run_dir / "pre-status.json").write_text(
        json.dumps({"head": head, "allowlist": allowlist, "status": git_status_map(wt)}, ensure_ascii=False, indent=2) + "\n"
    )


def dispatch(args):
    worktree = Path(args.worktree).resolve()
    run_dir = Path(args.run_dir).resolve()
    brief = args.brief
    if args.resume_session_id and args.rework_brief:
        brief = args.rework_brief
    command = ["codex", "-a", "never", "-s", "danger-full-access", "-C", str(worktree), "exec"]
    if args.resume_session_id:
        command.append("resume")
    command.extend([
        "-m", "gpt-6.2-sol", "-c", 'model_reasoning_effort="high"', "--json", "--output-schema", str(run_dir / "apply-receipt.schema.json"),
        "-o", str(run_dir / "apply-receipt.json"),
    ])
    if args.resume_session_id:
        command.append(args.resume_session_id)
    command.append("-")
    receipt = run_dir / "apply-receipt.json"
    if receipt.exists():
        receipt.unlink()
    with Path(brief).open("rb") as source, (run_dir / "sol-events.jsonl").open("wb") as events:
        subprocess.run(command, stdin=source, stdout=events, check=True)


def collect(args):
    wt = Path(args.worktree).resolve()
    run_dir = Path(args.run_dir)
    pre = json.loads((run_dir / "pre-status.json").read_text())
    allowlist = {validate_worktree_path(wt, p) for p in pre["allowlist"]}
    pre_status = pre["status"]
    post_status = git_status_map(wt)

    head = git_value(wt, "rev-parse", "HEAD")
    if head != pre["head"]:
        raise ValueError(f"HEAD changed during apply: {pre['head']} -> {head}")
    newly_touched = [path for path in pre_status.keys() | post_status.keys()
                     if pre_status.get(path) != post_status.get(path)]
    for path in sorted(newly_touched):
        validate_worktree_path(wt, path)

    violations = [p for p in sorted(newly_touched) if p not in allowlist]
    receipt_path = run_dir / "apply-receipt.json"
    receipt = json.loads(receipt_path.read_text()) if receipt_path.is_file() else {"status": "failed"}
    session_id = extract_session_id(run_dir / "sol-events.jsonl")

    bundle = {
        "worktree": str(wt),
        "allowlist": sorted(allowlist),
        "newly_touched": sorted(newly_touched),
        "allowlist_violations": violations,
        "receipt_status": receipt.get("status"),
        "session_id": session_id,
        "passed": len(violations) == 0 and receipt.get("status") == "completed",
    }
    (run_dir / "return-bundle.json").write_text(json.dumps(bundle, ensure_ascii=False, indent=2) + "\n")
    if violations:
        print(f"Allowlist violation: {violations}", file=sys.stderr)
        sys.exit(1)
    if receipt.get("status") != "completed":
        print(f"Apply receipt status is {receipt.get('status')}", file=sys.stderr)
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    p_pre = sub.add_parser("preflight")
    p_pre.add_argument("--worktree", required=True)
    p_pre.add_argument("--change", required=True)
    p_pre.add_argument("--brief", required=True)
    p_pre.add_argument("--run-dir", required=True)
    p_pre.add_argument("--expected-head", default="")
    p_pre.add_argument("--ledger", default="")

    p_run = sub.add_parser("dispatch")
    p_run.add_argument("--worktree", required=True)
    p_run.add_argument("--brief", required=True)
    p_run.add_argument("--run-dir", required=True)
    p_run.add_argument("--resume-session-id", default="")
    p_run.add_argument("--rework-brief", default="")

    p_col = sub.add_parser("collect")
    p_col.add_argument("--worktree", required=True)
    p_col.add_argument("--brief", required=True)
    p_col.add_argument("--run-dir", required=True)

    args = parser.parse_args()
    try:
        if args.command == "preflight":
            preflight(args)
        elif args.command == "dispatch":
            dispatch(args)
        else:
            collect(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print(f"codex-apply: {error}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
