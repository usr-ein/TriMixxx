# A stand-in for `pi-qemu image build trimixxx0`, for tst_golden: it holds the
# deck's lock and keeps its attempt and the golden's record as the real one
# does, in a fraction of a second.
#
#   python3 fakebuild.py CACHE REPO MODE
#
#   build        the golden made from what REPO holds
#   build-move   the same, with a merge while it builds: REPO's main and
#                origin/main move on to a new deck change, so its card says
#                the checkout changed
#   fail         a failed build
#   hang:AGE:MAX building since AGE seconds ago, allowed MAX minutes, until killed
import datetime
import fcntl
import json
import os
import subprocess
import sys
import time

cache, repo, mode = sys.argv[1], sys.argv[2], sys.argv[3]
os.makedirs(os.path.join(cache, "build"), exist_ok=True)
os.makedirs(os.path.join(cache, "golden"), exist_ok=True)
lock = open(os.path.join(cache, "build", "trimixxx0.lock"), "a+")
fcntl.flock(lock, fcntl.LOCK_EX)


def git(*args):
    return subprocess.check_output(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@t",
                                    "-c", "commit.gpgsign=false", *args], text=True).strip()


def stamp(ago=0):
    t = datetime.datetime.now(datetime.timezone.utc) - datetime.timedelta(seconds=ago)
    return t.strftime("%Y-%m-%dT%H:%M:%SZ")


def write(path, obj):
    with open(path + ".new", "w") as f:
        json.dump(obj, f)
    os.replace(path + ".new", path)


attempt_file = os.path.join(cache, "build", "trimixxx0.build.json")
head = git("rev-parse", "HEAD")
source = {"commit": head, "clean": True, "checkout": repo}
attempt = {"state": "building", "pid": os.getpid(), "started": stamp(), "source": source, "stuck_after_min": 60}
if mode.startswith("hang:"):
    _, age, most = mode.split(":")
    attempt.update(started=stamp(int(age)), stuck_after_min=int(most))
    write(attempt_file, attempt)
    time.sleep(600)
    sys.exit(0)
write(attempt_file, attempt)
if mode == "build-move":
    with open(os.path.join(repo, "pi_config", "a.service"), "a") as f:
        f.write("a merge meanwhile\n")
    git("commit", "-qam", "a merge while the golden is made")
    git("update-ref", "refs/remotes/origin/main", "HEAD")
time.sleep(0.5)
if mode == "fail":
    attempt.update(state="failed", why="deploy step 004_mixxx failed (exit 1)", ended=stamp())
    write(attempt_file, attempt)
    sys.exit(1)
record = dict(source, built=stamp(), made=stamp(), card="fake")
after = git("rev-parse", "HEAD")
if after != head:
    record.update(clean=False, why=f"the checkout changed during the build: it is at {after[:7]} now")
write(os.path.join(cache, "golden", "trimixxx0.json"), record)
attempt.update(state="built", ended=stamp())
write(attempt_file, attempt)
