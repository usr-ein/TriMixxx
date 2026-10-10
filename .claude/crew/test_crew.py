"""Tests for crew: the guard's rules, the drift check, session names, and `crew merge`
on throwaway repositories (a parent with a submodule, as TriMixxx and the Mixxx fork).

    python3 -m unittest discover -s .claude/crew -v
"""

import argparse
import contextlib
import importlib.machinery
import importlib.util
import io
import json
import os
import re
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
                                                    "run", "live_session", "claude_sessions", "resolve_host", "locks",
                                                    "gh", "snapshot")}
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
        self.assertTrue(self.denied(self.minion, "crew send nightman hi"))
        self.assertFalse(self.denied(self.nightman, "crew send feat-bitch 'Round 2 on #7'"))
        self.assertTrue(self.denied(self.day_minion, "crew adopt other --brief /tmp/b.md"))
        self.assertTrue(self.denied(self.bitch, "crew adopt feat --brief /tmp/b.md"))
        self.assertFalse(self.denied(self.nightman, "crew adopt feat --brief .crew/night/pitches/feat.md"))
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

    def test_cdjs(self):
        """A minion's emulated CDJs are BRANCH-a and BRANCH-b: the rest it only looks at. On 2026-10-10 three minions
        ran CDJs so named, and nothing kept one off another's."""
        for cmd in ("pi-qemu cdj up feat-a --link feat-net", "pi-qemu cdj up --link feat-net --dsp-model feat-b",
                    "pi-qemu cdj up feat-a --sd=/tmp/sd.img", "pi-qemu cdj press feat-a play --hold-ms 900",
                    "pi-qemu cdj rotary feat-b -3", "pi-qemu cdj rm feat-a", "pi-qemu/app/build/pi-qemu cdj rm feat-b",
                    "pi-qemu cdj list", "pi-qemu cdj status other-a", "pi-qemu cdj shot other-b /tmp/x.png",
                    "pi-qemu cdj build", "pi-qemu cdj up other-a --help", "pi-qemu cdj", "pi-qemu cdj help"):
            for reg in (self.minion, self.day_minion):
                self.assertIsNone(crew.bash_reason(cmd, reg, str(self.wt)), cmd)
        for cmd in ("pi-qemu cdj up other-a", "pi-qemu cdj up --link feat-net other-a", "pi-qemu cdj rm feat",
                    "pi-qemu cdj up --sd feat-a other-a", "pi-qemu cdj press other-b play", "pi-qemu cdj run other-a",
                    "pi-qemu cdj rotary -3 feat-a", "pi-qemu cdj rm feat-c", "pi-qemu cdj up",
                    "cd /tmp && pi-qemu cdj rm other-a", "pi-qemu cdj up other-a --link -h"):  # -h: --link's value
            for reg in (self.minion, self.day_minion):
                self.assertIn("your CDJs are feat-a and feat-b", crew.bash_reason(cmd, reg, str(self.wt)), cmd)

    def test_cdjs_the_bitch_and_the_nightman(self):
        for cmd in ("pi-qemu cdj list", "pi-qemu cdj status feat-a", "pi-qemu cdj shot feat-b /tmp/x.png"):
            for reg in (self.bitch, self.night_bitch, self.nightman):
                self.assertFalse(self.denied(reg, cmd), f"{reg['role']}: {cmd}")
        for cmd in ("pi-qemu cdj up feat-a", "pi-qemu cdj press feat-a play", "pi-qemu cdj rm feat-a",
                    "pi-qemu cdj build"):
            self.assertIn("only looks", crew.bash_reason(cmd, self.bitch, str(self.wt)), cmd)
        self.assertFalse(self.denied(self.nightman, "pi-qemu cdj rm feat-a && pi-qemu cdj rm other-b"))  # shedding
        for cmd in ("pi-qemu cdj up feat-a", "pi-qemu cdj press feat-a play", "pi-qemu cdj build"):
            self.assertIn("only removes", crew.bash_reason(cmd, self.nightman, str(self.wt)), cmd)

    def test_the_cdj_firmware_is_sams(self):
        """What every CDJ boots, as the golden card is what every deck starts from."""
        for reg in (self.minion, self.day_minion, self.bitch, self.nightman):
            self.assertIn("Sam's call", crew.bash_reason("pi-qemu cdj firmware ~/Downloads/C2KNXS.UPD", reg,
                                                         str(self.wt)))

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


