#!/usr/bin/env python3
"""The worst-case stack depth from each task entry, from GCC's static call graph (ENG-1.3).

Reads every .ci file GCC writes with -fcallgraph-info=su under a build directory: each defined
function's frame, and every direct call. From each entry, the deepest path is its own frame plus
the deepest of its callees. It fails, rather than guess, on what it can't bound:
  - recursion, which has no static bound;
  - a frame of dynamic size that GCC couldn't bound;
  - an indirect call: with no function pointers in project code, only FreeRTOS's own task start
    has one, and it is the entry, never a callee;
  - a call to a function defined in two places: name the one the image doesn't link with
    --not-linked (a target's own Fault_Stop links instead of src/support/fault.c's).
A call to a function with no call graph (the C library, pthreads: host-only code under the POSIX
port) costs a fixed allowance per call, and each one reached is named in the report. On top of
the deepest path, one asynchronous frame: what can land on the stack at any point, a signal
handler's frame on the host (the POSIX port's tick and its context switches), an interrupt's on
a target. Both allowances are the measured part of the contract, cross-checked by the painted
stack.

Each entry's budget is a #define in a header, in bytes, so the code that sizes the stack and this
check read the same number.

Usage: stack_depth.py <build-dir> --external-allowance BYTES --async-allowance BYTES
                      --entry FUNCTION=HEADER:MACRO [--entry ...] [--not-linked SOURCE ...]
Exit status: 0 every entry within budget, 1 one over it or unboundable, 2 nothing to read.
"""

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
NODE = re.compile(r'^node: \{ title: "([^"]+)" label: "([^"]+)"')
EDGE = re.compile(r'^edge: \{ sourcename: "([^"]+)" targetname: "([^"]+)"')
FRAME = re.compile(r"^(\d+) bytes \(([a-z,]+)\)$")


class Graph:
    def __init__(self):
        self.frames = {}   # title -> (bytes, qualifier)
        self.by_name = {}  # function name -> titles defining it
        self.calls = {}    # title -> callee titles, in order

    def read(self, path):
        for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
            node = NODE.match(line)
            if node:
                title, label = node.group(1), node.group(2).split("\\n")
                frame = FRAME.match(label[-1]) if len(label) >= 3 else None
                if frame:
                    self.frames[title] = (int(frame.group(1)), frame.group(2))
                    self.by_name.setdefault(label[0], set()).add(title)
                continue
            edge = EDGE.match(line)
            if edge:
                self.calls.setdefault(edge.group(1), []).append(edge.group(2))

    def resolve(self, target):
        """The defining title of a call's target, or None for a function with no call graph."""
        if target in self.frames:
            return target
        name = target.rsplit(":", 1)[-1]
        definitions = self.by_name.get(name, set())
        if len(definitions) > 1:
            raise Unboundable("%s is defined in %d places: %s" % (
                name, len(definitions), ", ".join(sorted(short(d) for d in definitions))))
        return next(iter(definitions)) if definitions else None


class Unboundable(Exception):
    pass


def deepest(graph, title, allowance, externals, memo, active):
    """(bytes, path) of the deepest call chain from title."""
    if title in memo:
        return memo[title]
    if title in active:
        raise Unboundable("recursion through %s" % short(title))
    size, qualifier = graph.frames[title]
    if qualifier == "dynamic":
        raise Unboundable("%s has a frame of unbounded dynamic size" % short(title))
    active.add(title)
    best, best_path = 0, []
    for target in graph.calls.get(title, []):
        if target == "__indirect_call":
            raise Unboundable("%s makes an indirect call" % short(title))
        callee = graph.resolve(target)
        if callee is None:
            externals.add(target)
            depth, path = allowance, [target + " (allowance)"]
        else:
            depth, path = deepest(graph, callee, allowance, externals, memo, active)
        if depth > best:
            best, best_path = depth, path
    active.discard(title)
    memo[title] = (size + best, ["%s (%d)" % (short(title), size)] + best_path)
    return memo[title]


def short(title):
    return title.rsplit("/", 1)[-1]


def budget_of(spec):
    header, macro = spec.split(":", 1)
    text = (ROOT / header).read_text(encoding="utf-8")
    match = re.search(r"^#define %s \(?(\d+)U?\)?" % re.escape(macro), text, re.MULTILINE)
    if not match:
        raise ValueError("no #define %s in %s" % (macro, header))
    return int(match.group(1))


def main(argv):
    required = ("--external-allowance", "--async-allowance", "--entry")
    if len(argv) < 1 or any(option not in argv for option in required):
        print(__doc__)
        return 2
    build = Path(argv[0])
    allowance = int(argv[argv.index("--external-allowance") + 1])
    asynchronous = int(argv[argv.index("--async-allowance") + 1])
    entries = [argv[i + 1] for i, arg in enumerate(argv) if arg == "--entry"]
    not_linked = [argv[i + 1] for i, arg in enumerate(argv) if arg == "--not-linked"]

    graph = Graph()
    files = [f for f in build.rglob("*.ci") if "CompilerId" not in str(f) and
             not any(f.as_posix().endswith("/%s.ci" % source) for source in not_linked)]
    for path in files:
        graph.read(path)
    if not graph.frames:
        print("stack_depth: no call graphs under %s (build with -fcallgraph-info=su)" % build)
        return 2

    failed = False
    for entry in entries:
        function, spec = entry.split("=", 1)
        budget = budget_of(spec)
        try:
            title = graph.resolve(function)
        except Unboundable as reason:
            print("stack_depth: %s: can't be bounded: %s" % (function, reason))
            failed = True
            continue
        if title is None:
            print("stack_depth: %s: not in the call graph" % function)
            failed = True
            continue
        externals = set()
        try:
            depth, path = deepest(graph, title, allowance, externals, {}, set())
            depth += asynchronous
            path.append("an asynchronous frame (%d)" % asynchronous)
        except Unboundable as reason:
            print("stack_depth: %s: can't be bounded: %s" % (function, reason))
            failed = True
            continue
        verdict = "within" if depth <= budget else "OVER"
        failed = failed or depth > budget
        print("stack_depth: %s: %d bytes, %s its budget of %d (%s)"
              % (function, depth, verdict, budget, spec.split(":", 1)[1]))
        print("  deepest path: " + " -> ".join(path))
        print("  outside the call graph, %d bytes a call: %s"
              % (allowance, ", ".join(sorted(externals)) or "none"))
    print("stack_depth: %d files read, %d functions" % (len(files), len(graph.frames)))
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
