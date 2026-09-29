# Working in this repository

## Branches and pushing (the rtos-actor experiment)

- **Push only to `integration/rtos-actor`. Never push to `rtos-actor` directly.** CI's gate
  workflow (`.github/workflows/gate.yml`) runs every gate on each push to the integration branch,
  and its `promote` job fast-forwards `rtos-actor` to that exact commit only when all of them
  pass. A ruleset on `rtos-actor` requires the gate jobs and refuses force pushes and deletion.
- **If `integration/rtos-actor` goes red, stop the line:** fix it or revert it before any other
  commit.
- If `promote` fails because `rtos-actor` moved, rebase `integration/rtos-actor` onto
  `rtos-actor` and push again, so the result is gated.
- `main` and `kay-oo` are not touched, and nothing is merged into them.

## The loop and the gate

- The feedback loop is fast atomic TDD: one failing test, only enough code to pass it, refactor,
  and the debug build's tests after each step.
- The gate is every test passing in every build, with lizard, clang-tidy, the stack tripwires,
  coverage and the function-pointer check. Locally, all of them before each commit; in CI, on
  every commit pushed.
- Mutation testing (`tools/mutation.sh`) is feedback, run by hand at phase stops. It is not a
  gate.

## Commits

- Every commit subject ends with its tag: `[make-easy]`, `[make-change]` or `[clean-up]`. A test
  that passes without new code is `[clean-up]`. No git tags.
- One behavior per commit, and every commit builds and passes its tests.
