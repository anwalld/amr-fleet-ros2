#!/usr/bin/env python3
"""Offline unit tests for governance, guard, and lint audit fixes."""

import argparse
import contextlib
import io
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS_DIR = ROOT / "tools"
SCRIPTS_DIR = ROOT / "scripts"
for directory in (TOOLS_DIR, SCRIPTS_DIR):
    if str(directory) not in sys.path:
        sys.path.insert(0, str(directory))

import codex_apply_helper  # noqa: E402
import lint_interface_coverage  # noqa: E402
import robot_guard  # noqa: E402


def _git(repo, *args):
    return subprocess.check_output(["git", "-C", str(repo), *args], text=True).strip()


def _init_temp_git_repo(root_dir):
    repo = Path(root_dir) / "repo"
    repo.mkdir()
    _git(repo, "init")
    _git(repo, "config", "user.email", "test@example.com")
    _git(repo, "config", "user.name", "Test User")
    (repo / "README.md").write_text("# temp\n", encoding="utf-8")
    _git(repo, "add", "README.md")
    _git(repo, "commit", "-m", "init")
    return repo.resolve()


def _make_apply_brief(wt, branch, head, change, allowlist_entries):
    bullets = "\n".join(f"- `{item}`" for item in allowlist_entries)
    return (
        "## 1. 执行环境与硬边界\n"
        f"- worktree: `{wt}`\n"
        f"- branch: `{branch}`\n"
        f"- HEAD: `{head}`\n"
        f"- change: `{change}`\n\n"
        "## 2. 允许修改文件白名单\n"
        f"{bullets}\n\n"
        "## 3. 现状锚点与精确修改契约\n"
        "- ok\n\n"
        "## 4. 显式禁止项\n"
        "- none\n\n"
        "## 5. 固定验证配方\n"
        "- ok\n\n"
        "## 6. 异常停机准则\n"
        "- ok\n\n"
        "## 7. 结构化回执\n"
        "- ok\n"
    )


