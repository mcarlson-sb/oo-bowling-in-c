#!/usr/bin/env python3
"""No heap in a firmware image (Power of Ten rule 3), checked in its linker map: no malloc, free
or their kin linked, no archive member pulled in for them (newlib's printf brings malloc), and no
section named for a heap. FreeRTOS's own heap is configSUPPORT_DYNAMIC_ALLOCATION 0, at compile
time; this is the link's half.

Usage: check_no_heap.py <map file>...
Exit status: 0 clean, 1 heap use found, 2 the check itself couldn't run.
"""

import re
import sys

# The C library's allocator, by its functions' names and newlib's object files for them.
ALLOCATOR = re.compile(
    r"(?<![\w.])(_?malloc|_?free|_?calloc|_?realloc|_?memalign|_malloc_r|_free_r|_calloc_r|"
    r"_realloc_r|_sbrk|_sbrk_r|__malloc_\w+|nano_\w*alloc|nano_free)(?![\w])"
    r"|lib_a-\w*(mallocr|freer|callocr|reallocr|sbrkr)\.o")
HEAP_SECTION = re.compile(r"^\s*\.?\w*heap\w*\b", re.IGNORECASE)

# The map's parts that say what was linked; "Discarded input sections" lists what wasn't.
LINKED_PARTS = ("Archive member included", "Linker script and memory map")
OTHER_PARTS = ("Discarded input sections", "Memory Configuration", "Allocating common symbols")


def heap_uses(map_text):
    uses, linked = [], False
    for number, line in enumerate(map_text.splitlines(), 1):
        if line.startswith(LINKED_PARTS):
            linked = True
            continue
        if line.startswith(OTHER_PARTS):
            linked = False
            continue
        if not linked:
            continue
        if ALLOCATOR.search(line) or HEAP_SECTION.match(line):
            uses.append("%d: %s" % (number, line.strip()))
    return uses


def main(paths):
    if not paths:
        print(__doc__)
        return 2
    failed = False
    for path in paths:
        try:
            with open(path, encoding="utf-8", errors="replace") as map_file:
                uses = heap_uses(map_file.read())
        except OSError as error:
            print("check_no_heap: could not run: %s" % error)
            return 2
        print("check_no_heap: %s: %d heap uses" % (path, len(uses)))
        for use in uses[:20]:
            print("  " + use)
        failed = failed or bool(uses)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
