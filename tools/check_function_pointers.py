#!/usr/bin/env python3
"""No function pointers in project code (JPL Power of Ten rule 9 style), checked over clang's
JSON AST dump rather than by grepping text, so a typedef, a macro or an implicit decay can't hide
one.

Flags, in files under src/ or rtos/:
  - a declaration whose type is a function pointer: a variable, struct member, parameter,
    typedef, or a function returning one;
  - taking a function's address: `&f`, or `f` decaying to a pointer anywhere but as the callee
    of a direct call.

Two ways out, both visible in review:
  - tools/function_pointer_allowlist.txt: legacy files still being strangled. Every entry must
    still have a hit, so the list can only shrink.
  - a line carrying the marker `FUNCTION POINTER EXEMPTION:` with a reason, in a file listed as
    an RTOS shell in the allowlist's [rtos-shell] section: what FreeRTOS itself requires.

Usage: check_function_pointers.py [--clang CLANG] [--freertos DIR] [-- extra clang args]
  --freertos DIR  the FreeRTOS-Kernel source, for the RTOS shell's headers (CMake fetches it to
                  <build>/_deps/freertos_kernel-src). Required when rtos/ has sources.
Exit status: 0 clean, 1 violations, 2 the check itself couldn't run.
"""

import json
import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = ROOT / "tools" / "function_pointer_allowlist.txt"
MARKER = "FUNCTION POINTER EXEMPTION:"

# "R (*)(A)", "R (*const)(A)", "R (*[3])(A)", and a function returning one, "R (*(A))(B)".
FUNCTION_POINTER_TYPE = re.compile(r"\(\s*\*[^()]*(\([^()]*\))?[^()]*\)\s*\(")
DECLARATIONS = {"VarDecl", "FieldDecl", "ParmVarDecl", "TypedefDecl", "FunctionDecl"}


def project_relative(path):
    """src/... or rtos/..., or None for anything outside the project's own code."""
    try:
        relative = Path(path).resolve().relative_to(ROOT)
    except ValueError:
        return None
    parts = relative.parts
    if parts and parts[0] in ("src", "rtos"):
        return relative.as_posix()
    return None


class LocationTracker:
    """Clang's JSON dump writes a location's file and line only when they change from the last
    location it wrote, in document order. This replays that, to know where each node is."""

    def __init__(self):
        self.file = None
        self.line = None

    def _bare(self, loc):
        if "file" in loc:
            self.file = loc["file"]
        if "line" in loc:
            self.line = loc["line"]
        return self.file, self.line

    def update(self, loc):
        if not loc:
            return self.file, self.line
        if "spellingLoc" in loc or "expansionLoc" in loc:
            self._bare(loc.get("spellingLoc", {}))
            return self._bare(loc.get("expansionLoc", {}))
        return self._bare(loc)


def type_strings(node):
    qual_type = node.get("type", {})
    return [qual_type.get("qualType", ""), qual_type.get("desugaredQualType", "")]


def is_function_decl_ref(node):
    return (node.get("kind") == "DeclRefExpr" and
            node.get("referencedDecl", {}).get("kind") == "FunctionDecl")


def violation_of(node, parent, index_in_parent):
    kind = node.get("kind")
    if kind in DECLARATIONS and not node.get("isImplicit"):
        if any(FUNCTION_POINTER_TYPE.search(t) for t in type_strings(node)):
            return "%s '%s' has function-pointer type %s" % (
                kind, node.get("name", "?"), type_strings(node)[0])
    if kind == "ImplicitCastExpr" and node.get("castKind") == "FunctionToPointerDecay":
        is_callee = parent is not None and parent.get("kind") == "CallExpr" and index_in_parent == 0
        if not is_callee:
            name = node.get("inner", [{}])[0].get("referencedDecl", {}).get("name", "?")
            return "function '%s' used as a pointer" % name
    if kind == "UnaryOperator" and node.get("opcode") == "&":
        inner = node.get("inner", [{}])[0]
        if is_function_decl_ref(inner):
            return "address of function '%s' taken" % inner["referencedDecl"].get("name", "?")
    return None