def snapshot(**kw) -> dict:
    """A quiet Mac, as crew.snapshot() sees it, with kw on top."""
    return dict({"ncpu": 12, "load": [1, 1, 1], "pressure": 1, "mem_free_pct": 70, "swap_gb": 0.5, "disk_gb": 100,
                 "docker_gb": 20, "decks": [], "cdjs": [], "minions": [], "bitches": [], "sessions": []}, **kw)


def cdj_list(*cdjs) -> str:
    """`pi-qemu cdj list`, as pi-qemu prints it (pi-qemu/app/src/cli/commands/cdj.cpp): (NAME, PID or 0, LINK)."""
    if not cdjs:
        return "no CDJs\n"
    return "".join(f"{name.ljust(20)} {(f'running (pid {pid})' if pid else 'stopped').ljust(20)} 02:43:44:12:34:56"
                   f"{', on link ' + link if link else ', no cable'}\n" for name, pid, link in cdjs)


class VerdictTest(unittest.TestCase):
    """The resources verdict, Docker's VM disk and the emulated CDJs among it."""

    LIM = {"DISK_MIN_GB": 40, "DISK_SHED_GB": 20, "DOCKER_MIN_GB": 8, "DOCKER_SHED_GB": 4, "LOAD_MAX": 0.75,
           "LOAD_SHED": 1.5, "MAX_MINIONS": 2, "MAX_DECKS": 3, "DECKS_PER_MINION": 1, "MAX_CDJS": 4,
           "CDJS_PER_MINION": 2}
    PURPOSES = ("minion", "build", "deck", "cdj")

    def verdict(self, purpose, **kw):
        return crew.verdict(snapshot(**kw), self.LIM, purpose)[0]

    def test_docker_disk(self):
        self.assertEqual([self.verdict(p, docker_gb=20) for p in self.PURPOSES], ["ok", "ok", "ok", "ok"])
        self.assertEqual([self.verdict(p, docker_gb=6) for p in self.PURPOSES], ["hold", "hold", "ok", "ok"])
        self.assertEqual([self.verdict(p, docker_gb=3) for p in self.PURPOSES], ["shed", "shed", "shed", "shed"])
        self.assertEqual(self.verdict("build", docker_gb=None), "ok")  # Docker not up: preflight says so, not this

    def test_cdjs_have_limits_of_their_own(self):
        """A CDJ takes about two cores and ~0.15 GB: MAX_CDJS on the Mac, and a minion is dispatched only while its
        pair fits (CDJS_PER_MINION). A stopped one costs nothing, and decks and builds are held by the load, not by
        the count of CDJs: a minion with its pair up still starts the deck it tests them with."""
        pair = [("a-a", "running"), ("a-b", "running"), ("b-a", "stopped")]
        self.assertEqual([self.verdict(p, cdjs=pair) for p in self.PURPOSES], ["ok", "ok", "ok", "ok"])
        three = pair + [("sam1", "running")]  # Sam's count too
        self.assertEqual([self.verdict(p, cdjs=three) for p in self.PURPOSES], ["hold", "ok", "ok", "ok"])
        self.assertEqual(crew.verdict(snapshot(cdjs=three), self.LIM, "minion")[1],
                         ["3 CDJs running: no room for a minion's 2 (max 4)"])
        four = three + [("b-b", "running")]  # two minions' pairs
        self.assertEqual([self.verdict(p, cdjs=four) for p in self.PURPOSES], ["hold", "ok", "ok", "hold"])
        self.assertEqual(crew.verdict(snapshot(cdjs=four), self.LIM, "cdj")[1],
                         ["4 CDJs running (max 4): a-a, a-b, sam1, b-b"])
        self.assertEqual(self.verdict("deck", cdjs=pair, decks=[("a", "running"), ("b", "running"), ("c", "running")]),
                         "hold")  # decks have theirs

    def test_cdjs_still_count_in_the_load(self):
        self.assertEqual([self.verdict(p, load=[10, 10, 4]) for p in self.PURPOSES], ["hold"] * 4)
        self.assertEqual(self.verdict("cdj", load=[19, 19, 19]), "shed")
        self.assertEqual(self.verdict("cdj", pressure=2), "hold")

    def test_a_cdj_needs_no_disk(self):
        """Its logs grow ~0.5 GB an hour; it never builds: held by neither disk's minimum, shed by both."""
        self.assertEqual([self.verdict(p, disk_gb=30) for p in self.PURPOSES], ["hold", "ok", "hold", "ok"])
        self.assertEqual(self.verdict("cdj", disk_gb=10), "shed")

    def test_describe_counts_them(self):
        text = crew.describe(snapshot(cdjs=[("a-a", "running"), ("b-a", "stopped")]), self.LIM)
        self.assertIn("decks running 0/3 (none); CDJs running 1/4 (a-a); minions 0/2", text)


