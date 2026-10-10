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
        self.saved = {k: getattr(crew, k) for k in ("MAIN", "CREW", "find_prs", "gh_json", "limits", "approved_commit",
                                                    "run", "live_session", "claude_sessions", "resolve_host", "locks")}
        crew.CREW = self.tmp / "crewstate"
        crew.resolve_host = lambda name: name.lower()  # not this Mac's ~/.ssh/config

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

    def test_redirections_are_not_arguments(self):
        """Four minions' pushes were refused as pushing a branch named 2: the 2 of 2>&1."""
        for cmd in ("git push -u origin feat 2>&1 | tail -3", "git push origin feat >/tmp/push.log 2>&1",
                    "git push origin feat &>log", "git commit -F - <<'EOF'\nmsg\nEOF"):
            self.assertFalse(self.denied(self.minion, cmd), cmd)
        for cmd in ("git push origin 2>/dev/null main", "git push origin feat 2>&1|sudo ls", "diff <(sudo cat x) y",
                    "cat <<< x; sudo ls", "echo $(sudo ls) >f", ">log git push origin main"):
            self.assertTrue(self.denied(self.minion, cmd), cmd)

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
        self.assertTrue(self.denied(self.day_minion, "pi-qemu deck shot --host trimixxx-pi-2 /tmp/x.png"))  # no lock
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

    def test_cd_into_the_main_checkout(self):
        main = crew.MAIN
        main.mkdir(exist_ok=True)
        self.assertTrue(self.denied(self.minion, f"cd {main} && git add x && git commit -m y"))
        self.assertTrue(self.denied(self.minion, f'cd "{main}"; git commit -m y'))
        self.assertFalse(self.denied(self.minion, f"cd {main} && git log -3 && cd {self.wt} && git commit -m y"))
        self.assertFalse(self.denied(self.minion, f"cd {main} && git push origin feat"))  # its own branch

    def test_real_decks_over_ssh(self):
        """Without a lock of its own, no minion reaches one, by day or night (LockTest: with one)."""
        for cmd in ("ssh trimixxx-pi-2 ls", "ssh -o BatchMode=yes pi@trimixxx2 true", "scp f pi@192.168.1.118:/tmp",
                    "rsync -av dir/ trimixxx-pi:/home/pi/x", "sftp trimixxx3", "ssh sam1902@169.254.232.146 true",
                    "ssh sam1902@fe80::dea6:32ff:fe88:2118%en12 true", "scp f 'pi@[fe80::1%en12]:/tmp'",
                    "ssh -o HostName=169.254.48.149 deck true", "ssh -o HostName=fe80::1%%en12 deck true"):
            self.assertTrue(self.denied(self.minion, cmd), cmd)
            self.assertTrue(self.denied(self.day_minion, cmd), cmd)
            self.assertTrue(self.denied(self.bitch, cmd), cmd)
        for cmd in ("ssh deck ls", "ssh -p 2222 trimixxx0 ls", "scp f feat:/tmp", "rsync -e 'ssh -p 22' a b",
                    "scp ./a:b /tmp/c", "ssh -o HostKeyAlias=192.168.1.80 deck true"):
            self.assertFalse(self.denied(self.minion, cmd), cmd)

    def test_decks_by_exact_name(self):
        self.assertTrue(self.denied(self.minion, "pi-qemu deck rm feat-hotplug"))
        self.assertFalse(self.denied(self.minion, "pi-qemu deck rm feat-b"))

    def test_read_only_git_is_not_a_write(self):
        for cmd in ("git branch -a", "git -C mixxx branch --list feat", "git worktree list", "git submodule status",
                    "git stash list", "git tag", "git config --get user.email", "git remote -v", "git fetch origin"):
            self.assertFalse(self.denied(self.bitch, cmd), cmd)
            self.assertFalse(self.denied(self.nightman, cmd), cmd)
        for cmd in ("git worktree add x", "git branch newname", "git submodule update --init", "git stash",
                    "git tag pi/v1.0.0", "git config user.email x@y", "git remote add up url"):
            self.assertTrue(self.denied(self.bitch, cmd), cmd)

    def test_every_command_is_checked(self):
        self.assertTrue(self.denied(self.minion, "git push origin feat && sudo ls"))
        self.assertTrue(self.denied(self.minion, "echo `sudo ls`"))
        self.assertTrue(self.denied(self.minion, "for f in a b; do sudo rm $f; done"))
        self.assertTrue(self.denied(self.minion, "if true; then pkill qemu; fi"))

    def test_job_directory_is_scratch(self):
        job = str(Path.home() / ".claude" / "jobs" / "abc" / "tmp" / "review.md")
        self.assertIsNone(crew.write_reason(job, self.bitch))
        self.assertIsNone(crew.write_reason(job, self.nightman))

    def test_nightman_writes_only_its_state(self):
        self.assertIsNone(crew.write_reason(str(crew.CREW / "night" / "plan.md"), self.nightman))
        self.assertIsNotNone(crew.write_reason(str(self.wt / "x.py"), self.nightman))
        self.assertTrue(self.denied(self.nightman, "git push origin main"))