class CodexApplyHelperAuditTests(unittest.TestCase):
    def test_extract_allowlist_rejects_unsafe_paths(self):
        for bad_path in (
            "../outside.py",
            "tools/../outside.py",
            "/etc/passwd",
            ".git/config",
            "sub/.git/HEAD",
        ):
            brief = f"## 允许修改文件白名单\n- `{bad_path}`\n## 下一节\n"
            with self.subTest(bad_path=bad_path):
                with self.assertRaises(ValueError):
                    codex_apply_helper.extract_allowlist(brief)

    def test_extract_allowlist_normalizes_dot_slash_and_rejects_escaping_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            wt = _init_temp_git_repo(tmp)
            outside = Path(tmp) / "outside.txt"
            outside.write_text("secret\n", encoding="utf-8")
            (wt / "good.py").write_text("x = 1\n", encoding="utf-8")
            (wt / "escape_link.py").symlink_to(outside)

            ok_brief = "## 允许修改文件白名单\n- `./good.py`\n"
            self.assertEqual(
                codex_apply_helper.extract_allowlist(ok_brief, worktree=wt),
                ["good.py"],
            )

            bad_brief = "## 允许修改文件白名单\n- `escape_link.py`\n"
            with self.assertRaises(ValueError):
                codex_apply_helper.extract_allowlist(bad_brief, worktree=wt)

    def test_preflight_rejects_escaping_symlink_and_collect_rejects_modified_symlink(self):
        with tempfile.TemporaryDirectory() as tmp:
            wt = _init_temp_git_repo(tmp)
            outside = Path(tmp) / "outside.txt"
            outside.write_text("outside\n", encoding="utf-8")
            change = "test-change"
            (wt / "openspec/changes" / change).mkdir(parents=True)
            (wt / "openspec/changes" / change / "tasks.md").write_text("- [ ] t\n", encoding="utf-8")
            (wt / "target.py").write_text("print('ok')\n", encoding="utf-8")
            _git(wt, "add", ".")
            _git(wt, "commit", "-m", "add change and target")
            head = _git(wt, "rev-parse", "HEAD")
            branch = _git(wt, "symbolic-ref", "--short", "HEAD")

            # 1. preflight rejects escaping symlink in allowlist
            (wt / "bad_link.py").symlink_to(outside)
            brief_bad = Path(tmp) / "brief_bad.md"
            brief_bad.write_text(_make_apply_brief(wt, branch, head, change, ["bad_link.py"]), encoding="utf-8")
            run_dir_bad = Path(tmp) / "run_bad"
            with self.assertRaises(ValueError):
                codex_apply_helper.preflight(argparse.Namespace(
                    worktree=str(wt),
                    change=change,
                    brief=str(brief_bad),
                    run_dir=str(run_dir_bad),
                    expected_head=head,
                    ledger="",
                ))
            (wt / "bad_link.py").unlink()

            # 2. preflight passes for target.py, then apply replaces target.py with escaping symlink
            brief_ok = Path(tmp) / "brief_ok.md"
            brief_ok.write_text(_make_apply_brief(wt, branch, head, change, ["target.py"]), encoding="utf-8")
            run_dir = Path(tmp) / "run_ok"
            codex_apply_helper.preflight(argparse.Namespace(
                worktree=str(wt),
                change=change,
                brief=str(brief_ok),
                run_dir=str(run_dir),
                expected_head=head,
                ledger="",
            ))
            (wt / "target.py").unlink()
            (wt / "target.py").symlink_to(outside)
            (run_dir / "apply-receipt.json").write_text(json.dumps({"status": "completed"}), encoding="utf-8")
            with self.assertRaises(ValueError):
                codex_apply_helper.collect(argparse.Namespace(
                    worktree=str(wt),
                    brief=str(brief_ok),
                    run_dir=str(run_dir),
                ))

    def test_collect_backfills_session_id_from_sol_events(self):
        with tempfile.TemporaryDirectory() as tmp:
            wt = _init_temp_git_repo(tmp)
            change = "test-session"
            (wt / "openspec/changes" / change).mkdir(parents=True)
            (wt / "openspec/changes" / change / "tasks.md").write_text("- [ ] t\n", encoding="utf-8")
            (wt / "target.py").write_text("x = 1\n", encoding="utf-8")
            _git(wt, "add", ".")
            _git(wt, "commit", "-m", "add target")
            head = _git(wt, "rev-parse", "HEAD")
            branch = _git(wt, "symbolic-ref", "--short", "HEAD")

            brief = Path(tmp) / "brief.md"
            brief.write_text(_make_apply_brief(wt, branch, head, change, ["target.py"]), encoding="utf-8")
            run_dir = Path(tmp) / "run"
            codex_apply_helper.preflight(argparse.Namespace(
                worktree=str(wt),
                change=change,
                brief=str(brief),
                run_dir=str(run_dir),
                expected_head=head,
                ledger="",
            ))
            (wt / "target.py").write_text("x = 2\n", encoding="utf-8")
            (run_dir / "apply-receipt.json").write_text(json.dumps({"status": "completed"}), encoding="utf-8")
            (run_dir / "sol-events.jsonl").write_text(
                '{"type":"thread.started","thread_id":"thr-123"}\n'
                '{"type":"session.configured","session_id":"sess-abc-999"}\n',
                encoding="utf-8",
            )
            codex_apply_helper.collect(argparse.Namespace(
                worktree=str(wt),
                brief=str(brief),
                run_dir=str(run_dir),
            ))
            bundle = json.loads((run_dir / "return-bundle.json").read_text(encoding="utf-8"))
            self.assertTrue(bundle["passed"])
            self.assertEqual(bundle["session_id"], "sess-abc-999")

            # Fallback to thread_id when only thread_id is present
            (run_dir / "sol-events.jsonl").write_text(
                '{"type":"thread.started","thread_id":"thr-only-456"}\n',
                encoding="utf-8",
            )
            codex_apply_helper.collect(argparse.Namespace(
                worktree=str(wt),
                brief=str(brief),
                run_dir=str(run_dir),
            ))
            bundle = json.loads((run_dir / "return-bundle.json").read_text(encoding="utf-8"))
            self.assertEqual(bundle["session_id"], "thr-only-456")


