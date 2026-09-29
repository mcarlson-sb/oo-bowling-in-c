#!/usr/bin/env bash
# Mutation testing with Mull: builds the library with every mutant compiled in, runs the tests
# once per mutant, and reports each mutant that no test killed. Linux only (Mull has no Windows
# release); needs clang 18 and Mull 0.34.1 for LLVM 18.
#
# Feedback to correct course, run once in a while by hand; not a gate. The fast loop is atomic
# TDD, and the gate is every test passing.
#
# Usage: tools/mutation.sh [debug | release | diff <ref>] [build-dir]
#   debug       (the default) asserts on, as the debug build runs.
#   release     NDEBUG, as the release build runs: reaches the guards that stand behind an
#               assert, which a debug build never gets past.
#   diff <ref>  debug, but only the lines changed since <ref> are mutated: "did the tests keep
#               up with these changes?", in a fraction of the time.
#
# Every mutant runs the whole test binary, so the slowest tests decide how long a run takes.
# By default the four property tests of the legacy Game facade's edits are left out: they take
# about 80% of a run, and the only mutant nothing else killed is now pinned by an example test
# (RTOS_ACTOR.md, "Mutation testing"). MULL_TEST_FILTER, if set, replaces that filter; set it
# empty to run every test.
#
# It exits non-zero when any mutant survives: feedback to look at, not a failed run. Progress
# shows live on the terminal; redirect it to a file and `tail -f` that to watch from elsewhere.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
mode=${1:-debug}
diff_ref=""
if [ "$mode" = diff ]; then
    diff_ref=${2:?"diff needs a ref: tools/mutation.sh diff <ref> [build-dir]"}
    shift
fi
build=${2:-build-mutation-$mode}

case "$mode" in
debug | diff) defines="" ;;
release) defines="-DNDEBUG" ;;
*) echo "usage: $0 [debug | release | diff <ref>] [build-dir]" >&2; exit 2 ;;
esac

# Both the plugin, at compile time, and the runner read it. The diff mode's copy adds the ref.
mkdir -p "$build"
config="$build/mull.yml"
cp "$root/tools/mull.yml" "$config"
if [ -n "$diff_ref" ]; then
    printf 'gitDiffRef: %s\ngitProjectRoot: %s\n' "$diff_ref" "$root" >> "$config"
fi
export MULL_CONFIG="$config"

cmake -S "$root" -B "$build" -G Ninja -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
    -DOO_C_MUTATION=ON -DCMAKE_C_FLAGS="$defines" -DCMAKE_CXX_FLAGS="$defines"
# The diff mode's mutants depend on the ref, not only on the sources: rebuild the library.
if [ -n "$diff_ref" ]; then
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
filter=${MULL_TEST_FILTER--CorrectionPropertyTest.*:EditRollsPropertyTest.*}
test_args=()
if [ -n "$filter" ]; then
    test_args=(--gtest_filter="$filter")
fi
mull-runner-18 "$build/bowling_tests" --reporters=IDE --reporters=Elements \
    --report-dir="$build/mull-report" --report-name=mutation "${test_args[@]}"