class VerdictTest(unittest.TestCase):
    """The resources verdict, Docker's VM disk among it."""

    LIM = {"DISK_MIN_GB": 40, "DISK_SHED_GB": 20, "DOCKER_MIN_GB": 8, "DOCKER_SHED_GB": 4, "LOAD_MAX": 0.75,
           "LOAD_SHED": 1.5, "MAX_MINIONS": 2, "MAX_DECKS": 3, "DECKS_PER_MINION": 1}

    def verdict(self, docker_gb, purpose):
        snap = {"ncpu": 12, "load": [1, 1, 1], "pressure": 1, "disk_gb": 100, "docker_gb": docker_gb, "decks": [],
                "minions": []}
        return crew.verdict(snap, self.LIM, purpose)[0]

    def test_docker_disk(self):
        self.assertEqual([self.verdict(20, p) for p in ("minion", "build", "deck")], ["ok", "ok", "ok"])
        self.assertEqual([self.verdict(6, p) for p in ("minion", "build", "deck")], ["hold", "hold", "ok"])
        self.assertEqual([self.verdict(3, p) for p in ("minion", "build", "deck")], ["shed", "shed", "shed"])
        self.assertEqual(self.verdict(None, "build"), "ok")  # Docker not up: preflight says so, not the verdict


class LockTest(Sandbox):
    """Real decks: one holder per unit, and a minion reaches one only through a lock of its own, by day or night."""

    def setUp(self):
        super().setUp()
        crew.MAIN = self.tmp / "main"
        self.wt = self.repo(crew.MAIN / ".claude" / "worktrees" / "feat")
        self.repo(crew.MAIN / ".claude" / "worktrees" / "other")
        ssh_config = {"trimixxx-pi": "192.168.1.80", "trimixxx-pi-2": "trimixxx2.local", "t1": "192.168.1.80"}
        crew.resolve_host = lambda name: ssh_config.get(name.lower(), name.lower())
        sid = os.environ.get("CLAUDE_CODE_SESSION_ID")
        self.addCleanup(lambda: os.environ.update(CLAUDE_CODE_SESSION_ID=sid) if sid else
                        os.environ.pop("CLAUDE_CODE_SESSION_ID", None))
        self.regs = {sid: crew.register_session(sid, role, mode, branch, self.wt) for sid, role, mode, branch in (
            ("feat-day", "minion", "day", "feat"), ("feat-night", "minion", "night", "feat"),
            ("other-day", "minion", "day", "other"), ("bitch", "bitch", "night", "feat"),
            ("nightman", "nightman", "night", None))}

    def lock(self, sid, unit, *names, holder=None, why=None) -> int:
        """`crew lock` as that session; sid None: Sam."""
        os.environ["CLAUDE_CODE_SESSION_ID"] = sid or "sams-own-session"
        return crew.cmd_lock(argparse.Namespace(unit=unit, names=list(names), holder=holder, why=why))

    def unlock(self, sid, unit) -> int:
        os.environ["CLAUDE_CODE_SESSION_ID"] = sid or "sams-own-session"
        return crew.cmd_unlock(argparse.Namespace(unit=unit))

    def refused(self, fn, code, words):
        with self.assertRaises(crew.Fail) as cm:
            fn()
        self.assertEqual(cm.exception.code, code, str(cm.exception))
        self.assertIn(words, str(cm.exception))

    def reason(self, sid, cmd):
        return crew.bash_reason(cmd, self.regs[sid], str(self.wt))

    def test_one_holder_per_unit(self):
        self.assertEqual(self.lock("feat-day", "trimixxx1", "trimixxx-pi"), 0)
        self.refused(lambda: self.lock("other-day", "trimixxx1"), 3, "feat's")
        self.refused(lambda: self.lock("other-day", "trimixxx2", "192.168.1.80"), 3, "trimixxx1")  # trimixxx-pi's
        self.refused(lambda: self.lock("other-day", "trimixxx2", "t1"), 3, "trimixxx1")  # another alias of it
        self.assertEqual(self.lock("other-day", "trimixxx2", "trimixxx-pi-2"), 0)
        self.assertEqual(sorted(crew.locks()), ["trimixxx1", "trimixxx2"])
        self.refused(lambda: self.lock("feat-day", "trimixxx-pi"), 2, "not a unit")

    def test_two_at_the_same_moment(self):
        crew.locks = lambda: {}  # both read the unit as free...
        self.assertEqual(self.lock("feat-day", "trimixxx1"), 0)
        self.refused(lambda: self.lock("other-day", "trimixxx1"), 3, "locked just now")  # ...one gets it
        crew.locks = self.saved["locks"]
        self.assertEqual(crew.locks()["trimixxx1"]["holder"], "feat")

    def test_the_lock_is_the_way_in(self):
        cmds = ("ssh trimixxx-pi ls", "scp f sam1902@192.168.1.80:/tmp", "rsync -a d/ t1:/tmp/d",
                "pi-qemu deck ship --host trimixxx-pi", "pi-qemu deck shot --host=trimixxx1.local /tmp/x.png")
        for cmd in cmds:
            self.assertIn("no lock of yours", self.reason("feat-night", cmd), cmd)
        self.lock("nightman", "trimixxx1", "trimixxx-pi", holder="feat")
        for cmd in cmds:
            self.assertIsNone(self.reason("feat-night", cmd), cmd)  # at night too: the nightman granted it
            self.assertIsNone(self.reason("feat-day", cmd), cmd)
            self.assertIn("feat's", self.reason("other-day", cmd), cmd)
            self.assertIn("only a minion", self.reason("bitch", cmd), cmd)
            self.assertIn("only a minion", self.reason("nightman", cmd), cmd)

    def test_addresses_the_lock_does_not_name(self):
        self.lock("nightman", "trimixxx1", "trimixxx-pi", holder="feat")
        self.assertIn("`crew lock trimixxx1 169.254.232.146` adds it",
                      self.reason("feat-night", "ssh sam1902@169.254.232.146 true"))
        self.assertEqual(self.lock("feat-night", "trimixxx1", "169.254.232.146", "fe80::dea6:32ff:fe88:2118%en12"), 0)
        for cmd in ("ssh sam1902@169.254.232.146 true", "scp f 'sam1902@[fe80::dea6:32ff:fe88:2118%en12]:/tmp'",
                    "ssh -o HostName=fe80::dea6:32ff:fe88:2118%%en12 -o HostKeyAlias=192.168.1.80 trimixxx-pi true"):
            self.assertIsNone(self.reason("feat-night", cmd), cmd)
        self.assertIn("feat's", self.reason("other-day", "ssh -o HostName=fe80::dea6:32ff:fe88:2118%%en12 deck true"))
        self.assertIsNotNone(self.reason("feat-night", "ssh -o HostName=169.254.48.149 trimixxx-pi true"))
        self.assertIn("`crew lock trimixxx2 trimixxx-pi-2`", self.reason("feat-night", "ssh trimixxx-pi-2 true"))
        for cmd in ("ssh $DECK true", 'scp f "$H":/tmp', "pi-qemu deck ship --host $D"):
            self.assertIn("not a variable", self.reason("feat-day", cmd), cmd)

    def test_at_night_the_nightman_grants_it(self):
        self.refused(lambda: self.lock("feat-night", "trimixxx1", "trimixxx-pi"), 2, "the nightman grants")
        self.refused(lambda: self.lock("nightman", "trimixxx1"), 2, "--for")
        self.refused(lambda: self.lock("nightman", "trimixxx1", holder="nosuch"), 2, "no crew worktree")
        self.refused(lambda: self.lock("feat-day", "trimixxx2", holder="other"), 2, "for itself")
        self.refused(lambda: self.lock("bitch", "trimixxx2"), 2, "bitch")
        self.assertEqual(self.lock("nightman", "trimixxx1", "trimixxx-pi", holder="feat", why="Sam 18:01: go"), 0)
        self.assertEqual(self.lock("feat-night", "trimixxx1", "169.254.232.146"), 0)  # it adds to its own
        lk = crew.locks()["trimixxx1"]
        self.assertEqual((lk["holder"], lk["by"], lk["why"]), ("feat", "nightman", "Sam 18:01: go"))
        self.assertIn("169.254.232.146", lk["names"])
        self.refused(lambda: self.lock("feat-night", "trimixxx1", "trimixxx-pi-2"), 2, "trimixxx2's name")

    def test_who_frees_a_unit(self):
        self.lock("nightman", "trimixxx1", holder="feat")
        self.refused(lambda: self.unlock("other-day", "trimixxx1"), 2, "Only its holder")
        self.assertEqual(self.unlock("feat-night", "trimixxx1"), 0)
        self.lock("nightman", "trimixxx1", holder="feat")
        self.assertEqual(self.unlock("nightman", "trimixxx1"), 0)
        self.assertEqual(self.lock(None, "trimixxx2"), 0)  # Sam keeps one for himself
        self.assertEqual(crew.locks()["trimixxx2"]["holder"], "Sam")
        self.assertIn("Sam's", self.reason("feat-day", "ssh trimixxx-pi-2 true"))
        self.assertEqual(self.unlock(None, "trimixxx2"), 0)
        self.assertEqual(crew.locks(), {})

    def test_clean_frees_the_branchs_units(self):
        self.lock("nightman", "trimixxx1", holder="feat")
        self.lock("nightman", "trimixxx2", "trimixxx-pi-2", holder="other")
        self.assertEqual(crew.release_locks("feat"), ["trimixxx1"])
        self.assertEqual(list(crew.locks()), ["trimixxx2"])


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

        def start(sid, env, cwd=self.wt):
            hook = json.dumps({"session_id": sid, "source": "resume", "cwd": str(cwd)})
            return subprocess.run(["python3", str(HERE / "crew"), "hook", "session"], input=hook,
                                  capture_output=True, text=True, env=env).stdout

        out = json.loads(start("s7", env))["hookSpecificOutput"]
        self.assertEqual(out["sessionTitle"], "feat")
        self.assertIn("re-read", out["additionalContext"])
        self.assertEqual((crew.registration("s7") or {}).get("role"), "minion")
        # Claude's daemon gives every background session the environment of the launch that started it: one that
        # starts anywhere else (a crew-tune session in the main checkout) is not that minion.
        self.assertEqual(start("s9", env, cwd=crew.MAIN), "")
        self.assertIsNone(crew.registration("s9"))
        env.pop("CREW_ROLE")
        self.assertEqual(start("s8", env), "")
        self.assertIsNone(crew.registration("s8"))

    def test_a_background_launch_carries_no_crew_role(self):
        """Or Claude's daemon, if this launch starts it, hands that role to every background session after it."""
        seen = {}

        def run(cmd, cwd=None, env=None, **kw):
            seen["env"] = env
            return subprocess.CompletedProcess(cmd, 0, "backgrounded · a1b2c3\n", "")
        crew.run, crew.live_session = run, lambda name, sessions=None: None
        crew.claude_sessions = lambda: [{"id": "a1b2c3", "sessionId": "s5", "name": "feat"}]
        os.environ["CREW_ROLE"] = "bitch"  # a launcher whose own environment came from such a daemon
        self.addCleanup(os.environ.pop, "CREW_ROLE", None)
        crew.launch("feat", self.wt, True, "/minion night", crew.crew_env("minion", "night", "feat", self.wt))
        self.assertFalse([k for k in seen["env"] if k.startswith("CREW_")])
        self.assertEqual(crew.registration("s5")["role"], "minion")  # registered by crew, once `claude agents` shows it

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
        base = {k: v for k, v in os.environ.items() if k != "CREW_LOCAL"}  # set in crew sessions; not here
        return subprocess.run(["python3", str(self.wt_copy), *args], capture_output=True, text=True, input=input,
                              env=dict(base, CREW_STATE=str(crew.CREW), **(env or {})))

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


