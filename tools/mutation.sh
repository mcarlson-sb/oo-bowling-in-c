#!/usr/bin/env bash
# Mutation testing with Mull: builds the library with every mutant compiled in, runs the tests
# once per mutant, and reports each mutant that no test killed. Linux only (Mull has no Windows
# release); needs clang 18 and Mull 0.34.1 for LLVM 18.
#
# Feedback to correct course, run once in a while by hand; not a gate. The fast loop is atomic
# TDD, and the gate is every test passing.
#
# Usage: tools/mutation.sh [debug | release | diff <ref> | only <regex>] [build-dir]
#   debug       (the default) asserts on, as the debug build runs.
#   release     NDEBUG, as the release build runs: reaches the guards that stand behind an
#               assert, which a debug build never gets past.
#   diff <ref>  debug, but only the lines changed since <ref> are mutated: "did the tests keep
#               up with these changes?", in a fraction of the time. Mull skips a file that is
#               new since <ref> altogether, so for new code use only.
#   only <regex> debug, mutating only the source files whose path matches <regex> (for example
#               'src/game_actor\.c'): a new module, all of it.
#
# Every mutant runs the whole test binary, so the slowest tests decide how long a run takes.
# By default the long random runs are left out, which take the binary past Mull's timeout under
# its instrumentation: the three edit property tests (3000 random edits each) and the
# rules-as-data runs against the general reference (5000 random games each). The examples beside
# them kill the same code's mutants. MULL_TEST_FILTER, if set, replaces that filter; set it empty
# to run every test.
#
# It exits non-zero when any mutant survives: feedback to look at, not a failed run. Progress
# shows live on the terminal; redirect it to a file and `tail -f` that to watch from elsewhere.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
mode=${1:-debug}
diff_ref=""
only=""
if [ "$mode" = diff ]; then
    diff_ref=${2:?"diff needs a ref: tools/mutation.sh diff <ref> [build-dir]"}
    shift
elif [ "$mode" = only ]; then
    only=${2:?"only needs a path regex: tools/mutation.sh only <regex> [build-dir]"}
    shift
fi
build=${2:-build-mutation-$mode}

case "$mode" in
debug | diff | only) defines="" ;;
release) defines="-DNDEBUG" ;;
*) echo "usage: $0 [debug | release | diff <ref> | only <regex>] [build-dir]" >&2; exit 2 ;;
esac

# Both the plugin, at compile time, and the runner read it. The diff mode's copy adds the ref;
# the only mode's narrows the files mutated.
mkdir -p "$build"
config="$build/mull.yml"
cp "$root/tools/mull.yml" "$config"
if [ -n "$diff_ref" ]; then
    printf 'gitDiffRef: %s\ngitProjectRoot: %s\n' "$diff_ref" "$root" >> "$config"
fi
if [ -n "$only" ]; then
    sed -i "s#^  - \.\*/src/\.\*\$#  - .*/${only}#" "$config"
    grep -q -- "- .*/${only}" "$config" || { echo "only: could not narrow $config" >&2; exit 2; }
fi
export MULL_CONFIG="$config"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
    -DOO_C_MUTATION=ON -DCMAKE_C_FLAGS="$defines" -DCMAKE_CXX_FLAGS="$defines"
# The diff and only modes' mutants depend on more than the sources: rebuild the library.
if [ -n "$diff_ref" ] || [ -n "$only" ]; then
    cmake --build "$build" --target clean > /dev/null
fi
cmake --build "$build" --parallel
# The mutants are compiled in but switched off: the tests must all pass before any is tried.
ctest --test-dir "$build" --output-on-failure

# A CPU-time limit on every process from here on. Mull's timeout kills the test process it
# started, but a death test's fork()ed child caught in a mutant's infinite loop outlives it and
# holds the output pipe open, so the run never finishes. The limit is inherited across fork(),
# where an alarm isn't, so it stops that child too. A whole run takes seconds; the runner itself
# mostly waits.
ulimit -t 30
filter=${MULL_TEST_FILTER--*.should_leave_the_game_as_a_fresh_game_of_the_edited_balls_would:RulesAsDataTest.*reference*}
test_args=()
if [ -n "$filter" ]; then
    test_args=(--gtest_filter="$filter")
fi
# Few workers: with one per core (24 here) the test binaries slow each other past Mull's timeout,
# and survivors are reported as timed out, which Mull counts as killed. MULL_WORKERS overrides.
mull-runner-18 "$build/bowling_tests" --workers "${MULL_WORKERS:-4}" \
    --reporters=IDE --reporters=Elements \
    --report-dir="$build/mull-report" --report-name=mutation "${test_args[@]}"