class CdjListTest(Sandbox):
    """crew reads the CDJs from `pi-qemu cdj list`, as it reads the decks from `pi-qemu deck list`."""

    def listing(self, stdout, code=0, stderr=""):
        calls = []

        def run(cmd, **kw):
            calls.append(cmd)
            return subprocess.CompletedProcess(cmd, code, stdout, stderr)
        crew.run = run
        found = crew.cdjs()
        self.assertEqual(calls, [["pi-qemu", "cdj", "list"]])
        return found

    def test_running_and_stopped(self):
        self.assertEqual(self.listing(cdj_list(("crew-cdj-resources-a", 54696, ""), ("cdj-gui-b", 0, "booth"),
                                               ("x", 7, "net-1"))),
                         [("crew-cdj-resources-a", "running"), ("cdj-gui-b", "stopped"), ("x", "running")])

    def test_none(self):
        self.assertEqual(self.listing(cdj_list()), [])
        self.assertEqual(self.listing("", code=2, stderr="pi-qemu: no command cdj\n"), [])  # a pi-qemu before CDJs


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


class SessionTest(Sandbox):
    """Sessions that have ended: `claude agents` still lists one stopped after its idle hour, status None."""

    def setUp(self):
        super().setUp()
        crew.MAIN = self.tmp / "main"
        self.wt = self.repo(crew.MAIN / ".claude" / "worktrees" / "feat")
        self.listed, self.launched = [], []
        crew.claude_sessions = lambda: self.listed

        def run(cmd, cwd=None, env=None, **kw):  # `claude --bg ...`: it shows up in `claude agents` at once
            self.launched.append(cmd)
            self.listed.append({"id": "n1", "sessionId": "s-new", "name": cmd[cmd.index("-n") + 1], "status": "busy"})
            return subprocess.CompletedProcess(cmd, 0, "backgrounded · n1\n", "")
        crew.run = run
        crew.register_session("s-old", "bitch", "night", "feat", self.wt)

    def test_a_stopped_session_is_not_alive(self):
        self.listed = [{"name": "feat-bitch", "status": None}, {"name": "nightman", "status": "waiting"}]
        self.assertIsNone(crew.live_session("feat-bitch"))
        self.assertEqual(crew.live_session("nightman")["status"], "waiting")

    def test_send_resumes_what_has_ended(self):
        self.listed = [{"name": "feat-bitch", "status": None, "id": "old"}]
        self.assertEqual(crew.cmd_send(argparse.Namespace(name="feat-bitch", text=["Round", "2", "on", "#7"])), 0)
        cmd = self.launched[-1]
        self.assertEqual(cmd[cmd.index("--resume") + 1], "s-old")
        self.assertIn("Round 2 on #7", cmd[-1])

    def test_send_leaves_a_running_session_to_sendmessage(self):
        self.listed = [{"name": "feat-bitch", "status": "idle", "id": "old"}]
        self.assertEqual(crew.cmd_send(argparse.Namespace(name="feat-bitch", text=["hi"])), 10)
        self.assertEqual(self.launched, [])

    def test_the_nightmans_prompts_say_so(self):
        """A resumed session's prompt is a plain turn: without this, a bitch took the nightman for Sam (day mode)."""
        crew.register_session("s-night", "nightman", "night", None, crew.MAIN)
        sid = os.environ.get("CLAUDE_CODE_SESSION_ID")
        self.addCleanup(lambda: os.environ.update(CLAUDE_CODE_SESSION_ID=sid) if sid else
                        os.environ.pop("CLAUDE_CODE_SESSION_ID", None))
        self.listed = [{"name": "feat-bitch", "status": None}]
        os.environ["CLAUDE_CODE_SESSION_ID"] = "s-night"
        crew.cmd_send(argparse.Namespace(name="feat-bitch", text=["Round 2 on #7"]))
        self.assertTrue(self.launched[-1][-1].startswith("From the nightman, not Sam: You were resumed"))
        self.listed = [{"name": "feat-bitch", "status": None}]
        os.environ["CLAUDE_CODE_SESSION_ID"] = "sams-own-session"
        crew.cmd_send(argparse.Namespace(name="feat-bitch", text=["Round 2 on #7"]))
        self.assertTrue(self.launched[-1][-1].startswith("You were resumed"))

    def test_never_resumes_a_running_session(self):
        """Claude would start a copy of it: two sessions on one conversation."""
        self.listed = [{"name": "feat-bitch", "status": "busy", "id": "x"}]
        crew.launch("feat-bitch", self.wt, True, "hi", crew.crew_env("bitch", "night", "feat", self.wt), "s-old")
        self.assertEqual(self.launched, [])


