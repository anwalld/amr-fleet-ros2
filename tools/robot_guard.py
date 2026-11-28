#!/usr/bin/env python3
"""Unified governance guard for robot (Codex, Antigravity, and CLI)."""

import argparse
from collections import Counter
import csv
import json
import re
import subprocess
import sys
from pathlib import Path

PROJECT = Path(__file__).resolve().parents[1]
OUTCOMES = {"regression", "fixed", "persists", "pending"}
SOURCE_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
    ".py", ".sh", ".srv", ".msg", ".action", ".yaml", ".yml",
    ".json", ".xml", ".cmake", ".launch", ".urdf", ".xacro",
}
IGNORED_PREFIXES = ("docs/", "openspec/")


def is_robot(cwd):
    if not cwd:
        return False
    git = subprocess.run(
        ["git", "-C", cwd, "rev-parse", "--path-format=absolute", "--git-common-dir"],
        capture_output=True,
        text=True,
        check=False,
    )
    return git.returncode == 0 and Path(git.stdout.strip()).resolve() == Path(subprocess.check_output(
        ["git", "-C", str(PROJECT), "rev-parse", "--path-format=absolute", "--git-common-dir"],
        text=True,
    ).strip()).resolve()


def count_compactions(transcript):
    records = set()
    notices = set()
    with Path(transcript).open() as stream:
        for line_number, line in enumerate(stream, start=1):
            try:
                entry = json.loads(line)
            except ValueError:
                continue
            if not isinstance(entry, dict):
                continue
            payload = entry.get("payload")
            key = entry.get("timestamp") or line_number
            if entry.get("type") == "CHECKPOINT":
                records.add(("antigravity", entry.get("step_index", key)))
            elif entry.get("type") == "compacted":
                records.add(key)
            elif entry.get("type") == "event_msg" and isinstance(payload, dict):
                if payload.get("type") in {"context_compacted", "compacted"}:
                    notices.add(key)
    return max(len(records), len(notices))


def count_rounds(path):
    defects = {}
    open_chains = {}
    with path.open(newline="") as stream:
        rows = csv.DictReader(stream, delimiter="\t")
        for row in rows:
            defect = row["defect"]
            outcome = row["outcome"]
            introduced = row.get("introduced_defect")
            if not defect or outcome not in OUTCOMES or not (row["evidence"] or "").strip():
                raise ValueError(f"Invalid record on line {rows.line_num}")
            if introduced and outcome != "regression":
                raise ValueError(f"Only regressions can introduce a defect on line {rows.line_num}")
            counts = defects.setdefault(defect, {
                "rounds": 0,
                "chain_length": 0,
                "chain": [],
                "unlinked_regressions": 0,
                "escalate": False,
            })
            expected = counts["rounds"] + 1
            if row["round"] != str(expected):
                raise ValueError(f"Expected round {expected} for {defect} on line {rows.line_num}")
            counts["rounds"] = expected

            if outcome == "fixed":
                open_chains.pop(defect, None)
            if outcome != "regression":
                continue
            chain = open_chains.pop(defect, [defect])
            if not introduced:
                counts["unlinked_regressions"] += 1
                continue
            if introduced in chain:
                raise ValueError(f"Introduced defect must identify a new regression on line {rows.line_num}")
            chain = [*chain, introduced]
            open_chains[introduced] = chain
            length = len(chain) - 1
            if length > counts["chain_length"]:
                counts.update(chain_length=length, chain=chain, escalate=length >= 3)
    return defects


def compaction_message(transcript, source=None):
    if not transcript:
        return "本对话的压缩次数未知，请按 robot 项目规则检查已记录的压缩事件。"
    try:
        count = count_compactions(transcript)
    except OSError:
        return "无法读取本对话的压缩记录，请按 robot 项目规则检查已记录的压缩事件。"
    count = max(count, int(source == "compact"))
    result = f"本对话日志已观测到 {count} 次上下文压缩。"
    if count >= 2:
        result += " 已达到 robot 项目的交接阈值。先写交接文档，再创建同项目新对话，复用当前 worktree 与分支。"
    return result


