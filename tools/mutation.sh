#!/usr/bin/env bash
# Mutation testing with Mull: builds the library with every mutant compiled in, runs the tests
# once per mutant, and reports each mutant that no test killed. Linux only (Mull has no Windows
# release); needs clang 18 and Mull 0.34.1 for LLVM 18.
#
# Feedback to correct course, run once in a while by hand; not a gate. A run takes minutes: the
# fast loop is atomic TDD, and the gate is every test passing.
#
# Usage: tools/mutation.sh [debug|release] [build-dir]
#   debug   (the default) asserts on, as the debug build runs.
#   release NDEBUG, as the release build runs: reaches the guards that stand behind an assert,
#           which a debug build never gets past.
set -eu
root=$(cd "$(dirname "$0")/.." && pwd)
mode=${1:-debug}
build=${2:-build-mutation-$mode}

case "$mode" in
debug) defines="" ;;
release) defines="-DNDEBUG" ;;
*) echo "usage: $0 [debug|release] [build-dir]" >&2; exit 2 ;;
esac

# Both the plugin, at compile time, and the runner read it: the runner's per-mutant timeout.
export MULL_CONFIG="$root/tools/mull.yml"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
    -DOO_C_MUTATION=ON -DCMAKE_C_FLAGS="$defines" -DCMAKE_CXX_FLAGS="$defines"
cmake --build "$build" --parallel
# The mutants are compiled in but switched off: the tests must all pass before any is tried.
ctest --test-dir "$build" --output-on-failure

# A CPU-time limit on every process from here on. Mull's timeout kills the test process it
# started, but a death test's fork()ed child caught in a mutant's infinite loop outlives it and
# holds the output pipe open, so the run never finishes. The limit is inherited across fork(),
# where an alarm isn't, so it stops that child too. The runner itself mostly waits.
ulimit -t 120
mull-runner-18 "$build/bowling_tests" --reporters=IDE --reporters=Elements \
    --report-dir="$build/mull-report" --report-name=mutation