class AdoptTest(Sandbox):
    """`crew adopt`: a minion or bitch Sam started by day, left to the nightman when he goes. On 2026-10-10 the
    nightman adopted cdj-2k-emu by hand: no verb could switch another session's mode, and the guard kept it out of
    the minion's pitch."""

    def setUp(self):
        super().setUp()
        crew.MAIN = self.tmp / "main"
        self.wt = self.repo(crew.MAIN / ".claude" / "worktrees" / "feat")
        (self.wt / ".crew").mkdir()
        self.pitch = self.wt / ".crew" / "pitch.md"
        self.pitch.write_text("# feat: hot-plug sticks\n\nMode: day. Started 2026-10-10T14:00:22+02:00 by Sam, from "
                              "main abc1234.\n\n## Pitch (Sam's words)\n\nA stick plugged in mid-set shows up\n")
        self.brief = self.tmp / "brief.md"
        self.brief.write_text("**Goal.** Tonight: the hot-plug path, emulated decks only.\n")
        self.listed, self.launched = [], []
        crew.claude_sessions = lambda: self.listed

        def run(cmd, cwd=None, env=None, **kw):  # `claude --bg ...`: it shows up in `claude agents` at once
            self.launched.append(cmd)
            self.listed.append({"id": "n1", "sessionId": "s-new", "name": cmd[cmd.index("-n") + 1], "status": "busy"})
            return subprocess.CompletedProcess(cmd, 0, "backgrounded · n1\n", "")
        crew.run = run
        crew.register_session("s-min", "minion", "day", "feat", self.wt)
        crew.register_session("s-bitch", "bitch", "day", "feat", self.wt)
        crew.register_session("s-night", "nightman", "night", None, crew.MAIN)
        sid = os.environ.get("CLAUDE_CODE_SESSION_ID")
        self.addCleanup(lambda: os.environ.update(CLAUDE_CODE_SESSION_ID=sid) if sid else
                        os.environ.pop("CLAUDE_CODE_SESSION_ID", None))
        os.environ["CLAUDE_CODE_SESSION_ID"] = "s-night"  # the nightman adopts

    def crew(self, *argv) -> tuple[int, str, str]:
        """`crew ARGV...`, through the command line: its exit code, stdout and stderr."""
        out, err = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
            code = crew.main(list(argv))
        return code, out.getvalue(), err.getvalue()

    def adopt(self, name, brief=None) -> tuple[int, str, str]:
        return self.crew("adopt", name, "--brief", str(brief or self.brief))

    def running(self, name, sid):
        return {"name": name, "status": "busy", "sessionId": sid, "cwd": str(self.wt), "kind": "interactive"}

    def test_a_running_day_minion(self):
        self.listed = [self.running("feat", "s-min")]
        code, out, err = self.adopt("feat")
        self.assertEqual(code, 0, err)
        self.assertEqual(crew.registration("s-min")["mode"], "night")
        pitch = self.pitch.read_text()
        self.assertLess(pitch.index("A stick plugged in mid-set"), pitch.index("Tonight: the hot-plug path"))
        self.assertIn("Mode: night, adopted by the nightman", pitch)  # a minion re-reading it won't register day
        self.assertNotIn("Mode: day", pitch)
        message = out.split("\n\n", 1)[1]
        self.assertTrue(message.startswith("From the nightman, not Sam: Sam has gone, and you are adopted"), out)
        self.assertIn("`crew register minion --mode night`", message)
        self.assertIn("SendMessage it this (to: feat)", out)
        self.assertIn("crew adopt feat-bitch", out)  # its bitch is adopted apart
        self.assertEqual(crew.registration("s-bitch")["mode"], "day")
        self.assertEqual(self.launched, [])
        # Once adopted, the guard treats it as a night minion: no real deck locked by itself.
        with self.assertRaises(crew.Fail) as cm:
            os.environ["CLAUDE_CODE_SESSION_ID"] = "s-min"
            crew.cmd_lock(argparse.Namespace(unit="trimixxx1", names=[], holder=None, why=None))
        self.assertIn("the nightman grants", str(cm.exception))

    def test_an_ended_day_minion_is_resumed_with_the_message(self):
        self.listed = [{"name": "feat", "status": None, "sessionId": "s-min", "id": "old"}]
        code, out, err = self.adopt("feat")
        self.assertEqual(code, 0, err)
        cmd = self.launched[-1]
        self.assertIn("--bg", cmd)
        self.assertEqual(cmd[cmd.index("--resume") + 1], "s-min")
        self.assertTrue(cmd[-1].startswith("From the nightman, not Sam: You were resumed as the crew's minion for feat "
                                           "(night mode)."), cmd[-1])
        self.assertIn("you are adopted into night mode", cmd[-1])
        self.assertEqual(cmd[-1].count("re-read"), 1)  # the resume's own start says what to re-read
        self.assertEqual(crew.registration("s-min")["mode"], "night")
        self.assertEqual(crew.registration("s-new")["mode"], "night")  # the copy Claude resumes it as
        self.assertIn("Tonight: the hot-plug path", self.pitch.read_text())

    def test_a_running_day_bitch(self):
        self.listed = [self.running("feat-bitch", "s-bitch")]
        code, out, err = self.adopt("feat-bitch")
        self.assertEqual(code, 0, err)
        self.assertEqual(crew.registration("s-bitch")["mode"], "night")
        self.assertEqual((self.wt / ".crew" / "review-brief.md").read_text(), self.brief.read_text())
        self.assertNotIn("Tonight", self.pitch.read_text())  # the pitch is the minion's
        self.assertIn("`crew register bitch --mode night --branch feat`", out)
        self.assertIn("crew adopt feat ", out)  # its minion is still day
        self.assertIsNotNone(crew.bash_reason("crew merge feat", crew.registration("s-bitch"), str(self.wt)))

    def test_refused(self):
        def refused(name, words, code=2):
            before = (self.pitch.read_text(), [crew.registration(s) for s in ("s-min", "s-bitch")])
            c, out, err = self.adopt(name)
            self.assertEqual(c, code, err)
            self.assertIn(words, err)
            self.assertEqual(before, (self.pitch.read_text(), [crew.registration(s) for s in ("s-min", "s-bitch")]))
            self.assertEqual(self.launched, [])
        self.listed = [self.running("feat", "s-min")]
        refused("other", "no minion or bitch named other")
        refused("nightman", "is the nightman")
        self.brief.write_text("\n")
        refused("feat", "is empty")
        self.brief.write_text("Tonight.\n")
        os.environ["CLAUDE_CODE_SESSION_ID"] = "s-bitch"
        refused("feat", "a bitch does not adopt")
        os.environ["CLAUDE_CODE_SESSION_ID"] = "s-night"
        self.listed = [self.running("feat", "s-elsewhere") | {"cwd": str(crew.MAIN)}]
        refused("feat", "not adopting it")  # named like the minion, but no registration names it, nor its worktree
        # In a question box (Sam's "Merge #N now?", say), a message reaches it only once someone answers.
        self.listed = [self.running("feat-bitch", "s-bitch") | {"status": "waiting"}]
        refused("feat-bitch", "waiting in a question box")
        self.assertFalse((self.wt / ".crew" / "review-brief.md").exists())

    def test_the_nightman_never_wakes_a_day_session(self):
        """Round 1 of #27: once its minion was adopted, a bitch still in day mode, its session ended, was woken by the
        next round's `crew send` in day mode, to ask Sam until morning."""
        self.listed = [{"name": "feat-bitch", "status": None, "sessionId": "s-bitch"}]
        for argv in (("send", "feat-bitch", "Round 2 on #7"), ("resume", "feat-bitch")):
            code, out, err = self.crew(*argv)
            self.assertEqual(code, 2, argv)
            self.assertIn("in day mode", err)
            self.assertIn("`crew adopt feat-bitch --brief FILE`", err)
        self.listed = [self.running("feat-bitch", "s-bitch")]  # running: not "send it a message" either
        self.assertEqual(self.crew("send", "feat-bitch", "Round 2 on #7")[0], 2)
        code, out, err = self.crew("bitch", "feat", "--night", "--brief", str(self.brief))
        self.assertEqual(code, 2)
        self.assertIn("already running, in day mode", err)
        self.assertFalse((self.wt / ".crew" / "review-brief.md").exists())  # refused before anything is written
        self.listed = [self.running("feat", "s-min")]
        pitch = self.pitch.read_text()
        self.assertEqual(self.crew("minion", "feat", "--night", "--pitch", str(self.brief))[0], 2)
        self.assertEqual(self.pitch.read_text(), pitch)  # Sam's words kept
        self.assertEqual(self.launched, [])
        # Adopted with the round's brief instead: woken in night mode, with it.
        self.listed = [{"name": "feat-bitch", "status": None, "sessionId": "s-bitch"}]
        self.brief.write_text("Round 2 on #7: the minion answered round 1; review again.\n")
        self.assertEqual(self.adopt("feat-bitch")[0], 0)
        self.assertIn("(night mode)", self.launched[-1][-1])
        self.assertIn("Round 2 on #7", (self.wt / ".crew" / "review-brief.md").read_text())
        self.assertEqual(crew.registration("s-new")["mode"], "night")

    def test_sam_wakes_his_day_sessions(self):
        """The refusal is the nightman's: Sam's own `crew send` wakes his day bitch as it is."""
        os.environ["CLAUDE_CODE_SESSION_ID"] = "sams-own-session"
        self.listed = [{"name": "feat-bitch", "status": None, "sessionId": "s-bitch"}]
        self.assertEqual(self.crew("send", "feat-bitch", "Round 2 on #7")[0], 0)
        self.assertIn("(day mode)", self.launched[-1][-1])

    def test_already_night_changes_nothing(self):
        """Run twice, it says so and changes nothing: one brief in the pitch, no session woken."""
        self.listed = [self.running("feat", "s-min")]
        self.assertEqual(self.adopt("feat")[0], 0)
        pitch = self.pitch.read_text()
        code, out, err = self.adopt("feat")
        self.assertEqual(code, 2)
        self.assertIn("already in night mode", err)
        self.assertEqual(self.pitch.read_text(), pitch)
        self.listed = [{"name": "feat", "status": None, "sessionId": "s-min"}]
        self.assertEqual(self.adopt("feat")[0], 2)
        self.assertEqual(self.launched, [])

    def test_a_session_resumed_by_hand(self):
        """Sam reopened it with `claude --resume` (a copy, with a new id) and it has not registered again yet."""
        self.listed = [self.running("feat", "s-copy")]
        code, out, err = self.adopt("feat")
        self.assertEqual(code, 0, err)
        self.assertEqual((crew.registration("s-copy")["role"], crew.registration("s-copy")["mode"]),
                         ("minion", "night"))

    def test_a_minion_with_no_pitch_yet(self):
        self.pitch.unlink()
        self.listed = [self.running("feat", "s-min")]
        code, out, err = self.adopt("feat")
        self.assertEqual(code, 0, err)
        self.assertTrue(self.pitch.read_text().startswith("## Night mode: the nightman's brief"))
        self.assertIn("send it Sam's words", out)

    def test_a_held_unit_is_kept(self):
        with contextlib.redirect_stdout(io.StringIO()):
            crew.cmd_lock(argparse.Namespace(unit="trimixxx1", names=[], holder="feat", why="Sam 13:50: go"))
        self.listed = [self.running("feat", "s-min")]
        code, out, err = self.adopt("feat")
        self.assertIn("it holds trimixxx1", out)
        self.assertEqual(crew.locks()["trimixxx1"]["holder"], "feat")


