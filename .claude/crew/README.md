# The crew

Agents that take Sam's pitches to merged pull requests, each in a worktree
and on an emulated deck of its own, side by side on the Mac.

| Role | What it does | Started by | Session name |
|---|---|---|---|
| **minion** | one pitch → one branch → one PR; builds and tests on its own emulated deck; brings Sam only the architecture and design calls | `crew minion BRANCH` (Sam), or the nightman | `BRANCH` |
| **bitch** | reviews one minion's PR: direction, architecture, logic, never style; one review per round, with a verdict | `crew bitch BRANCH` (Sam), or the nightman | `BRANCH-bitch` |
| **nightman** | overnight: Sam's pitches → minions → bitches → merges, within the Mac's limits; a report in the morning; never invents work | Sam: `claude -n nightman`, then `/nightman` | `nightman` |

Their instructions are the skills `.claude/skills/{minion,bitch,nightman}`.
Their tool is `crew` (this directory). Each is one Claude session at max effort.

## Day and night

**By day** Sam is around and is the puppet master: he starts minions and
bitches, and they ask him (AskUserQuestion) for architecture and design
calls. A minion uses a real deck only with his go, and a bitch merges only
after his yes.

**By night** the nightman started them, and nobody can be asked:

- **Questions** go to the nightman. It answers only from what Sam said
  (his pitch, his answers that evening, `CLAUDE.md`, memory). Otherwise the
  minion applies Sam's rule:
  - A call **cheap to switch later**: it takes the most reversible option,
    logs it `[for Sam]`, and goes on. The PR waits for Sam instead of merging.
  - A call **expensive to switch later**: it parks the pitch, with the
    question.
- **Real decks:** none at night.
- **Merges:** the nightman merges what a bitch approved, unless it waits for
  Sam.

A minion or bitch that Sam writes to himself switches to day mode.

## The loop

```
Sam's pitch ─▶ minion: worktree + deck, plan, Sam's calls, build, test, drift checks
                 │
                 ▼  PR (TriMixxx, + companion PRs on the fork, prolink, ttymidi)   review:ready
               bitch: reads the pitch, the minion's decisions, the diff ─▶ one review
                 │  verdict in the review's header and the label
                 ├─ review:changes ─▶ minion answers (new commits, replies) ─▶ bitch again (≤ 3 rounds)
                 ├─ review:needs-sam ─▶ waits for Sam
                 ▼  review:approved
               crew merge: fast-forward main, prolink → mixxx → ttymidi → TriMixxx, push each
                 (only the approved commit, or the same change rebased; never a [for Sam] decision)
               crew clean: sessions, decks, worktree, branches gone; notes kept in .crew/archive/
```

## By day: Sam as puppet master

```sh
crew install                     # once: puts crew on PATH (~/.local/bin/crew -> the main checkout's)
crew minion usb-hotplug "A stick plugged in mid-set shows up in the library within a second"
                                 # in a new tab: makes the worktree, preps its submodules, starts the minion
crew minion usb-hotplug --bg "..."   # or in the background; `claude agents` shows who needs input
crew status                      # every branch: minion, bitch, PR, review state, decks, Status line; the load
crew bitch usb-hotplug           # once its PR is up
claude attach usb-hotplug        # talk to a background minion
crew minion usb-hotplug --resume # re-open a minion whose session has ended
```

In `claude agents`, Space peeks at a session and a number answers its
question.

## By night

```sh
claude -n nightman               # in the main checkout, in a tab left open
/nightman <your pitches>
```

It asks what it must, checks the Mac (`crew preflight`: charger, ssh agent,
pushed mains, Docker, permission mode, load) and keeps it awake. Once you say
go, it runs until the queue is done. In the morning: its last message, and
`.crew/night/report-DATE.md`.

**Before the first real night, rehearse it once, awake.** Give the nightman
two trivial pitches that touch the same file. That exercises:

- the dispatch, the idle notices and the review rounds
- a merge
- an exit 3 on the second merge, and its rebase

## Limits

`limits.env`, read by `crew resources`, which the nightman checks before every
dispatch and every 20 minutes, and minions check before starting a deck:

- minions alive at once
- decks on the Mac, and per minion
- CPU: dispatch while the 5-minute load is under 0.75 × cores; shed above
  1.5 × (15-minute)
- memory pressure
- free disk: 40 GB to dispatch; shed below 20
- review rounds
- drift-check cadence

