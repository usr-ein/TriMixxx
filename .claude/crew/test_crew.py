"""Tests for crew: the guard's rules, the drift check, session names, and `crew merge`
on throwaway repositories (a parent with a submodule, as TriMixxx and the Mixxx fork).

    python3 -m unittest discover -s .claude/crew -v
"""

import argparse
import importlib.machinery
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent


def load_crew():
    loader = importlib.machinery.SourceFileLoader("crew_tool", str(HERE / "crew"))
    spec = importlib.util.spec_from_loader("crew_tool", loader)
    mod = importlib.util.module_from_spec(spec)
    loader.exec_module(mod)
    return mod


crew = load_crew()


def git(d, *args) -> str:
    return subprocess.run(["git", "-C", str(d), *args], check=True, capture_output=True, text=True).stdout.strip()


class Sandbox(unittest.TestCase):
    """A temp dir with its own git config, so no test reads Sam's (signing, identities)."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="crew-test-")).resolve()  # /var is /private/var
        cfg = self.tmp / "gitconfig"
        cfg.write_text("[user]\n\tname = T\n\temail = t@t\n[commit]\n\tgpgsign = false\n"
                       "[protocol \"file\"]\n\tallow = always\n[init]\n\tdefaultBranch = main\n"
                       "[advice]\n\tdetachedHead = false\n")
        self.env = {k: os.environ.get(k) for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM")}
        os.environ.update(GIT_CONFIG_GLOBAL=str(cfg), GIT_CONFIG_NOSYSTEM="1")
        self.saved = {k: getattr(crew, k) for k in ("MAIN", "CREW", "find_prs", "gh_json", "limits")}
        crew.CREW = self.tmp / "crewstate"

    def tearDown(self):
        for k, v in self.saved.items():
            setattr(crew, k, v)
        for k, v in self.env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        shutil.rmtree(self.tmp, ignore_errors=True)

    def repo(self, path: Path, branch="feat") -> Path:
        path.mkdir(parents=True)
        git(path, "init", "-q")
        (path / "f").write_text("x\n")
        git(path, "add", "f")
        git(path, "commit", "-qm", "init")
        if branch != "main":
            git(path, "switch", "-qc", branch)
        return path


class GuardTest(Sandbox):
    def setUp(self):
        super().setUp()
        crew.MAIN = self.tmp / "main"
        self.wt = self.repo(crew.MAIN / ".claude" / "worktrees" / "feat")
        self.minion = {"role": "minion", "mode": "night", "branch": "feat", "worktree": str(self.wt)}
        self.day_minion = dict(self.minion, mode="day")
        self.bitch = dict(self.minion, role="bitch", mode="day")
        self.night_bitch = dict(self.bitch, mode="night")
        self.nightman = {"role": "nightman", "mode": "night", "branch": None, "worktree": str(crew.MAIN)}

    def denied(self, reg, cmd):
        return crew.bash_reason(cmd, reg, str(self.wt)) is not None

    def test_minion_pushes_only_its_branch(self):
        self.assertFalse(self.denied(self.minion, "git push -u origin feat"))
        self.assertFalse(self.denied(self.minion, "git push --force-with-lease origin feat:refs/heads/feat"))
        self.assertFalse(self.denied(self.minion, "git push"))  # the worktree is on feat
        for cmd in ("git push origin main", "git push origin HEAD:main", "git push origin feat main",
                    "git push --all origin", "git push origin --delete feat", "git push --tags"):
            self.assertTrue(self.denied(self.minion, cmd), cmd)
        git(self.wt, "switch", "-qc", "other")
        self.assertTrue(self.denied(self.minion, "git push"))

    def test_github_only_through_crew_gh(self):
        self.assertTrue(self.denied(self.minion, "gh pr create --title x"))
        self.assertTrue(self.denied(self.minion, "GH_TOKEN=x command gh pr list"))
        self.assertFalse(self.denied(self.minion, "crew gh pr create --title x --body-file /tmp/b"))

    def test_text_is_not_a_command(self):
        self.assertFalse(self.denied(self.minion, "git commit -F - <<'EOF'\nsudo systemctl restart x\nEOF"))
        self.assertFalse(self.denied(self.minion, 'git commit -m "pkill and sudo, described"'))
        self.assertTrue(self.denied(self.minion, "true && sudo ls"))

    def test_machine_wide_harm(self):
        for cmd in ("pkill -f qemu", "killall docker", "docker builder prune -f", "docker system prune",
                    "diskutil list", "sudo dd if=x of=/dev/rdisk4", "pi-qemu deck golden x", "pi-qemu image build x"):
            for reg in (self.minion, self.bitch, self.nightman):
                self.assertTrue(self.denied(reg, cmd), f"{reg['role']}: {cmd}")

    def test_decks(self):
        self.assertFalse(self.denied(self.minion, "pi-qemu deck up feat"))
        self.assertFalse(self.denied(self.minion, "pi-qemu deck up feat-b --boot"))
        self.assertTrue(self.denied(self.minion, "pi-qemu deck rm usb-speed"))
        self.assertTrue(self.denied(self.minion, "pi-qemu deck shot --host trimixxx-pi-2 /tmp/x.png"))
        self.assertFalse(self.denied(self.day_minion, "pi-qemu deck shot --host trimixxx-pi-2 /tmp/x.png"))
        self.assertTrue(self.denied(self.bitch, "pi-qemu deck deploy feat mixxx"))
        self.assertFalse(self.denied(self.bitch, "pi-qemu deck shot feat /tmp/x.png"))
        self.assertTrue(self.denied(self.nightman, "pi-qemu deck up x"))
        self.assertFalse(self.denied(self.nightman, "pi-qemu deck stop feat"))

    def test_sessions_and_merges(self):
        self.assertTrue(self.denied(self.minion, "claude --bg -n x hi"))
        self.assertTrue(self.denied(self.minion, "crew minion other"))
        self.assertFalse(self.denied(self.nightman, "crew minion other --night --pitch /tmp/p.md"))
        self.assertTrue(self.denied(self.minion, "crew merge feat"))
        self.assertTrue(self.denied(self.night_bitch, "crew merge feat"))
        self.assertFalse(self.denied(self.bitch, "crew merge feat"))
        self.assertFalse(self.denied(self.nightman, "crew merge feat && crew clean feat"))

    def test_bitch_reads_only(self):
        self.assertFalse(self.denied(self.bitch, "git log main..feat && git diff main...feat && git branch"))
        for cmd in ("git commit -m x", "git checkout main", "git -C mixxx rebase main", "git branch -D feat"):
            self.assertTrue(self.denied(self.bitch, cmd), cmd)
        self.assertIsNotNone(crew.write_reason(str(self.wt / "README.md"), self.bitch))
        self.assertIsNone(crew.write_reason(str(self.wt / ".crew" / "review.md"), self.bitch))

    def test_minion_stays_in_its_worktree(self):
        self.assertIsNone(crew.write_reason(str(self.wt / "mixxx_config" / "x.js"), self.minion))
        self.assertIsNone(crew.write_reason("/tmp/notes.md", self.minion))
        self.assertIsNotNone(crew.write_reason(str(crew.MAIN / "README.md"), self.minion))
        self.assertTrue(self.denied(self.minion, f"git -C {crew.MAIN} commit -m x"))
        self.assertFalse(self.denied(self.minion, f"git -C {crew.MAIN} log -3"))

    def test_nightman_writes_only_its_state(self):
        self.assertIsNone(crew.write_reason(str(crew.CREW / "night" / "plan.md"), self.nightman))
        self.assertIsNotNone(crew.write_reason(str(self.wt / "x.py"), self.nightman))
        self.assertTrue(self.denied(self.nightman, "git push origin main"))


class HookTest(Sandbox):
    def setUp(self):
        super().setUp()
        crew.MAIN = self.tmp / "main"
        self.wt = self.repo(crew.MAIN / ".claude" / "worktrees" / "feat")
        (self.wt / ".crew").mkdir()
        (self.wt / ".crew" / "pitch.md").write_text("# feat: hot-plug sticks\n\nMode: night.\n")
        (self.wt / ".git" / "info" / "exclude").write_text(".crew/\n")
        (self.wt / "dj-usb.sh").write_text("work\n")
        git(self.wt, "add", "dj-usb.sh")
        git(self.wt, "commit", "-qm", "work")
        crew.limits = lambda: {"DRIFT_EVERY_CALLS": 3, "DRIFT_EVERY_MIN": 25}
        (crew.CREW / "sessions").mkdir(parents=True)
        self.reg = {"role": "minion", "mode": "night", "branch": "feat", "worktree": str(self.wt), "sid": "s1"}

    def test_drift_check_every_n_calls(self):
        results = [crew.hook_drift({}, self.reg) for _ in range(4)]
        self.assertEqual([r is not None for r in results], [False, False, True, False])
        text = results[2]["hookSpecificOutput"]["additionalContext"]
        self.assertIn("hot-plug sticks", text)
        self.assertIn("1 file changed", text)
        self.assertIn("0 uncommitted", text)

    def test_drift_state_is_not_a_session(self):
        """Regression: drift counters next to the registrations broke `crew resume` and `crew status`."""
        (crew.CREW / "sessions" / "s1.json").write_text(
            '{"role": "minion", "mode": "night", "branch": "feat", "worktree": "%s"}' % self.wt)
        (crew.CREW / "sessions" / "junk.json").write_text('{"calls": 3, "last": 1}')
        for _ in range(4):
            crew.hook_drift({}, self.reg)
        self.assertEqual([r["sid"] for r in crew.registrations()], ["s1"])
        self.assertTrue((crew.CREW / "drift" / "s1.json").exists())

    def test_a_session_crew_launched_registers_itself(self):
        env = dict(os.environ, CREW_LOCAL="1", CREW_STATE=str(crew.CREW), CREW_ROLE="minion", CREW_MODE="night",
                   CREW_BRANCH="feat", CREW_WORKTREE=str(self.wt))

        def start(sid, env):
            hook = json.dumps({"session_id": sid, "source": "resume", "cwd": str(self.wt)})
            return subprocess.run(["python3", str(HERE / "crew"), "hook", "session"], input=hook,
                                  capture_output=True, text=True, env=env).stdout

        out = json.loads(start("s7", env))["hookSpecificOutput"]
        self.assertEqual(out["sessionTitle"], "feat")
        self.assertIn("re-read", out["additionalContext"])
        self.assertEqual((crew.registration("s7") or {}).get("role"), "minion")
        env.pop("CREW_ROLE")
        self.assertEqual(start("s8", env), "")
        self.assertIsNone(crew.registration("s8"))

    def test_drift_only_for_minions(self):
        self.assertIsNone(crew.hook_drift({}, dict(self.reg, role="bitch")))

    def test_names(self):
        title = lambda reg: crew.hook_session({"source": "startup"}, reg)["hookSpecificOutput"]["sessionTitle"]
        self.assertEqual(title(self.reg), "feat")
        self.assertEqual(title(dict(self.reg, role="bitch")), "feat-bitch")
        self.assertEqual(title({"role": "nightman", "mode": "night"}), "nightman")
        resumed = crew.hook_session({"source": "compact"}, self.reg)["hookSpecificOutput"]
        self.assertIn(".claude/skills/minion/SKILL.md", resumed["additionalContext"])


class DelegationTest(Sandbox):
    """A worktree's copy of crew runs the main checkout's, so fixes reach running sessions."""

    def setUp(self):
        super().setUp()
        src = (HERE / "crew").read_text()
        self.main_copy = self.tmp / "main" / ".claude" / "crew" / "crew"
        self.wt_copy = self.tmp / "main" / ".claude" / "worktrees" / "feat" / ".claude" / "crew" / "crew"
        for path, text in ((self.main_copy, src.replace("    print(MAIN)\n", "    print('main copy', MAIN)\n")),
                           (self.wt_copy, src)):
            path.parent.mkdir(parents=True)
            path.write_text(text)
            (path.parent / "limits.env").write_text((HERE / "limits.env").read_text())

    def crew(self, *args, env=None, input=None):
        return subprocess.run(["python3", str(self.wt_copy), *args], capture_output=True, text=True, input=input,
                              env=dict(os.environ, CREW_STATE=str(crew.CREW), **(env or {})))

    def test_runs_the_main_checkouts_copy(self):
        self.assertEqual(self.crew("root").stdout.strip(), f"main copy {self.tmp / 'main'}")
        self.assertEqual(self.crew("root", env={"CREW_LOCAL": "1"}).stdout.strip(), str(self.tmp / "main"))

    def test_hooks_too(self):
        (crew.CREW / "sessions").mkdir(parents=True)
        wt = self.main_copy.parents[2] / ".claude" / "worktrees" / "feat"
        (crew.CREW / "sessions" / "s1.json").write_text(
            '{"role": "minion", "mode": "night", "branch": "feat", "worktree": "%s"}' % wt)
        self.main_copy.write_text(self.main_copy.read_text().replace(
            "crew guard ({reg['role']}", "main copy guard ({reg['role']}"))
        hook = '{"session_id": "s1", "cwd": "%s", "tool_name": "Bash", "tool_input": {"command": "sudo ls"}}' % wt
        self.assertIn("main copy guard (minion, night)", self.crew("hook", "guard", input=hook).stdout)
        other = hook.replace('"s1"', '"not-crew"')
        self.assertEqual(self.crew("hook", "guard", input=other).stdout, "")


class MergeTest(Sandbox):
    """origin/ holds bare repos; main/ is the main checkout, with mixxx as a submodule on its main."""

    def setUp(self):
        super().setUp()
        origin = self.tmp / "origin"
        mixxx = self.repo(self.tmp / "seed-mixxx", branch="main")
        git(origin.parent, "clone", "-q", "--bare", str(mixxx), str(origin / "mixxx.git"))
        parent = self.repo(self.tmp / "seed-parent", branch="main")
        git(parent, "submodule", "add", "-q", str(origin / "mixxx.git"), "mixxx")
        git(parent, "commit", "-qm", "add mixxx")
        git(origin.parent, "clone", "-q", "--bare", str(parent), str(origin / "parent.git"))
        crew.MAIN = self.tmp / "main"
        git(self.tmp, "clone", "-q", "--recurse-submodules", str(origin / "parent.git"), str(crew.MAIN))
        git(crew.MAIN / "mixxx", "switch", "-q", "main")
        self.labels = ["review:approved"]
        crew.find_prs = lambda b: {".": {"number": 1, "repo": "usr-ein/TriMixxx", "state": "OPEN", "isDraft": False,
                                         "labels": self.labels, "headRefOid": "x", "url": "u"}}
        crew.gh_json = lambda args, repo=None: {"state": "MERGED"}

    def feature(self, mixxx=True, bump=True) -> dict:
        """A minion's branch feat: a commit in mixxx, and in the parent a change plus the bump."""
        tips = {}
        if mixxx:
            mwt = self.tmp / "mwt"
            git(crew.MAIN / "mixxx", "worktree", "add", "-q", "-b", "feat", str(mwt))
            (mwt / "engine.cpp").write_text("change\n")
            git(mwt, "add", "engine.cpp")
            git(mwt, "commit", "-qm", "engine change")
            git(mwt, "push", "-q", "origin", "feat")
            tips["mixxx"] = git(mwt, "rev-parse", "HEAD")
        pwt = self.tmp / "pwt"
        git(crew.MAIN, "worktree", "add", "-q", "-b", "feat", str(pwt), "main")
        (pwt / "skin.xml").write_text("change\n")
        git(pwt, "add", "skin.xml")
        if mixxx and bump:
            git(pwt, "update-index", "--cacheinfo", f"160000,{tips['mixxx']},mixxx")
        git(pwt, "commit", "-qm", "feature, and bump mixxx")
        git(pwt, "push", "-q", "origin", "feat")
        tips["."] = git(pwt, "rev-parse", "HEAD")
        return tips

    def merge(self, dry_run=False) -> int:
        return crew.cmd_merge(argparse.Namespace(branch="feat", dry_run=dry_run, unapproved=False))

    def assert_refused(self, code, words):
        with self.assertRaises(crew.Fail) as cm:
            self.merge()
        self.assertEqual(cm.exception.code, code, str(cm.exception))
        self.assertIn(words, str(cm.exception))

    def test_merges_deepest_first_and_pushes(self):
        tips = self.feature()
        self.assertEqual(self.merge(), 0)
        origin = self.tmp / "origin"
        self.assertEqual(git(origin / "mixxx.git", "rev-parse", "main"), tips["mixxx"])
        self.assertEqual(git(origin / "parent.git", "rev-parse", "main"), tips["."])
        self.assertEqual(git(crew.MAIN, "rev-parse", "HEAD"), tips["."])
        self.assertEqual(git(crew.MAIN / "mixxx", "rev-parse", "HEAD"), tips["mixxx"])
        self.assertEqual(self.merge(), 0)  # again: nothing left to do

    def test_parent_only(self):
        tips = self.feature(mixxx=False)
        before = git(self.tmp / "origin" / "mixxx.git", "rev-parse", "main")
        self.assertEqual(self.merge(), 0)
        self.assertEqual(git(self.tmp / "origin" / "parent.git", "rev-parse", "main"), tips["."])
        self.assertEqual(git(self.tmp / "origin" / "mixxx.git", "rev-parse", "main"), before)

    def test_dry_run_changes_nothing(self):
        self.feature()
        before = git(self.tmp / "origin" / "parent.git", "rev-parse", "main")
        self.assertEqual(self.merge(dry_run=True), 0)
        self.assertEqual(git(self.tmp / "origin" / "parent.git", "rev-parse", "main"), before)

    def test_refuses_unapproved(self):
        self.feature()
        self.labels = ["review:changes"]
        self.assert_refused(2, "not approved")
        self.labels = ["review:approved", "review:needs-sam"]
        self.assert_refused(2, "waits for Sam")

    def test_refuses_when_main_moved(self):
        self.feature()
        (crew.MAIN / "other").write_text("y\n")
        git(crew.MAIN, "add", "other")
        git(crew.MAIN, "commit", "-qm", "another feature")
        git(crew.MAIN, "push", "-q", "origin", "main")
        self.assert_refused(3, "rebase")

    def test_refuses_missing_bump(self):
        self.feature(bump=False)
        self.assert_refused(2, "bump")

    def test_refuses_unpushed_main(self):
        self.feature()
        (crew.MAIN / "mixxx" / "local").write_text("z\n")
        git(crew.MAIN / "mixxx", "add", "local")
        git(crew.MAIN / "mixxx", "commit", "-qm", "unpushed")
        self.assert_refused(4, "unpushed")

    def test_refuses_unpushed_branch(self):
        self.feature()
        pwt = self.tmp / "pwt"
        (pwt / "late").write_text("w\n")
        git(pwt, "add", "late")
        git(pwt, "commit", "-qm", "late fix, not pushed")
        self.assert_refused(2, "not pushed")


if __name__ == "__main__":
    unittest.main()
