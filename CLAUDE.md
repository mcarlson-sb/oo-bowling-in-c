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