def check_defect_ledgers(tool_input, cwd, escalate_target="GPT-6.2 Astra High"):
    command = json.dumps(tool_input or {}, ensure_ascii=False)
    if "-defects.tsv" not in command or not is_robot(cwd):
        return []

    paths = subprocess.check_output(
        ["git", "-C", str(PROJECT), "worktree", "list", "--porcelain", "-z"],
        text=True,
    )
    workdir = None
    if isinstance(tool_input, dict):
        workdir = tool_input.get("workdir") or tool_input.get("Cwd")
        if isinstance(tool_input.get("args"), dict):
            workdir = workdir or tool_input["args"].get("Cwd")
    active_path = Path(workdir or cwd)
    if not active_path.is_absolute():
        active_path = Path(cwd) / active_path
    active_path = active_path.resolve()
    worktrees = [
        Path(item.removeprefix("worktree ")).resolve()
        for item in paths.split("\0") if item.startswith("worktree ")
    ]
    active_worktree = next(
        (path for path in (active_path, *active_path.parents) if path in worktrees), None
    )
    ledgers = [
        (worktree, ledger)
        for worktree in worktrees
        for ledger in sorted((worktree / "docs/handoffs").glob("*-defects.tsv"))
    ]
    absolute_names = {ledger.name for _, ledger in ledgers if str(ledger) in command}
    messages = []
    for worktree, ledger in ledgers:
        named = str(ledger) in command
        relative = (
            worktree == active_worktree and ledger.name in command
            and ledger.name not in absolute_names
        )
        if not (named or relative):
            continue
        try:
            chains = count_rounds(ledger)
        except (OSError, ValueError, KeyError) as error:
            messages.append(f"无法核对缺陷账本 {ledger}：{error}")
            continue
        for details in chains.values():
            if details["escalate"]:
                chain = " → ".join(details["chain"])
                messages.append(
                    f"{ledger}：{chain} 已达到三次引入新缺陷的阈值。检查该链是否已交接；"
                    f"若没有，暂停实现并按项目规则交接给 {escalate_target}。"
                )
    return messages


def is_tracked_code_or_script(rel_path):
    if rel_path.startswith(IGNORED_PREFIXES):
        return False
    p = Path(rel_path)
    if p.name == "CMakeLists.txt" or p.name == "package.xml":
        return True
    return p.suffix in SOURCE_EXTENSIONS


