#!/usr/bin/env python3
"""No function pointers in project code (JPL Power of Ten rule 9 style), checked over clang's
JSON AST dump rather than by grepping text, so a typedef, a macro or an implicit decay can't hide
one.

Flags, in files under src/, rtos/ or, for a target image, target/:
  - a declaration whose type is a function pointer: a variable, struct member, parameter,
    typedef, or a function returning one;
  - taking a function's address: `&f`, or `f` decaying to a pointer anywhere but as the callee
    of a direct call.

Three ways out, all visible in review:
  - tools/function_pointer_allowlist.txt: legacy files still being strangled. Every entry must
    still have a hit, so the list can only shrink.
  - a line carrying the marker `FUNCTION POINTER EXEMPTION:` with a reason, in a file listed as
    an RTOS shell in the allowlist's [rtos-shell] section: what FreeRTOS itself requires.
  - a declaration named in the allowlist's [vector-table] section, as `file: name name...`: a
    target's startup, whose vector table the hardware reads. Every name must still have a hit.

Usage: check_function_pointers.py [--clang CLANG] [--freertos DIR] [--port PORT]
                                  [-- extra clang args]
  --freertos DIR  the FreeRTOS-Kernel source, for the RTOS shell's headers (CMake fetches it to
                  <build>/_deps/freertos_kernel-src). Required when rtos/ has sources.
  --port PORT     the FreeRTOS port whose rtos/port/<PORT> is checked: posix (the default) or
                  cm4f. Each port has its own FreeRTOSConfig.h, so the others' are left out.
  --image IMAGE   a target image, with --port cm4f: target/<IMAGE> and target/newlib are
                  checked too. Pass its device headers and the compiler's target after --.
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
    """src/..., rtos/... or target/..., or None for anything outside the project's own code."""
    try:
        relative = Path(path).resolve().relative_to(ROOT)
    except ValueError:
        return None
    parts = relative.parts
    if parts and parts[0] in ("src", "rtos", "target"):
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


def walk(node, tracker, hits, parent=None, index_in_parent=0, declaration=None):
    """Every hit, keyed by where it is, with the name of the file-scope declaration it is in."""
    if parent is not None and parent.get("kind") == "TranslationUnitDecl":
        declaration = node.get("name")
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
            hits.setdefault((where, line, message.split(" has ")[0]), (message, declaration))
    for index, child in enumerate(node.get("inner", [])):
        walk(child, tracker, hits, node, index, declaration)


FREERTOS_PORT_DIRS = {
    "posix": ["portable/ThirdParty/GCC/Posix", "portable/ThirdParty/GCC/Posix/utils"],
    "cm4f": ["portable/GCC/ARM_CM4F"],
}


def freertos_includes(freertos, port):
    dirs = ["include"] + FREERTOS_PORT_DIRS[port]
    return [flag for d in dirs for flag in ("-I", str(Path(freertos) / d))]


def is_in_scope(path, port, image):
    """Everything but another port's rtos/port/<name>/, and target/ only for an image: its own
    directory, and what every image shares."""
    parts = path.relative_to(ROOT).parts
    if parts[0] == "target":
        return image is not None and parts[1] in (image, "newlib")
    return not (len(parts) > 3 and parts[:2] == ("rtos", "port") and parts[2] != port)


def project_files(pattern, port, image):
    return sorted(f for top in ("src", "rtos", "target") if (ROOT / top).is_dir()
                  for f in (ROOT / top).rglob(pattern) if is_in_scope(f, port, image))


def project_includes(port, image):
    """Every directory of the project's own code that holds a header: each component's include/,
    and its private headers beside its sources."""
    dirs = sorted({h.parent for h in project_files("*.h", port, image)})
    return [flag for d in dirs for flag in ("-I", str(d))]


def dump_ast(clang, source, extra_args, as_header, scope):
    command = [clang, "-fsyntax-only", "-std=c11"] + project_includes(*scope) + extra_args
    if as_header:
        command += ["-x", "c"]
    command += ["-Xclang", "-ast-dump=json", str(source)]
    result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        raise RuntimeError("clang failed on %s" % source)
    return json.loads(result.stdout)


def read_allowlist():
    legacy, shells, vector_tables, section = set(), set(), {}, "legacy"
    for raw in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line in ("[rtos-shell]", "[vector-table]"):
            section = line
        elif section == "[rtos-shell]":
            shells.add(line)
        elif section == "[vector-table]":
            where, names = line.split(":", 1)
            vector_tables[where.strip()] = set(names.split())
        else:
            legacy.add(line)
    return legacy, shells, vector_tables


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

    port = argv[argv.index("--port") + 1] if "--port" in argv else "posix"
    image = argv[argv.index("--image") + 1] if "--image" in argv else None
    if port not in FREERTOS_PORT_DIRS or not (image is None or (ROOT / "target" / image).is_dir()):
        print("check_function_pointers: could not run: no port '%s' or image '%s'" % (port, image))
        return 2
    scope = (port, image)
    sources = project_files("*.c", port, image)
    headers = [h for h in project_files("*.h", port, image) if h.name != "FreeRTOSConfig.h"]
    if any((ROOT / "rtos").rglob("*.c")):
        if "--freertos" not in argv:
            print("check_function_pointers: could not run: rtos/ has sources; "
                  "pass --freertos <FreeRTOS-Kernel source>")
            return 2
        extra = freertos_includes(argv[argv.index("--freertos") + 1], port) + extra
    hits = {}
    try:
        for source in sources:
            walk(dump_ast(clang, source, extra, False, scope), LocationTracker(), hits)
        for header in headers:
            walk(dump_ast(clang, header, extra, True, scope), LocationTracker(), hits)
    except (RuntimeError, OSError, json.JSONDecodeError) as error:
        print("check_function_pointers: could not run: %s" % error)
        return 2

    legacy, shells, vector_tables = read_allowlist()
    violations, legacy_seen, exempted, tables_seen = [], set(), 0, set()
    for (where, line, _), (message, declaration) in sorted(
            hits.items(), key=lambda h: (h[0][0], h[0][1] or 0, h[0][2])):
        if where in legacy:
            legacy_seen.add(where)
        elif is_exempt(where, line, shells):
            exempted += 1
        elif declaration in vector_tables.get(where, ()):
            exempted += 1
            tables_seen.add((where, declaration))
        else:
            violations.append("%s:%s: %s" % (where, line, message))

    stale = sorted(legacy - legacy_seen)
    for entry in stale:
        violations.append("%s: on the legacy allowlist but has no function pointers left; "
                          "remove it from %s" % (entry, ALLOWLIST.relative_to(ROOT).as_posix()))
    for where, names in sorted(vector_tables.items()):
        if (ROOT / where).exists() and is_in_scope(ROOT / where, port, image):
            for name in sorted(names - {d for w, d in tables_seen if w == where}):
                violations.append("%s: %s is a [vector-table] exemption with no function pointer "
                                  "left; remove it" % (where, name))

    legacy_hits = sum(1 for h in hits if h[0] in legacy)
    print("check_function_pointers: %d files checked, %d legacy hits in %d allowlisted files, "
          "%d exemptions, %d violations" % (len(sources) + len(headers), legacy_hits,
                                                 len(legacy_seen), exempted, len(violations)))
    for violation in violations:
        print("  " + violation)
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