class ReviewTest(Sandbox):
    """An approval is a review that names the commit it read; a label alone approves nothing."""

    def setUp(self):
        super().setUp()
        crew.find_prs = lambda b: {".": {"number": 1, "repo": "usr-ein/TriMixxx", "state": "OPEN", "isDraft": False,
                                         "labels": [], "body": "", "headRefOid": "b" * 40, "url": "u"}}
        crew.gh_json = lambda args, repo=None: self.fail("nothing may reach GitHub")

    def test_label_cannot_approve(self):
        with self.assertRaises(crew.Fail) as cm:
            crew.cmd_label(argparse.Namespace(branch="feat", state="approved"))
        self.assertIn("approving review", str(cm.exception))

    def test_review_of_a_head_that_moved(self):
        body = self.tmp / "review.md"
        body.write_text("Looks right.")
        with self.assertRaises(crew.Fail) as cm:
            crew.cmd_review(argparse.Namespace(branch="feat", verdict="approved", commit="a" * 40,
                                               body_file=str(body), comments=None, repo="trimixxx"))
        self.assertIn("review what was pushed since", str(cm.exception))


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
        self.labels, self.body, self.approved = ["review:approved"], "Decisions:\n- [mine] a choice", None
        crew.find_prs = lambda b: {".": {"number": 1, "repo": "usr-ein/TriMixxx", "state": "OPEN", "isDraft": False,
                                         "labels": self.labels, "body": self.body, "headRefOid": "x", "url": "u"}}
        crew.approved_commit = lambda pr: self.approved
        crew.gh_json = lambda args, repo=None: {"state": "MERGED"}

    def feature(self, mixxx=True, bump=True) -> dict:
        """A minion's branch feat: a commit in mixxx, and in the parent a change plus the bump."""
        tips = {}
        if mixxx:
            mwt = self.tmp / "mwt"
            git(crew.MAIN / "mixxx", "worktree", "add", "-q", "-b", "feat", str(mwt), "origin/main")
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
        tips["."] = self.approved = git(pwt, "rev-parse", "HEAD")
        return tips

    def move_main(self):
        """Another feature lands on main meanwhile, in a file of its own."""
        (crew.MAIN / "other").write_text("y\n")
        git(crew.MAIN, "add", "other")
        git(crew.MAIN, "commit", "-qm", "another feature")
        git(crew.MAIN, "push", "-q", "origin", "main")

    def merge(self, dry_run=False) -> int:
        return crew.cmd_merge(argparse.Namespace(branch="feat", dry_run=dry_run))

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
        self.move_main()
        self.assert_refused(3, "rebase")

    def test_merges_only_what_was_approved(self):
        self.feature()
        pwt = self.tmp / "pwt"
        (pwt / "late").write_text("w\n")
        git(pwt, "add", "late")
        git(pwt, "commit", "-qm", "pushed after the approval")
        git(pwt, "push", "-q", "origin", "feat")
        self.assert_refused(2, "since the bitch approved")
        self.approved = None
        self.assert_refused(2, "not an approval")

    def test_a_clean_rebase_keeps_its_approval(self):
        self.feature()
        self.move_main()
        pwt = self.tmp / "pwt"
        git(pwt, "fetch", "-q", "origin")
        git(pwt, "rebase", "-q", "origin/main")
        git(pwt, "push", "-q", "--force-with-lease", "origin", "feat")
        self.assertEqual(self.merge(), 0)
        self.assertEqual(git(self.tmp / "origin" / "parent.git", "rev-parse", "main"), git(pwt, "rev-parse", "HEAD"))

    def test_refuses_decisions_taken_for_sam(self):
        self.feature()
        self.body = "## Decisions\n- [for Sam] polling, not inotify: cheap to switch\n"
        self.assert_refused(2, "for Sam")

    def test_refuses_a_pointer_off_main(self):
        """A bump to a submodule commit that no branch of this name brings onto main: unreviewed, maybe unpushed."""
        mwt = self.tmp / "mwt"
        git(crew.MAIN / "mixxx", "worktree", "add", "-q", "-b", "elsewhere", str(mwt), "origin/main")
        (mwt / "engine.cpp").write_text("change\n")
        git(mwt, "add", "engine.cpp")
        git(mwt, "commit", "-qm", "on another branch")
        git(mwt, "push", "-q", "origin", "elsewhere")
        pwt = self.tmp / "pwt"
        git(crew.MAIN, "worktree", "add", "-q", "-b", "feat", str(pwt), "main")
        git(pwt, "update-index", "--cacheinfo", f"160000,{git(mwt, 'rev-parse', 'HEAD')},mixxx")
        git(pwt, "commit", "-qm", "bump mixxx")
        git(pwt, "push", "-q", "origin", "feat")
        self.approved = git(pwt, "rev-parse", "HEAD")
        self.assert_refused(2, "not on mixxx's main")

    def test_leaves_a_detached_submodule_that_moved(self):
        sub = crew.MAIN / "mixxx"
        git(sub, "switch", "-q", "--detach")
        (sub / "local").write_text("Sam's\n")
        git(sub, "add", "local")
        git(sub, "commit", "-qm", "Sam's commit on a detached HEAD")
        mine = git(sub, "rev-parse", "HEAD")
        self.feature()
        self.assertEqual(self.merge(), 0)
        self.assertEqual(git(sub, "rev-parse", "HEAD"), mine)

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