def _repo_basename_counts(wt, extra_paths=()):
    ls_out = subprocess.check_output(
        ["git", "-C", str(wt), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        text=True,
    )
    all_files = {p for p in ls_out.split("\0") if p}
    all_files.update(extra_paths)
    return Counter(Path(p).name for p in all_files)


def _matches_path_boundary(text, target, allow_dot_slash=False):
    prefix = r"(?<![A-Za-z0-9_./-])(?:\./)?" if allow_dot_slash else r"(?<![A-Za-z0-9_./-])"
    pattern = prefix + re.escape(target) + r"(?![A-Za-z0-9_./-])"
    return re.search(pattern, text) is not None


def verify_change(worktree, change):
    wt = Path(worktree).resolve()
    change_dir = wt / "openspec/changes" / change
    if not change_dir.is_dir():
        raise FileNotFoundError(f"OpenSpec change directory not found: {change_dir}")

    status_out = subprocess.check_output(
        ["git", "-C", str(wt), "status", "--porcelain=v1", "--untracked-files=all", "-z"],
        text=True,
    )
    changed_files = []
    entries = status_out.split("\0")
    i = 0
    while i < len(entries):
        entry = entries[i]
        i += 1
        if not entry:
            continue
        code = entry[:2]
        path = entry[3:]
        if "R" in code or "C" in code:
            previous_path = entries[i]
            i += 1
            if is_tracked_code_or_script(previous_path):
                changed_files.append(previous_path)
        if path.endswith("/"):
            continue
        if is_tracked_code_or_script(path):
            changed_files.append(path)

    doc_texts = []
    for doc_name in ("proposal.md", "design.md", "tasks.md", "verification.md"):
        doc_path = change_dir / doc_name
        if doc_path.is_file():
            doc_texts.append(doc_path.read_text(errors="replace"))
    specs_dir = change_dir / "specs"
    if specs_dir.is_dir():
        for spec_file in sorted(specs_dir.rglob("*.md")):
            doc_texts.append(spec_file.read_text(errors="replace"))
    combined = "\n".join(doc_texts)

    unique_changed = sorted(set(changed_files))
    basename_counts = _repo_basename_counts(wt, unique_changed)

    covered = []
    uncovered = []
    for rel in unique_changed:
        basename = Path(rel).name
        is_unique_basename = basename_counts.get(basename, 0) == 1
        if _matches_path_boundary(combined, rel, allow_dot_slash=True):
            covered.append(rel)
        elif is_unique_basename and _matches_path_boundary(combined, basename):
            covered.append(rel)
        else:
            uncovered.append(rel)

    return {
        "worktree": str(wt),
        "change": change,
        "checked_files": unique_changed,
        "covered": covered,
        "uncovered": uncovered,
        "passed": len(uncovered) == 0,
    }


def handle_hook(payload, mode=None):
    is_ag = (
        mode in {"ag-pre", "ag-post"}
        or "transcriptPath" in payload
        or "toolInput" in payload
        or payload.get("hookEventName") in {"PreInvocation", "PostInvocation"}
    )
    cwd = payload.get("cwd")
    if not is_robot(cwd):
        return None

    if mode == "codex-session" or (not is_ag and "transcript_path" in payload):
        msg = compaction_message(payload.get("transcript_path"), payload.get("source"))
        return {
            "hookSpecificOutput": {
                "hookEventName": "SessionStart",
                "additionalContext": msg,
            }
        }

    if mode == "codex-post" or (not is_ag and "tool_input" in payload):
        msgs = check_defect_ledgers(payload.get("tool_input"), cwd, "GPT-6.2 Astra High")
        if not msgs:
            return None
        return {
            "hookSpecificOutput": {
                "hookEventName": "PostToolUse",
                "additionalContext": "\n".join(msgs),
            }
        }

    if mode == "ag-pre" or (is_ag and payload.get("hookEventName") == "PreInvocation"):
        transcript = payload.get("transcriptPath") or payload.get("transcript_path")
        if transcript:
            try:
                count = count_compactions(transcript)
            except OSError:
                count = 0
            if count >= 2:
                return {
                    "injectSteps": [{"systemMessage": {
                        "systemMessage": compaction_message(transcript),
                    }}],
                }
        return None

    if mode == "ag-post" or (is_ag and payload.get("hookEventName") == "PostInvocation"):
        tool_input = payload.get("toolInput") or payload.get("tool_input") or payload.get("toolCall")
        messages = []
        if tool_input:
            messages = check_defect_ledgers(tool_input, cwd, "Gemini 4 Carbon (High)")
        elif payload.get("transcriptPath") and isinstance(payload.get("initialNumSteps"), int):
            try:
                with Path(payload["transcriptPath"]).open() as stream:
                    for line in stream:
                        try:
                            step = json.loads(line)
                        except ValueError:
                            continue
                        if (isinstance(step, dict) and isinstance(step.get("step_index"), int)
                                and step["step_index"] >= payload["initialNumSteps"]):
                            for call in step.get("tool_calls") or []:
                                messages.extend(check_defect_ledgers(call, cwd, "Gemini 4 Carbon (High)"))
            except OSError:
                pass
        msgs = list(dict.fromkeys(messages))
        if not msgs:
            return None
        return {
            "injectSteps": [{"systemMessage": {"systemMessage": "\n".join(msgs)}}],
        }
    return None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command")

    p_comp = sub.add_parser("check-compaction")
    p_comp.add_argument("transcript", type=Path)

    p_def = sub.add_parser("check-defects")
    p_def.add_argument("ledger", type=Path)

    p_ver = sub.add_parser("verify-change")
    p_ver.add_argument("--worktree", type=Path, default=Path.cwd())
    p_ver.add_argument("--change", required=True)

    p_hook = sub.add_parser("hook")
    p_hook.add_argument("--mode", choices=["codex-session", "codex-post", "ag-pre", "ag-post"])

    args = parser.parse_args()
    if args.command == "check-compaction":
        print(json.dumps({"compactions": count_compactions(args.transcript)}, ensure_ascii=False))
        return
    if args.command == "check-defects":
        try:
            print(json.dumps(count_rounds(args.ledger), ensure_ascii=False, indent=2))
        except (OSError, ValueError, KeyError) as error:
            parser.exit(2, f"{error}\n")
        return
    if args.command == "verify-change":
        try:
            res = verify_change(args.worktree, args.change)
        except (OSError, subprocess.SubprocessError) as error:
            parser.exit(2, f"{error}\n")
        print(json.dumps(res, ensure_ascii=False, indent=2))
        sys.exit(0 if res["passed"] else 1)
    if args.command == "hook":
        payload = json.load(sys.stdin)
        out = handle_hook(payload, args.mode)
        if out is not None or args.mode in {"ag-pre", "ag-post"}:
            print(json.dumps(out or {}, ensure_ascii=False))
        return
    parser.print_help()
    sys.exit(2)


if __name__ == "__main__":
    main()