class RobotGuardVerifyChangeTests(unittest.TestCase):
    def test_duplicate_basename_rejects_basename_fallback_and_requires_rel_path(self):
        with tempfile.TemporaryDirectory() as tmp:
            wt = _init_temp_git_repo(tmp)
            for pkg in ("src/pkg_a", "src/pkg_b"):
                (wt / pkg).mkdir(parents=True)
                (wt / pkg / "CMakeLists.txt").write_text("# cmake\n", encoding="utf-8")
            (wt / "src/pkg_a/unique_helper_xyz.py").write_text("a = 1\n", encoding="utf-8")
            _git(wt, "add", ".")
            _git(wt, "commit", "-m", "add packages")

            change_dir = wt / "openspec/changes/demo-change"
            change_dir.mkdir(parents=True)
            # Only mentions src/pkg_a/CMakeLists.txt (not pkg_b), plus unique_helper_xyz.py by basename
            (change_dir / "tasks.md").write_text(
                "- 修改 `src/pkg_a/CMakeLists.txt` 与 `unique_helper_xyz.py`\n",
                encoding="utf-8",
            )

            # Modify both CMakeLists.txt and unique_helper_xyz.py
            (wt / "src/pkg_a/CMakeLists.txt").write_text("# cmake a\n", encoding="utf-8")
            (wt / "src/pkg_b/CMakeLists.txt").write_text("# cmake b\n", encoding="utf-8")
            (wt / "src/pkg_a/unique_helper_xyz.py").write_text("a = 2\n", encoding="utf-8")

            result = robot_guard.verify_change(wt, "demo-change")
            self.assertFalse(result["passed"])
            self.assertIn("src/pkg_a/CMakeLists.txt", result["covered"])
            self.assertIn("src/pkg_a/unique_helper_xyz.py", result["covered"])
            self.assertEqual(result["uncovered"], ["src/pkg_b/CMakeLists.txt"])

            # Mentioning only bare CMakeLists.txt must still NOT cover pkg_b/CMakeLists.txt
            (change_dir / "tasks.md").write_text(
                "- 修改 `CMakeLists.txt` 与 `unique_helper_xyz.py`\n",
                encoding="utf-8",
            )
            result2 = robot_guard.verify_change(wt, "demo-change")
            self.assertFalse(result2["passed"])
            self.assertEqual(
                result2["uncovered"],
                ["src/pkg_a/CMakeLists.txt", "src/pkg_b/CMakeLists.txt"],
            )

            # Mentioning non-boundary substring for unique file must NOT match
            (change_dir / "tasks.md").write_text(
                "- 修改 `src/pkg_a/CMakeLists.txt`、`src/pkg_b/CMakeLists.txt`、`not_unique_helper_xyz.py`\n",
                encoding="utf-8",
            )
            result3 = robot_guard.verify_change(wt, "demo-change")
            self.assertFalse(result3["passed"])
            self.assertEqual(result3["uncovered"], ["src/pkg_a/unique_helper_xyz.py"])


class LintInterfaceCoverageViaTests(unittest.TestCase):
    def _run_check(self, table_text, script_text, found_map):
        with tempfile.TemporaryDirectory() as tmp:
            tmp_path = Path(tmp)
            table_file = tmp_path / "interface_coverage.tsv"
            table_file.write_text(table_text, encoding="utf-8")
            case_script = tmp_path / "case_script.sh"
            case_script.write_text(script_text, encoding="utf-8")
            cases_map = {"ops:legacy_entries": case_script}
            buf = io.StringIO()
            with contextlib.redirect_stdout(buf):
                rc = lint_interface_coverage.check(
                    table_path=table_file,
                    cases_map=cases_map,
                    found_map=found_map,
                )
            return rc, buf.getvalue()

    def test_allows_via_submit_order_when_target_covered(self):
        table_text = (
            "kind\tname\towner\tcases\tnote\n"
            "service\t/v2/submit_order\trobot_tasks/mission_runner_node\tops:legacy_entries\t\n"
            "service\t/v2/legacy_submit\trobot_tasks/mission_runner_node\tops:legacy_entries\tvia:/v2/submit_order\n"
        )
        found_map = {
            ("service", "/v2/submit_order"): {"robot_tasks/mission_runner_node"},
            ("service", "/v2/legacy_submit"): {"robot_tasks/mission_runner_node"},
        }
        rc, out = self._run_check(table_text, "ros2 service call /v2/submit_order\n", found_map)
        self.assertEqual(rc, 0, msg=out)

    def test_rejects_arbitrary_via_fake(self):
        table_text = (
            "kind\tname\towner\tcases\tnote\n"
            "service\t/v2/submit_order\trobot_tasks/mission_runner_node\tops:legacy_entries\t\n"
            "service\t/v2/legacy_submit\trobot_tasks/mission_runner_node\tops:legacy_entries\tvia:/fake\n"
        )
        found_map = {
            ("service", "/v2/submit_order"): {"robot_tasks/mission_runner_node"},
            ("service", "/v2/legacy_submit"): {"robot_tasks/mission_runner_node"},
        }
        rc, out = self._run_check(table_text, "ros2 service call /v2/submit_order\n", found_map)
        self.assertEqual(rc, 1, msg=out)
        self.assertIn("不允许的转发声明：via:/fake", out)

    def test_rejects_via_submit_order_when_target_not_covered(self):
        table_text = (
            "kind\tname\towner\tcases\tnote\n"
            "service\t/v2/submit_order\trobot_tasks/mission_runner_node\tops:legacy_entries\t\n"
            "service\t/v2/legacy_submit\trobot_tasks/mission_runner_node\tops:legacy_entries\tvia:/v2/submit_order\n"
        )
        found_map = {
            ("service", "/v2/submit_order"): {"robot_tasks/mission_runner_node"},
            ("service", "/v2/legacy_submit"): {"robot_tasks/mission_runner_node"},
        }
        # Script does NOT reference /v2/submit_order
        rc, out = self._run_check(table_text, "echo nothing\n", found_map)
        self.assertEqual(rc, 1, msg=out)
        self.assertIn("转发目标 service /v2/submit_order 不存在或未被有效覆盖", out)


if __name__ == "__main__":
    unittest.main()
