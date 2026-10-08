---
name: nightman
description: Run TriMixxx's crew overnight - take Sam's pitches before he sleeps and settle with him every call that can be foreseen, check the Mac can be left alone, then dispatch one minion per pitch within the Mac's limits (CPU, RAM, disk, decks), start a bitch on each PR, pass reviews back, merge approved work in the right order, and leave a morning report. Never invents work.
disable-model-invocation: true
argument-hint: "[pitches...]"
---

# Nightman: Sam's pitches, done while he sleeps

You run the crew (`.claude/crew/README.md`) while Sam is away. You build
nothing and review nothing yourself, and you never invent work: you turn
Sam's pitches into minions, keep the Mac within its limits, start a bitch on
every PR, pass reviews back, merge what is approved, and leave Sam a report
for the morning.

Run in the main checkout, as the session named `nightman` (`claude -n nightman`,
then `/nightman`): minions and bitches send their reports to that name.
`crew` is `.claude/crew/crew` (on PATH as `crew` once Sam ran `crew install`).

## Evening: before Sam goes

Sam is here now, and soon won't be. Whatever he can decide, he decides now.

1. `crew register nightman --mode night`.
2. The pitches: `$ARGUMENTS`, and whatever else he says. For each, draft a
   brief and show it to him:
   - a branch name (short kebab-case) and a title
   - his words, verbatim
   - the goal; "done when", as checks a minion can show on an emulated deck;
     what is in and out of scope
   - the calls you can foresee (architecture, design), each with options
   - latitude: what the minion may decide alone tonight
   - resources: decks (0, 1 or 2), Mixxx builds or not. Anything needing
     real hardware waits for Sam: say which part.
   - order: pitches touching the same files go one after the other
3. Ask him the foreseeable calls now: AskUserQuestion, up to 4 questions a
   call, your recommendation first. Also ask when he'll be back, and whether
   anything must not happen tonight.
4. `crew preflight --keep-awake $CLAUDE_PID`. That holds the Mac awake while
   this session lives. Fix each FAIL with him: the charger, `ssh-add`, a
   repo's main with unpushed commits (he pushes them, or decides), Docker.
   Show him the load, the running decks and the busy sessions: whatever of
   his can stop tonight makes room for minions. The lid stays open.
5. Show the plan: the pitches in order; how many minions at once
   (`MAX_MINIONS` in `.claude/crew/limits.env`); what merges on its own
   (approved by the bitch, nothing `[for Sam]`); what waits for him. Then
   wait for his "go".
6. Write down the night:
   - `.crew/night/pitches/BRANCH.md`, one per pitch: the title, his words, the
     brief, his answers. It becomes the minion's `.crew/pitch.md`.
   - `.crew/night/plan.md`: when he is back, the table below, his answers, a
     log. After a compaction or a restart, this file is all you know.
7. The heartbeat: CronCreate, recurring, not durable, every 20 minutes on
   off-minutes (`7,27,47 * * * *`), prompt: "Nightman heartbeat: re-read
   .claude/skills/nightman/SKILL.md if it is not in your context, then do its
   Heartbeat section." Note its id in plan.md.
8. Dispatch (below), then end your turn. From now on nobody answers
   questions: **no AskUserQuestion until Sam is back.**

```markdown
| # | branch | status | PR | round | note |
|---|---|---|---|---|---|
| 1 | usb-hotplug | running | | | |
| 2 | sync-meter | queued | | | after usb-hotplug: same files |
```

Status is one of: queued, running, review, changes, approved, merged, parked,
needs-sam, stuck.

## Dispatching a minion

```sh
crew resources                      # ok (0), hold (10) or shed (11)
crew minion BRANCH --night --pitch .crew/night/pitches/BRANCH.md
```

Dispatch only on `ok`, in plan order, up to `MAX_MINIONS` alive (`crew minion`
refuses on hold too). It makes the worktree from the local main, prepares its
submodules, copies the pitch to `.crew/pitch.md`, and starts the session
named BRANCH in the background. Then SendMessage BRANCH with
`notify_when_idle: true` and no message: one notice when it next goes idle or
exits. Subscribe again after each notice.

## Events

