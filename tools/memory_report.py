#!/usr/bin/env python3
"""A firmware image's memory, region by region, against its linker script's MEMORY: what each
region holds, what is left, and its largest objects. Fails if an object spans two regions, which
the link should already have refused.

Flash is counted where it is programmed: each loaded section's load address (its LMA). RAM is
counted where it lives: each allocated section's address in a region not named FLASH.

Usage: memory_report.py <elf> <linker script> [--prefix arm-none-eabi-] [--largest N]
                        [--symbol NAME]...
  --symbol NAME  report this object's size too, wherever it is (the protocol tables).
Exit status: 0, 1 an object spans regions, 2 the report itself couldn't run.
"""

import re
import subprocess
import sys

MEMORY_LINE = re.compile(
    r"^\s*(\w+)\s*\([^)]*\)\s*:\s*ORIGIN\s*=\s*(\w+)\s*,\s*LENGTH\s*=\s*(\w+)", re.MULTILINE)
SIZE_SUFFIX = {"K": 1024, "M": 1024 * 1024}


def number(text):
    if text[-1] in SIZE_SUFFIX:
        return int(text[:-1], 0) * SIZE_SUFFIX[text[-1]]
    return int(text, 0)


def regions_of(script_text):
    return [(name, number(origin), number(length))
            for name, origin, length in MEMORY_LINE.findall(script_text)]


def region_at(regions, address):
    for name, origin, length in regions:
        if origin <= address < origin + length:
            return name
    return None


def run(command):
    return subprocess.run(command, capture_output=True, text=True, check=True).stdout


def sections_of(objdump_text):
    """(name, size, vma, lma, flags) of each section, from objdump -h."""
    lines = objdump_text.splitlines()
    sections = []
    for index, line in enumerate(lines):
        fields = line.split()
        if len(fields) >= 7 and fields[0].isdigit():
            flags = lines[index + 1].strip() if index + 1 < len(lines) else ""
            sections.append((fields[1], int(fields[2], 16), int(fields[3], 16),
                             int(fields[4], 16), flags))
    return sections


def objects_of(nm_text):
    """(name, address, size, type) of each sized symbol, from nm -S."""
    objects = []
    for line in nm_text.splitlines():
        fields = line.split()
        if len(fields) == 4:
            objects.append((fields[3], int(fields[0], 16), int(fields[1], 16), fields[2]))
    return objects


def main(argv):
    args, prefix, largest, wanted = [], "arm-none-eabi-", 5, []
    iterator = iter(argv)
    for arg in iterator:
        if arg == "--prefix":
            prefix = next(iterator)
        elif arg == "--largest":
            largest = int(next(iterator))
        elif arg == "--symbol":
            wanted.append(next(iterator))
        else:
            args.append(arg)
    if len(args) != 2:
        print(__doc__)
        return 2
    elf, script = args
    try:
        regions = regions_of(open(script, encoding="utf-8").read())
        sections = sections_of(run([prefix + "objdump", "-h", elf]))
        objects = objects_of(run([prefix + "nm", "-S", "--size-sort", elf]))
    except (OSError, subprocess.CalledProcessError) as error:
        print("memory_report: could not run: %s" % error)
        return 2

    used = {name: 0 for name, _, _ in regions}
    for name, size, vma, lma, flags in sections:
        if "ALLOC" not in flags or size == 0:
            continue
        home = region_at(regions, vma)
        if home is not None and "FLASH" not in home.upper():
            used[home] += size
        if "LOAD" in flags:
            programmed = region_at(regions, lma)
            if programmed is not None and "FLASH" in programmed.upper():
                used[programmed] += size

    print("| Region | Origin | Size | Used | Left | Used % |")
    print("|---|---|---|---|---|---|")
    for name, origin, length in regions:
        print("| %s | 0x%08X | %d | %d | %d | %.1f%% |" % (
            name, origin, length, used[name], length - used[name], 100.0 * used[name] / length))

    spanning = []
    by_region = {}
    for name, address, size, kind in objects:
        if kind.lower() not in "bdr":
            continue
        start, end = region_at(regions, address), region_at(regions, address + size - 1)
        if start != end:
            spanning.append(name)
        if start is not None and "FLASH" not in start.upper():
            by_region.setdefault(start, []).append((size, name))
    print()
    for region in sorted(by_region):
        top = sorted(by_region[region], reverse=True)[:largest]
        print("Largest objects in %s: %s" % (
            region, ", ".join("%s %d bytes" % (name, size) for size, name in top)))
    for name in wanted:
        sizes = [size for symbol, _, size, _ in objects if symbol == name]
        print("%s: %s" % (name, ("%d bytes" % sizes[0]) if sizes else "not linked"))
    if spanning:
        print("Objects spanning two regions: %s" % ", ".join(spanning))
        return 1
    print("No object spans two regions.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