class CleanTest(Sandbox):
    """`crew clean` takes a branch's emulated CDJs with its decks. On 2026-10-10 a minion's CDJs, ~2 cores each,
    outlived it: `cdj up` keeps one for a day."""

    DECKS = ("feat                 ssh port 2222  running (pid 200)\n"
             "feat-b               ssh port -     suspended\n"
             "other                ssh port 2223  running (pid 201)\n")

    def setUp(self):
        super().setUp()
        crew.MAIN = self.repo(self.tmp / "main", branch="main")
        self.wt = crew.MAIN / ".claude" / "worktrees" / "feat"
        git(crew.MAIN, "worktree", "add", "-q", "-b", "feat", str(self.wt), "main")
        (self.wt / ".crew").mkdir()
        (self.wt / ".crew" / "notes.md").write_text("Status: merged\n")
        cdjs = cdj_list(("feat-a", 101, "feat-net"), ("feat-b", 0, ""), ("other-a", 102, ""), ("sam1", 103, ""))
        self.calls, self.failing = [], set()
        real = self.saved["run"]

        def run(cmd, cwd=None, check=True, **kw):  # pi-qemu's verbs recorded, git's run
            if cmd[0] != "pi-qemu":
                return real(cmd, cwd=cwd, check=check, **kw)
            verb = " ".join(cmd[1:])
            self.calls.append((verb, cwd))
            failed = verb in self.failing
            listing = {"deck list": self.DECKS, "cdj list": cdjs}.get(verb, "")
            err = "pi-qemu: it would not stop" if failed else ""
            return subprocess.CompletedProcess(cmd, int(failed), listing, err)
        crew.run = run
        crew.claude_sessions = lambda: []

    def clean(self, abandon=False) -> str:
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(crew.cmd_clean(argparse.Namespace(branch="feat", abandon=abandon)), 0)
        return out.getvalue()

    def removed(self, kind) -> list[str]:
        return [verb.split()[2] for verb, _ in self.calls if verb.startswith(f"{kind} rm ")]

    def test_its_cdjs_go_with_its_decks(self):
        out = self.clean()
        self.assertEqual(self.removed("deck"), ["feat", "feat-b"])
        self.assertEqual(self.removed("cdj"), ["feat-a", "feat-b"])  # running or stopped; never other-a, nor Sam's sam1
        self.assertEqual({cwd for verb, cwd in self.calls if verb.startswith("cdj rm")}, {self.wt})  # its emulator's
        self.assertIn("removed CDJ feat-a", out)
        self.assertFalse(self.wt.exists())

    def test_abandoned_too(self):
        """--abandon keeps the work and the real-deck locks, and stops what costs the Mac: sessions, decks, CDJs."""
        out = self.clean(abandon=True)
        self.assertEqual(self.removed("cdj"), ["feat-a", "feat-b"])
        self.assertIn("sessions, decks and CDJs stopped", out)
        self.assertTrue((self.wt / ".crew" / "notes.md").exists())

    def test_one_that_would_not_go(self):
        self.failing = {"cdj rm feat-b"}
        out = self.clean(abandon=True)
        self.assertIn("removed CDJ feat-a", out)
        self.assertIn("CDJ feat-b not removed (pi-qemu: it would not stop): `pi-qemu cdj rm feat-b`", out)


