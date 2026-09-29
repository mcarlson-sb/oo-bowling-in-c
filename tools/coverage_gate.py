#!/usr/bin/env python3
"""Fails if the library's line coverage is under a minimum.

Reads the output of the CMake `coverage` target (gcov's summary, one "File" and one "Lines
executed" line per source), totals every file under src/ and include/, and compares the whole
library's line coverage with the minimum. Prints each file and the total either way.

Usage: coverage_gate.py <coverage-target-output> --min-lines <percent>
Exit status: 0 at or over the minimum, 1 under it, 2 if there was nothing to read.
"""

import re
import sys

FILE = re.compile(r"^File '(.+)'$")
LINES = re.compile(r"^Lines executed:([0-9.]+)% of ([0-9]+)$")


def library_file(path):
    """src/... or include/..., or None for anything else gcov reports on."""
    normalized = path.replace("\\", "/")
    for part in ("/src/", "/include/"):
        if part in normalized:
            return part.strip("/") + "/" + normalized.split(part, 1)[1]
    return None


def main(argv):
    if len(argv) != 3 or argv[1] != "--min-lines":
        print(__doc__)
        return 2
    minimum = float(argv[2])
    files, current = {}, None
    with open(argv[0], encoding="utf-8", errors="replace") as report:
        for raw in report:
            line = raw.strip()
            match = FILE.match(line)
            if match:
                current = library_file(match.group(1))
                continue
            match = LINES.match(line)
            if match and current is not None and current not in files:
                percent, total = float(match.group(1)), int(match.group(2))
                files[current] = (round(percent * total / 100.0), total)
                current = None
    if not files:
        print("coverage_gate: no library files in the report")
        return 2
    covered = sum(c for c, _ in files.values())
    total = sum(t for _, t in files.values())
    for name in sorted(files):
        c, t = files[name]
        print("  %-28s %4d/%-4d %6.1f%%" % (name, c, t, 100.0 * c / t))
    overall = 100.0 * covered / total
    verdict = "PASS" if overall >= minimum else "FAIL"
    print("coverage_gate: lines %d/%d = %.1f%% (minimum %.1f%%): %s"
          % (covered, total, overall, minimum, verdict))
    return 0 if verdict == "PASS" else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
