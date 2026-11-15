# How the work is organised

## Who decides

The owner. He plays each build, sends screenshots and short messages, and decides: what the game is, what is wrong, what is cut. Many entries in the timeline begin with one of his sentences. Decisions made in conversation are written into the design docs so they survive.

The work itself is done by AI agents: writing code, running the art pipeline, measuring balance, playing builds by script, reviewing. One agent coordinates; others are given one area each.

## One area, one branch, one report

- Each agent works in its own copy of the repository (a git worktree) on its own branch, on one area: for example melee and formations, or sieges and outworks. Its brief names the files it owns and the files it must leave to others.
- An agent measures before it changes anything, keeps the "before" numbers, and reports what was measured, what changed and what was not met. Unmet targets are written down, not hidden.
- Finished branches are merged into one integration branch. After each merge the checks are run again: the tests the change touches, the golden checksums, the native-against-browser comparison, a build.
- When two branches change the same rules, one merges first and the other re-measures on top of it.

## What goes live, and when

A push to `master` builds the browser version and publishes it. That is the live game. So pushes to `master` happen only on the owner's word. The full test suite runs before such a deploy and not otherwise.

## Rules that came from mistakes

`docs/ARCHITECTURE_RULES.md` was written on 5 Nov after an army walked through a castle wall. A second, thinner way of moving men had been built for the Conquest because joining it to the battle had been called too large.

- **Skirmish is the baseline.** The Conquest is the same world with the fence taken away. Whatever can happen in one can happen in the other.
- **One game, one code.** One battle system, one selection, one order path, one way to draw a man. A second loop over the men is a bug.
- **"Too large" is not a reason** to leave a rule broken. Split the work and do it.
- **Lockstep-ready.** All player intent is an ordered command applied at a tick. The step uses integers and a seeded random source only.
- **Tests pin the rule**, not only a checksum.

Others, smaller, from the same kind of day:
- Text on screen is short and literal. The owner reads all of it in a table and cuts.
- No function keys.
- State that matters is never kept only in a scratch folder.
- A test that is dropped instead of fixed says so in its commit.

## The review loop

Since 14 Nov the work runs as a loop: build, play by script, review, pick the top finding, fix, measure, merge, review again. The reviewer's score for fun went 5.5, 6.5, 7.0, 7.0 across four rounds. When it stopped moving, the response was not another round of features but a narrower question: at six men against six, does a tactic do anything a player could learn? That is the tactics lab, in progress.

## What this way of working is bad at

- An agent told a problem is too big will build a smaller, parallel thing beside it. That happened on 4 Nov and cost a day to undo.
- An agent can make a failing test pass by moving the test. It has happened; the rule now is to say so.
- Measurements made by script do not say how the game feels. The owner's hands are still the only test of that.
- Many branches changing the same rules at once means the pinned checksums are re-pinned again and again, and each re-pin has to be read, not waved through.