class StatusTest(Sandbox):
    """`crew status`: each branch's decks and CDJs, and in its resources line every CDJ on the Mac."""

    def test_cdjs_by_branch_and_in_all(self):
        crew.MAIN = self.tmp / "main"
        for b in ("feat", "other"):
            (crew.MAIN / ".claude" / "worktrees" / b / ".crew").mkdir(parents=True)
        crew.snapshot = lambda: snapshot(decks=[("feat", "running")], cdjs=[
            ("feat-a", "running"), ("feat-b", "stopped"), ("other-c", "running"), ("sam1", "running")])
        crew.claude_sessions, crew.gh_json, crew.locks = lambda: [], lambda args, repo=None: [], lambda: {}
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(crew.cmd_status(argparse.Namespace()), 0)
        rows = {line.split()[0]: re.split(r"\s{2,}", line) for line in out.getvalue().splitlines()[:3]}
        self.assertEqual(rows["branch"][5:7], ["decks", "CDJs"])
        self.assertEqual(rows["feat"][5:7], ["feat(run)", "feat-a(run) feat-b(sto)"])
        self.assertEqual(rows["other"][5:7], ["-", "-"])  # other-c is no CDJ of its: BRANCH-a and BRANCH-b only
        self.assertIn("CDJs running 3/", out.getvalue())


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
        for state in ("approved", "plan-approved"):
            with self.assertRaises(crew.Fail) as cm:
                crew.cmd_label(argparse.Namespace(branch="feat", state=state))
            self.assertIn("approving review", str(cm.exception))

    def test_review_of_a_head_that_moved(self):
        body = self.tmp / "review.md"
        body.write_text("Looks right.")
        with self.assertRaises(crew.Fail) as cm:
            crew.cmd_review(argparse.Namespace(branch="feat", verdict="approved", commit="a" * 40,
                                               body_file=str(body), comments=None, repo="trimixxx", plan=False))
        self.assertIn("review what was pushed since", str(cm.exception))