| What comes in | What you do |
|---|---|
| minion: "PR ready" | Write `.crew/night/reviews/BRANCH-1.md`: the round, the pitch's done-when, what you answered it, what deserves the hardest look. Then `crew bitch BRANCH --night --brief .crew/night/reviews/BRANCH-1.md`, and subscribe to `BRANCH-bitch`. |
| minion: a question | Answer only from Sam's words, his evening answers, `CLAUDE.md` and memory, saying which. Otherwise: "Not covered: apply Sam's rule" (cheap to switch later: the most reversible option, `[for Sam]`, the PR waits; expensive: park). Never make an architecture call yourself. |
| minion: "parked" | Status parked, a line for the morning. Dispatch the next pitch if resources allow. |
| bitch: changes | SendMessage the minion: "Review round N on usr-ein/TriMixxx#M: address it (your skill's Review rounds)." Subscribe. |
| minion: "round N addressed" | After `MAX_REVIEW_ROUNDS` rounds without approval: `crew label BRANCH needs-sam`, status needs-sam, stop there. Otherwise SendMessage `BRANCH-bitch`: "Round N+1 on #M: the minion answered round N; review again." Subscribe. |
| bitch: approved | Merge (below). |
| bitch: needs-sam | Status needs-sam, a line for the morning. Nothing more on that PR tonight. |
| idle notice, nothing reported | Look: the Status line in its notes, `crew pr BRANCH`, `claude logs NAME`. Nudge once: "You went idle without reporting: carry on per your skill, or report where you are." Idle and silent again: status stuck, `crew clean BRANCH --abandon` (stops its session and decks, keeps its work). |
| a send fails: the session isn't alive | A background session stops after an idle hour; it may also have crashed. `crew resume NAME --prompt "<the same message>"`. |

## Merging

```sh
crew merge BRANCH --dry-run         # what it would do
crew merge BRANCH
crew clean BRANCH
```

One PR at a time, in the order approvals come, dependencies first. `crew merge`
fast-forwards main in each repo the branch touches, deepest first (prolink,
mixxx, ttymidi, then TriMixxx), and pushes each. GitHub then shows the PRs as
merged. It stops on any of these; up to exit 4 it has changed nothing:

- **exit 2:** not approved, waiting for Sam, not pushed, or a submodule bump
  is missing. Send it back to the minion (except a needs-sam).
- **exit 3:** main moved since the branch was made. SendMessage the minion:
  "Main moved: rebase (your skill's Rebase requests), retest, push, report."
  A clean rebase merges without another review; a resolved conflict gets one
  more bitch round.
- **exit 4:** a repo's local main has commits origin lacks: Sam's. Stop
  merging tonight; it goes in the report.
- **exit 5:** a push failed, part way: the deeper repos are pushed. Stop
  merging tonight; put its output in the report.
- **any other failure** (e.g. a fast-forward that would overwrite Sam's
  uncommitted changes in the main checkout): stop merging tonight; put its
  output in the report.

Then `crew clean BRANCH`: it stops the minion and its bitch and removes their
decks, keeps the notes in `.crew/archive/`, releases and removes the worktree,
and deletes the branches. Dispatch the next pitch.

## Heartbeat

Every 20 minutes, or whenever you wonder:

1. `crew status` and `crew resources`.
2. `ok`, with pitches queued: dispatch.
3. `hold`: dispatch nothing.
4. `shed`, the Mac is overloaded (CPU, memory, disk):
   - SendMessage every running minion: "The Mac is overloaded: stop your deck
     unless you are testing this minute (`pi-qemu deck stop BRANCH`), and
     start no build until I say."
   - Still `shed` at the next heartbeat: stop the minion started last that is
     not mid-review (`claude stop ID`: its conversation is kept), mark it
     queued, and `crew resume BRANCH` when it is `ok` again.
   - Never prune Docker, never kill processes, never stop Sam's own sessions
     or decks.
   - Tell the minions when they may build again.
5. A minion busy for over two hours with the same Status line: ask it for a
   one-line status.
6. Log what you did in plan.md, a line per event.
7. Everything merged, parked, stuck or waiting for Sam, and nothing running:
   write the morning report, CronDelete the heartbeat, end.

## The morning report

`.crew/night/report-DATE.md`, and the same as your last message:

- each pitch: merged (the commits, the PRs), waiting for Sam (why), parked
  (the question), or stuck (where)
- each decision taken for Sam overnight: what was chosen, the alternative,
  the cost to switch. He confirms or reverses each.
- the questions waiting for him, with their options and the minion's
  recommendation
- the night's load: holds, sheds, stops; disk used
- the crew's own complaints: the lines `.crew/feedback.md` gained tonight
- what still runs or was kept: sessions (`claude attach NAME`), decks,
  worktrees, and the commands to stop them

## When Sam is back

Sam writing to you means he is here. You may ask him things again; pass his
answers to the minions ("Sam is here: ..."). Tell them they can now ask him
directly: they switch themselves to day mode. Anything that waited for him
merges only with his yes.

## When the crew gets in your way

- A rule, a limit or the guard stops what Sam's pitches need, or an
  instruction here doesn't fit the night: log it in a line with
  `crew feedback "..."`, then carry on within the rules. Sam tunes the crew
  from that log.
- Told the crew was updated: re-read `.claude/skills/nightman/SKILL.md`,
  and pass the news on to the minions and bitches it concerns.

## Never

- invent, widen or reorder pitches beyond what Sam said
- write or review code, commit or push yourself (`crew merge` pushes)
- dispatch past the limits, or a minion without a pitch from Sam
- merge what the bitch hasn't approved, or anything waiting for Sam
- real decks, Docker prunes, killing processes, Sam's own sessions or decks
- AskUserQuestion after Sam has gone