def walk(node, tracker, hits, parent=None, index_in_parent=0):
    file, line = tracker.update(node.get("loc"))
    where = project_relative(file) if file else None
    rng = node.get("range", {})
    tracker.update(rng.get("begin"))
    tracker.update(rng.get("end"))
    if where is not None:
        message = violation_of(node, parent, index_in_parent)
        if message:
            # One declaration seen from several translation units can print its type
            # differently (bool, _Bool): key on the declaration, not the message.
            hits.setdefault((where, line, message.split(" has ")[0]), message)
    for index, child in enumerate(node.get("inner", [])):
        walk(child, tracker, hits, node, index)


def freertos_includes(freertos):
    port = Path(freertos) / "portable" / "ThirdParty" / "GCC" / "Posix"
    return ["-I", str(Path(freertos) / "include"), "-I", str(port), "-I", str(port / "utils")]


def project_includes():
    """Every directory of the project's own code that holds a header: each component's include/,
    and its private headers beside its sources."""
    dirs = sorted({h.parent for top in ("src", "rtos") for h in (ROOT / top).rglob("*.h")})
    return [flag for d in dirs for flag in ("-I", str(d))]


def dump_ast(clang, source, extra_args, as_header):
    command = [clang, "-fsyntax-only", "-std=c11"] + project_includes() + extra_args
    if as_header:
        command += ["-x", "c"]
    command += ["-Xclang", "-ast-dump=json", str(source)]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError("clang failed on %s" % source)
    return json.loads(result.stdout)


def read_allowlist():
    legacy, shells, section = set(), set(), "legacy"
    for raw in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line == "[rtos-shell]":
            section = "shell"
        elif section == "shell":
            shells.add(line)
        else:
            legacy.add(line)
    return legacy, shells


def is_exempt(where, line, shells):
    if where not in shells or line is None:
        return False
    lines = (ROOT / where).read_text(encoding="utf-8").splitlines()
    nearby = lines[max(0, line - 2):line]  # the line itself, or the one just above it
    return any(MARKER in text for text in nearby)


def main(argv):
    clang = os.environ.get("CLANG", "clang")
    extra = []
    if "--clang" in argv:
        clang = argv[argv.index("--clang") + 1]
    if "--" in argv:
        extra = argv[argv.index("--") + 1:]

    sources = sorted((ROOT / "src").rglob("*.c")) + sorted((ROOT / "rtos").rglob("*.c"))
    headers = (sorted((ROOT / "src").rglob("*.h")) +
               sorted(h for h in (ROOT / "rtos").rglob("*.h") if h.name != "FreeRTOSConfig.h"))
    if any((ROOT / "rtos").rglob("*.c")):
        if "--freertos" not in argv:
            print("check_function_pointers: could not run: rtos/ has sources; "
                  "pass --freertos <FreeRTOS-Kernel source>")
            return 2
        extra = freertos_includes(argv[argv.index("--freertos") + 1]) + extra
    hits = {}
    try:
        for source in sources:
            walk(dump_ast(clang, source, extra, False), LocationTracker(), hits)
        for header in headers:
            walk(dump_ast(clang, header, extra, True), LocationTracker(), hits)
    except (RuntimeError, OSError, json.JSONDecodeError) as error:
        print("check_function_pointers: could not run: %s" % error)
        return 2

    legacy, shells = read_allowlist()
    violations, legacy_seen, exempted = [], set(), 0
    for (where, line, _), message in sorted(hits.items(),
                                            key=lambda h: (h[0][0], h[0][1] or 0, h[0][2])):
        if where in legacy:
            legacy_seen.add(where)
        elif is_exempt(where, line, shells):
            exempted += 1
        else:
            violations.append("%s:%s: %s" % (where, line, message))

    stale = sorted(legacy - legacy_seen)
    for entry in stale:
        violations.append("%s: on the legacy allowlist but has no function pointers left; "
                          "remove it from %s" % (entry, ALLOWLIST.relative_to(ROOT).as_posix()))

    legacy_hits = sum(1 for h in hits if h[0] in legacy)
    print("check_function_pointers: %d files checked, %d legacy hits in %d allowlisted files, "
          "%d RTOS exemptions, %d violations" % (len(sources) + len(headers), legacy_hits,
                                                 len(legacy_seen), exempted, len(violations)))
    for violation in violations:
        print("  " + violation)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