class PlanReviewTest(Sandbox):
    """A pitch can start with a review of its plan, in a draft PR: its rounds counted apart, its approval a green
    light to build, never a merge."""

    def setUp(self):
        super().setUp()
        self.reviews, self.labels = [], []
        pr = {"number": 1, "repo": "usr-ein/TriMixxx", "state": "OPEN", "isDraft": True, "labels": [], "body": "",
              "headRefOid": "c" * 40, "url": "u"}
        crew.find_prs = lambda b: {".": dict(pr)}

        def gh_json(args, repo=None):
            if args[:1] == ["api"]:
                return [[{"body": body, "commit_id": c} for body, c in self.reviews]]
            return [{"name": f"review:{s}"} for s in crew.STATES]  # label list

        def gh(args, repo=None, check=True, input=None):
            if input:
                posted = json.loads(input)
                self.reviews.append((posted["body"], posted["commit_id"]))
            if "--add-label" in args:
                self.labels.append(args[args.index("--add-label") + 1])
            return subprocess.CompletedProcess(args, 0, '{"html_url": "h"}', "")
        crew.gh_json, crew.gh = gh_json, gh
        self.body = self.tmp / "review.md"
        self.body.write_text("The plan holds.")

    def review(self, verdict, plan):
        crew.cmd_review(argparse.Namespace(branch="feat", verdict=verdict, commit="c" * 40, body_file=str(self.body),
                                           comments=None, repo="trimixxx", plan=plan))
        return self.reviews[-1][0].split("\n", 1)[0]

    def test_a_plan_is_a_green_light_never_a_merge(self):
        pr = crew.find_prs("feat")["."]
        self.assertIn("plan round 1 · verdict: changes requested", self.review("changes", plan=True))
        self.assertIn("plan round 2 · verdict: green light, not a merge", self.review("approved", plan=True))
        self.assertEqual(self.labels[-1], "review:plan-approved")
        self.assertIsNone(crew.approved_commit(pr))
        self.assertEqual((crew.review_rounds(pr, plan=True), crew.review_rounds(pr)), (2, 0))
        self.assertIn("· round 1 · verdict: approved", self.review("approved", plan=False))  # the result's own count
        self.assertEqual(self.labels[-1], "review:approved")
        self.assertEqual(crew.approved_commit(pr), "c" * 40)


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