On **hold**, nothing new starts. On **shed**, minions stop idle decks and
builds, then the newest minion is stopped (its conversation kept) until the
Mac recovers.

## Safety

The hooks in `.claude/settings.json` act on registered crew sessions only.
A session `crew` launches is registered as it starts. In a tab, it registers
itself from its environment. In the background, crew registers it once
`claude agents` shows it: Claude's daemon gives every background session the
environment of the launch that started the daemon, so crew passes it no role.
A resumed session is registered the same way, though Claude Code brings it
back as a copy with a new id. One Sam starts by hand registers with
`crew register`, its skill's first step. Other sessions pay ~30 ms a tool call
and are otherwise untouched.

- **guard** (before Bash and file writes) refuses a crew session:
  - pushing anything but its own branch, merging, or writing outside its
    worktree (a bitch writes nothing; the nightman only its own state)
  - plain `gh` (use `crew gh`)
  - `sudo`, disk commands, Docker prunes, `pkill`/`killall`, `deck golden`,
    `image build`
  - another agent's decks, starting sessions (minion and bitch)
  - real decks at night
- **drift** (after each tool call) interrupts a minion every 25 minutes or
  40 tool calls with a drift check: its pitch, its diff so far, and "what am
  I doing, and does the pitch need it?"
- **session / prompt** keep the session's name its role's (`BRANCH`,
  `BRANCH-bitch`, `nightman`). After a compaction or a resume, they tell it to
  re-read its skill, pitch and notes.

**GitHub, always through `crew gh`.** The shell's identity wrapper for gh
does not run inside Claude Code (its helper functions are not in the shell
snapshot), so plain `gh` acts as whichever account is active, often sam-njia.
In the Mixxx fork it would also open PRs on upstream mixxxdj/mixxx. `crew gh`
uses usr-ein's token and refuses any repository outside usr-ein/. GitHub
won't let the PR's own account approve it, so a bitch's verdict is the
review's header and the PR's `review:` label: `ready`, `changes`, `approved`,
`needs-sam`.

**What `crew merge` lands** is what the bitch approved: the commit its last
review approved, or the same change rebased onto a newer main (compared repo
by repo, as patch ids). A review names the commit it read, and the
`review:approved` label comes only with an approving review (Sam's yes is
recorded as one, quoting him). It refuses:

- a PR whose body still lists a `- [for Sam]` decision
- a submodule pointer that would not be on that submodule's main
- any repo whose local main holds commits origin lacks

It moves a detached submodule in the main checkout only from where main
pointed before.

The repositories are public: so are PRs and reviews.

## Adjusting the crew

While you use it, in any session: **`/crew-tune <what went wrong, or what you
want>`**, or just ask to tune the crew.

1. It looks at what happened: `crew status`, the feedback log, the
   nightman's log, the PR, the session's own transcript.
2. It changes the right layer and runs the tests.
3. On your OK, it commits to main and tells the running sessions.

- **The feedback log:** crew sessions note where the rules or tools got in
  their way with `crew feedback "..."`. `crew feedback` alone shows the log
  (`.crew/feedback.md`); `/crew-tune` with no argument works through it.
- **The layers:**
  - `limits.env`: numbers
  - `.claude/skills/{minion,bitch,nightman}`: behaviour
  - `crew` and `test_crew.py`: the tool and its guard
  - `.claude/settings.json`: hook wiring
  - this README
- **Fixes reach running sessions.** Every copy of `crew` (a worktree's, a
  hook's) runs the main checkout's, so a fix there takes effect at once.
  `CREW_LOCAL=1` runs a worktree's own copy instead, to try a change.
  Sessions re-read their skill from the main checkout when told it changed.

## State

- `.claude/worktrees/BRANCH/.crew/`: `pitch.md`, `notes.md` (its Status line
  shows in `crew status`), `evidence/`, review files. Ignored by git; kept in
  `.crew/archive/` by `crew clean`.
- `.crew/` in the main checkout:
  - `sessions/`: registrations (role, mode, branch, worktree)
  - `night/`: plan, pitches, review briefs, morning reports
  - `feedback.md`: the crew's complaints, for `/crew-tune`
  - `archive/`
- On GitHub, the TriMixxx PR is the record: the pitch, the decisions, the
  testing, every review.

## Tests

```sh
python3 -m unittest discover -s .claude/crew -v
```

The guard's rules, the drift check, session names, and `crew merge` on
throwaway repositories (a parent and a submodule).
