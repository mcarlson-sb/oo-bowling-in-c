# Working in this repository

## Branches and pushing (the rtos-actor experiment)

- **Push only to `integration/rtos-actor`. Never push to `rtos-actor` directly.** CI's gate
  workflow (`.github/workflows/gate.yml`) runs every gate on each push to the integration branch,
  and its `promote` job fast-forwards `rtos-actor` to that exact commit only when all of them
  pass. A ruleset on `rtos-actor` requires the gate jobs and refuses force pushes and deletion.
- **After every push to `integration/rtos-actor`, wait for its gate run to finish** (`gh run
  watch <run-id> --exit-status`, or poll the Actions API) **before pushing again.** Never push
  onto a red gate, or onto one that is still running.
- **If `integration/rtos-actor` goes red, stop the line and recover by rewriting it.** A revert
  commit can't make it green: `every-commit` tests every commit in `origin/rtos-actor..HEAD`,
  so the red commit stays in range. `integration/rtos-actor` may be rewritten; `rtos-actor`
  never is (its ruleset refuses force pushes). The recovery:
  1. `git fetch origin` and note the red tip: `red=$(git rev-parse origin/integration/rtos-actor)`.
  2. `git reset --hard origin/rtos-actor`, the last promoted, green commit.
  3. Re-apply the work, corrected: cherry-pick the good commits, fix the bad one.
  4. `git push --force-with-lease=integration/rtos-actor:$red origin HEAD:integration/rtos-actor`,
     which refuses to overwrite anything but the red tip you saw.
  5. Wait for the gate: `promote` fast-forwards `rtos-actor` and the line is green again.
- If `promote` fails because `rtos-actor` moved, rebase `integration/rtos-actor` onto
  `rtos-actor` and push again (with `--force-with-lease`, as above), so the result is gated.
- `main` and `kay-oo` are not touched, and nothing is merged into them.

## The loop and the gate

- The feedback loop is fast atomic TDD: one failing test, only enough code to pass it, refactor,
  and the debug build's tests after each step.
- The gate is every test passing in every build, with lizard, clang-tidy, the stack tripwires,
  coverage and the function-pointer check. Locally: the quick debug build's tests before each
  commit, and all of the gates before each push. In CI: all of them on every push, and
  `every-commit` builds and tests each commit pushed.
- Mutation testing (`tools/mutation.sh`) is feedback, run by hand at phase stops. It is not a
  gate.

## Commits

- Every commit subject ends with its tag: `[make-easy]`, `[make-change]` or `[clean-up]`. A test
  that passes without new code is `[clean-up]`. No git tags.
- One behavior per commit, and every commit builds and passes its tests.

## Cleanup: what every round looks for

Every cleanup round, whether at a phase stop, after a review, or on its own, checks all of these.
Each finding is its own `[clean-up]` commit, unless it changes behavior.

- **Owners.** Code sits in the module whose job it is: validation with the rules, addressing with
  the protocol, plumbing with what it plumbs. A struct or flag that one module only reads from
  another is a sign it belongs elsewhere. Each module's job fits in one sentence.
- **Tell, don't ask.** No caller reads a value field by field to decide for it, or checks before it
  acts ("is it full?", then "push"). The value does the thing and says what happened. Loops over
  another module's list belong in that module.
- **Composed method.** One level of abstraction per function: a function reads as named steps, or
  does one step, not both. Dispatch only dispatches. One way to do a thing, such as hosting a kind,
  everywhere.
- **Dead simple.**
  - indirection left over from an older design;
  - a `bool` parameter that chooses a path, which is split so the caller says what happened;
  - a `default:` that switches off `-Wswitch`;
  - arithmetic written out more than once, which gets a named helper;
  - guards for an API or a hazard that no longer exists, deleted with their tests.
- **Comments.**
  - a comment that restates a signature goes;
  - a comment that needs a second read gets a verb, or goes;
  - design rationale moves to ARCHITECTURE.md, and the code keeps the contract;
  - a comment that states intent becomes a name that reveals it.
- **Dead code.** A function, field or parameter with nothing left to use it goes.
- **Store decisions, derive facts.** A state the code can work out (holding, over) is derived where
  it's read, not stored and kept in step.
- **Proxy limits.** Complexity, lines and parameters are proxies for readability. A shape that
  exists only to satisfy one is undone, if it reads worse without the limit. Resource limits (stack,
  RAM, tripwires) never give way.
- **Write every case out.** A table or switch that lists every (state, selector) pair shows what a
  default path hides. Read it for answers given to answers, and for refusals that say the wrong
  thing.
- **Mutation feedback.** Run `tools/mutation.sh`, debug and release, and read every survivor:
  - an equivalent mutant, or one only a sanitizer can see, is recorded;
  - a real gap gets a test, proved against the mutant made by hand, and made to compile;
  - a run that finishes in seconds didn't build, so its results aren't this code's.
- **Measure before and after.** Record lizard (`-m`), cognitive complexity, NLOC, the function
  count and the largest function before the round starts, and report them again at its end.
