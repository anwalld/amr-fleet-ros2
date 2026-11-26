"""Read Dagu progress, relay terminal results and clean archived runs."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shlex
import socket
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[1]
STATE = Path.home() / "Library/Application Support/robot-dagu"
INBOX = STATE / "inbox"
LABEL = "com.codex.robot.dagu-brief"


def git(worktree, *args):
    return subprocess.check_output(["git", "-C", worktree, *args], text=True).strip()


def remote(run, action):
    command = shlex.join(["python3", "-", run["output"], run["workspace"], action, run.get("identity", "-")])
    result = subprocess.run(["ssh", "-oBatchMode=yes", "-oConnectTimeout=8", "--", run["host"], command],
                            input=(ROOT / "scripts/dagu_brief_remote.py").read_text(),
                            text=True, capture_output=True, timeout=30, check=True)
    return json.loads(result.stdout)


def save(path, run):
    tmp = path.with_suffix(".tmp")
    tmp.write_text(json.dumps(run, ensure_ascii=False))
    tmp.replace(path)


def install():
    agent = Path.home() / "Library/LaunchAgents" / (LABEL + ".plist")
    agent.parent.mkdir(exist_ok=True)
    agent.write_bytes(plistlib.dumps(dict(Label=LABEL, RunAtLoad=True, StartInterval=60,
        ProgramArguments=[sys.executable, str(Path(__file__).resolve()), "poll"],
        StandardErrorPath=str(STATE / "relay.log"))))
    domain = f"gui/{os.getuid()}"
    if subprocess.run(["launchctl", "print", domain + "/" + LABEL], capture_output=True).returncode:
        subprocess.run(["launchctl", "bootstrap", domain, agent], check=True)


def resolve_adapter(requested, thread_id):
    if requested != "auto":
        return requested
    try:
        uuid.UUID(thread_id)
        if shutil.which("codex"):
            return "codex"
    except ValueError:
        pass
    return "local"


def register(args):
    if not re.fullmatch(r"[\w.@-]+", args.host) or args.host.startswith("-"):
        raise ValueError("invalid SSH host")
    worktree = Path(git(args.worktree, "rev-parse", "--show-toplevel")).resolve()
    branch = git(worktree, "symbolic-ref", "--short", "HEAD")
    for change in args.change:
        if not re.fullmatch(r"[a-z0-9-]+", change) or not (worktree / "openspec/changes" / change / "tasks.md").is_file():
            raise ValueError(f"missing active change {change}")
    adapter = resolve_adapter(args.adapter, args.thread)
    thread = str(uuid.UUID(args.thread)) if adapter == "codex" else args.thread
    run = dict(worktree=str(worktree), branch=branch, changes=sorted(set(args.change)),
               thread=thread, adapter=adapter, host=args.host,
               output=args.output, workspace=args.workspace)
    if adapter == "codex":
        codex = shutil.which("codex")
        if not codex:
            raise ValueError("codex CLI unavailable")
        run["codex"] = codex
    key = hashlib.sha256((args.host + "\n" + args.output).encode()).hexdigest()
    path = STATE / (key + ".json")
    status = remote(run, "read")
    run["identity"] = status["identity"]
    if args.archive_only:
        if not status["terminal"]:
            raise ValueError("archive-only requires a completed run")
        run["done"] = True
    if path.exists():
        existing = json.loads(path.read_text())
        if any(existing.get(k) != v for k, v in run.items()):
            raise ValueError("this run already has a different binding")
    else:
        save(path, run)
    install()


def archived(worktree, change):
    base = worktree / "openspec/changes"
    return not (base / change).exists() and any(
        (p / "tasks.md").is_file() for p in (base / "archive").glob("????-??-??-" + change))


def wake(run):
    sys.path.insert(0, str(STATE / "lib"))
    import websocket
    result = subprocess.run([run["codex"], "app-server", "daemon", "start"],
                            capture_output=True, text=True, timeout=45, check=True)
    endpoint = json.loads(result.stdout.strip().splitlines()[-1])["socketPath"]
    with socket.socket(socket.AF_UNIX) as sock:
        sock.settimeout(15)
        sock.connect(endpoint)
        ws = websocket.create_connection("ws://localhost/", socket=sock, timeout=15)
        try:
            def request(sequence, method, params):
                ws.send(json.dumps(dict(id=sequence, method=method, params=params)))
                while True:
                    reply = json.loads(ws.recv())
                    if reply.get("id") == sequence:
                        if "error" in reply:
                            raise RuntimeError(reply["error"]["message"])
                        return reply["result"]
            request(1, "initialize", {"clientInfo": {"name": "robot-dagu", "version": "1"}})
            ws.send(json.dumps({"method": "initialized"}))
            request(2, "thread/resume", {"threadId": run["thread"], "excludeTurns": True})
        finally:
            ws.close()


def deliver_local(run, status):
    INBOX.mkdir(mode=0o700, parents=True, exist_ok=True)
    run_id = status["run_id"]
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,127}", run_id):
        raise ValueError("invalid run id in terminal status")
    payload = {
        "run_id": run_id,
        "event": status["event"],
        "brief": status["brief"],
        "worktree": run["worktree"],
        "changes": run["changes"],
        "output": run["output"],
        "thread": run.get("thread"),
    }
    inbox_file = INBOX / f"{run_id}.json"
    tmp = inbox_file.with_suffix(".tmp")
    tmp.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n")
    tmp.replace(inbox_file)
    with (STATE / "terminal-events.jsonl").open("a") as stream:
        stream.write(json.dumps(payload, ensure_ascii=False) + "\n")
    script = f'display notification {json.dumps(status["brief"])} with title "robot Dagu 终态"'
    subprocess.run(["osascript", "-e", script], capture_output=True, check=False)


def handle(args, path, run):
    if args.command == "clean":
        wt = Path(git(args.worktree, "rev-parse", "--show-toplevel")).resolve()
        if (str(wt) != run["worktree"] or git(wt, "symbolic-ref", "--short", "HEAD") != run["branch"]
                or args.change not in run["changes"] or not all(archived(wt, c) for c in run["changes"])):
            return
        if not run.get("done"):
            raise ValueError("terminal delivery pending")
        print(remote(run, "clean")["removed"])
        path.unlink()
        return
    if run.get("done"):
        return
    status = remote(run, "read")
    if not status["terminal"]:
        return
    adapter = run.get("adapter") or ("codex" if run.get("codex") else "local")
    if adapter == "local":
        if status["event"] != run.get("sent"):
            deliver_local(run, status)
        run.update(sent=status["event"], pending_wake=False, done=True)
        save(path, run)
        return
    if status["event"] != run.get("sent"):
        message = ("Dagu 终态：" + status["brief"]
                   + " 请按项目既有授权核对终态证据并接续。对应 worktree：" + run["worktree"])
        subprocess.run([run["codex"], "queue", "--thread", run["thread"], "--message", message],
                       capture_output=True, text=True, timeout=60, check=True)
        run.update(sent=status["event"], pending_wake=True)
        save(path, run)
    if run.get("pending_wake"):
        try:
            wake(run)
        except RuntimeError as error:
            if "already has an active writer" not in str(error):
                raise
        run.update(pending_wake=False, done=True)
        save(path, run)


def show_status(run_id):
    matches = []
    for path in STATE.glob("*.json"):
        run = json.loads(path.read_text())
        output_name = Path(run["output"]).name
        if re.fullmatch(r"\d{4}-\d{2}-\d{2}-" + re.escape(run_id), output_name):
            matches.append(run)
    if len(matches) != 1:
        raise ValueError(f"expected one registered run for {run_id}, found {len(matches)}")
    print(remote(matches[0], "read")["brief"])


def main():
    parser = argparse.ArgumentParser()
    commands = parser.add_subparsers(dest="command", required=True)
    bind = commands.add_parser("bind")
    for name in ("host", "output", "workspace", "worktree", "thread"):
        bind.add_argument("--" + name, required=True)
    bind.add_argument("--adapter", choices=["auto", "codex", "local"], default="auto")
    bind.add_argument("--archive-only", action="store_true")
    bind.add_argument("--change", action="append", required=True)
    commands.add_parser("poll")
    status = commands.add_parser("status")
    status.add_argument("run_id")
    clean = commands.add_parser("clean")
    clean.add_argument("--worktree", required=True)
    clean.add_argument("--change", required=True)
    args = parser.parse_args()
    if args.command == "status":
        try:
            show_status(args.run_id)
        except (OSError, ValueError, subprocess.SubprocessError) as error:
            parser.exit(1, (getattr(error, "stderr", None) or str(error)).strip() + "\n")
        return
    STATE.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (STATE / "lock").open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if args.command == "bind":
            register(args)
            return
        failed = False
        for path in STATE.glob("*.json"):
            try:
                handle(args, path, json.loads(path.read_text()))
            except (OSError, ValueError, RuntimeError, subprocess.SubprocessError) as error:
                print(path.name, getattr(error, "stderr", None) or str(error), file=sys.stderr)
                failed = True
        sys.exit(int(failed))


if __name__ == "__main__":
    main()
